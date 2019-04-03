#include "pch.h"
#include "PhotoProcessor.h"
#include "TorchUtils.h"
#include "../Globals.h"

using namespace std;

namespace imqs {
namespace roadproc {

char RoadTypeToChar(RoadTypes t) {
	switch (t) {
	case RoadTypes::Gravel: return 'g';
	case RoadTypes::Ignore: return 'i';
	case RoadTypes::JeepTrack: return 'j';
	case RoadTypes::Tar: return 't';
	case RoadTypes::NONE:
		IMQS_DIE();
		return ' ';
	}
}

string PhotoProcessor::BaseUrl = "http://roads.imqs.co.za";

static Error FindPhotosOnServer(std::string client, std::string prefix, std::vector<std::string>& photoUrls) {
	// the LIKE statement on the API side looks like this:
	//   like := "gs://roadphoto.imqs.co.za/" + client + "/" + prefix + "%%"

	// eg curl http://roads.imqs.co.za/api/ph/list_photos?client=za.nl.um.--&prefix=2019/2019-02-27/164GOPRO

	string                     api = PhotoProcessor::BaseUrl + "/api/ph/list_photos?";
	ohash::map<string, string> q   = {
        {"client", client},
        {"prefix", prefix},
    };

	auto resp = http::Client::Get(api + url::Encode(q));
	if (!resp.Is200()) {
		// This is often the first error that you'll see from this system, so we add extra detail
		return Error::Fmt("Error fetching photos from %v: %v", api, resp.ToError().Message());
	}

	rapidjson::Document jroot;
	auto                err = rj::ParseString(resp.Body, jroot);
	if (!err.OK())
		return err;
	if (!jroot.IsArray())
		return Error("JSON response from list_photos is not an array");
	for (size_t i = 0; i < jroot.Size(); i++) {
		const auto& j = jroot[(rapidjson::SizeType) i];
		photoUrls.push_back(rj::GetString(j, "URL"));
	}

	return Error();
}

PhotoProcessor::PhotoProcessor() {
	QNotStarted.Initialize(true);
	QDownloaded.Initialize(true);
	QHaveRoadType.Initialize(true);
	QDone.Initialize(true);
	bool debugMode = false;
	if (debugMode) {
		// reduce batch sizes for debugging
		NumFetchThreads        = 1;
		MaxQDownloaded         = 2;
		RoadTypeBatchSize      = 2;
		GravelQualityBatchSize = 2;
		UploadBatchSize        = 2;
	}
}

int PhotoProcessor::Run(argparse::Args& args) {
	auto           username = args.Params[0];
	auto           password = args.Params[1];
	auto           client   = args.Params[2];
	auto           prefix   = args.Params[3];
	PhotoProcessor pp;
	auto           err = pp.RunInternal(username, password, client, prefix, args.GetInt("resume"));
	if (!err.OK()) {
		tsf::print("Error: %v\n", err.Message());
		return 1;
	}
	return 0;
}

Error PhotoProcessor::RunInternal(string username, string password, string client, string prefix, int startAt) {
	Finished = false;

	tsf::print("Logging in\n");
	http::Connection cx;
	auto             err = global::Login(cx, username, password, SessionCookie);
	if (!err.OK())
		return err;

	tsf::print("Loading models\n");
	err = LoadModels();
	if (!err.OK())
		return err;

	tsf::print("Preparing photo queue\n");
	vector<string> photoUrls;
	err = FindPhotosOnServer(client, prefix, photoUrls);
	if (!err.OK())
		return err;
	tsf::print("Found %v photos to process, starting at %v\n", photoUrls.size(), startAt);

	int64_t id = 0;
	for (size_t i = startAt; i < photoUrls.size(); i++) {
		auto j        = new PhotoJob();
		j->PhotoURL   = photoUrls[i];
		j->InternalID = id++;
		QNotStarted.Push(j);
	}

	// launch threads
	vector<thread> threads;
	for (int i = 0; i < NumFetchThreads; i++)
		threads.push_back(thread([&] { FetchThread(); }));

	for (int i = 0; i < NumRoadTypeThreads; i++)
		threads.push_back(thread([&] { RoadTypeThread(); }));

	for (int i = 0; i < NumAssessmentThreads; i++)
		threads.push_back(thread([&] { AssessmentThread(); }));

	for (int i = 0; i < NumUploadThreads; i++)
		threads.push_back(thread([&] { UploadThread(); }));

	// get all queues to flush (eg if a batch isn't filled yet, just go ahead and process the incomplete batch)
	for (int i = 0; i < 100; i++)
		QNotStarted.Push(nullptr);

	// wait for all queues to drain
	while (QNotStarted.Size() != 0 || QDownloaded.Size() != 0 || QHaveRoadType.Size() != 0 || QDone.Size() != 0) {
		os::Sleep(50 * time::Millisecond);
	}
	// set the Finished flag, to signal all threads to exit
	Finished = true;
	for (int i = 0; i < 100; i++) {
		QNotStarted.Push(nullptr);
		QDownloaded.Push(nullptr);
		QHaveRoadType.Push(nullptr);
		QDone.Push(nullptr);
	}

	for (auto& t : threads)
		t.join();

	return Error();
}

void PhotoProcessor::FetchThread() {
	http::Connection cx;
	gfx::ImageIO     imgIO;

	while (!Finished) {
		QNotStarted.SemaphoreObj().wait();
		auto job = QNotStarted.PopTailR();
		if (Finished)
			break;
		if (job == nullptr) {
			QDownloaded.Push(job);
			continue;
		}

		string url = job->PhotoURL;
		if (!strings::StartsWith(url, "gs://")) {
			tsf::print("Invalid photo URL '%v'\n", url);
			delete job;
			continue;
		}
		url = "http://" + url.substr(5);

		http::Response resp;
		for (int i = 0; i < MaxDownloadAttempts; i++) {
			resp = cx.Get(url);
			if (resp.Is200())
				break;
			tsf::print("Failed to download '%v'. Retrying...\n");
			os::Sleep((1 << i) * time::Second);
		}
		if (!resp.Is200()) {
			delete job;
			tsf::print("Failed to download '%v'. Giving up\n");
			continue;
		}

		// The max resolution that we use is 1/4 of the original 4000x3000 GoPro image, so we can get the
		// JPEG library to assist with that downscaling efficiently.
		int   width  = 0;
		int   height = 0;
		void* rgba   = nullptr;
		auto  err    = imgIO.LoadJpegScaled(resp.Body.data(), resp.Body.size(), 4, width, height, rgba, TJPF_RGBA);
		if (!err.OK()) {
			tsf::print("Failed to decode %v: %v\n", err.Message());
			delete job;
			continue;
		}
		if (width != 1000 || height != 750) {
			tsf::print("Expected image to be 4000 x 3000, but image is %v x %v\n", width * 4, height * 4);
			delete job;
			continue;
		}
		gfx::Image quarter(gfx::ImageFormat::RGBA, gfx::Image::ConstructTakeOwnership, width * 4, rgba, width, height);
		//auto       quarter = half.HalfSizeSIMD();
		// crop, preserving only the bottom 1/3 of the frame
		//auto halfCrop    = half.Window(0, half.Height - 500, half.Width, 500);
		auto quarterCrop = quarter.Window(0, quarter.Height - 256, quarter.Width, 256);
		//job->RGB_2000_500 = ImgToTensor(halfCrop);
		job->RGB_1000_256 = ImgToTensor(quarterCrop);

		while (QDownloaded.Size() > MaxQDownloaded) {
			if (Finished)
				break;
			os::Sleep(50 * time::Millisecond);
		}
		tsf::print("Decoded %4d %v\n", job->InternalID, job->PhotoURL);
		QDownloaded.Push(job);
	}
}

void PhotoProcessor::RoadTypeThread() {
	http::Connection cx;
	gfx::ImageIO     imgIO;

	torch::Tensor     batch  = torch::empty({RoadTypeBatchSize, 3, 256, 1000});
	int               ibatch = 0; // number of elements in the batch
	vector<PhotoJob*> batchJobs;  // the jobs inside this batch
	batchJobs.resize(RoadTypeBatchSize);

	while (!Finished) {
		QDownloaded.SemaphoreObj().wait();
		auto job = QDownloaded.PopTailR();
		if (Finished)
			break;

		// null job means a pipeline flush

		if (job) {
			auto t            = job->RGB_1000_256.permute({2, 0, 1}); // HWC -> CHW
			batchJobs[ibatch] = job;
			batch[ibatch]     = t;
			ibatch++;
		}

		if ((!job || ibatch == RoadTypeBatchSize) && ibatch != 0) {
			torch::NoGradGuard nograd;
			GPULock.lock();
			auto res = MRoadType->forward({batch.cuda()}).toTensor().cpu();
			GPULock.unlock();
			auto amax = torch::argmax(res, 1);
			// res shape is [4,3] (BC)
			// amax shape is [4]
			//DumpPhotos(batchJobs);
			//tsf::print("res shape: %v\n", SizeToString(res.sizes()));
			//tsf::print("amax shape: %v\n", SizeToString(amax.sizes()));
			for (int i = 0; i < ibatch; i++) {
				batchJobs[i]->RoadType = (RoadTypes) amax[i].item().toInt();
				//tsf::print("Road Type: %v\n", (int) batchJobs[i]->RoadType);
				while (QHaveRoadType.Size() >= MaxQHaveRoadType) {
					os::Sleep(50 * time::Millisecond);
				}
				QHaveRoadType.Push(batchJobs[i]);
			}
			ibatch = 0;
		}
	}
}

void PhotoProcessor::AssessmentThread() {
	torch::Tensor     gravelBatch  = torch::empty({GravelQualityBatchSize, 3, 256, 1000});
	int               iGravelBatch = 0; // number of elements in the batch
	vector<PhotoJob*> gravelJobs;       // the jobs inside this batch
	gravelJobs.resize(GravelQualityBatchSize);

	while (!Finished) {
		QHaveRoadType.SemaphoreObj().wait();
		auto job = QHaveRoadType.PopTailR();
		if (Finished)
			break;

		// null job means a pipeline flush

		if (job && job->RoadType == RoadTypes::Gravel) {
			auto t                    = job->RGB_1000_256.permute({2, 0, 1}); // HWC -> CHW
			gravelJobs[iGravelBatch]  = job;
			gravelBatch[iGravelBatch] = t;
			iGravelBatch++;
		}

		if (iGravelBatch == GravelQualityBatchSize || (job == nullptr && iGravelBatch != 0)) {
			// Run analysis on all models
			vector<torch::Tensor> results; // one for each model
			auto                  batchCuda = gravelBatch.cuda();
			torch::NoGradGuard    nograd;
			GPULock.lock();
			for (auto& m : Models)
				results.push_back(m.Model->forward({batchCuda}).toTensor());
			GPULock.unlock();

			// consume results
			for (size_t iModel = 0; iModel < Models.size(); iModel++) {
				auto res = results[iModel].cpu();
				//tsf::print("res shape: %v\n", SizeToString(res.sizes()));
				for (int iSample = 0; iSample < iGravelBatch; iSample++) {
					gravelJobs[iSample]->Results[iModel] = res[iSample].item().toFloat();
				}
			}

			// send job to the next stage
			for (int i = 0; i < iGravelBatch; i++) {
				while (QDone.Size() >= MaxQDone) {
					os::Sleep(50 * time::Millisecond);
				}
				QDone.Push(gravelJobs[i]);
			}

			iGravelBatch = 0;
		}
	}
}

void PhotoProcessor::UploadThread() {
	int64_t        nUploads  = 0;
	size_t         batchSize = 0;
	nlohmann::json batch;

	batch["ModelVersion"] = "1.0.0";

	http::Connection cx;

	while (!Finished) {
		QDone.SemaphoreObj().wait();
		auto job = QDone.PopTailR();
		if (Finished)
			break;

		if (job) {
			auto& photos       = batch["Photos"];
			auto& photo        = photos[job->PhotoURL];
			photo["road_type"] = RoadTypeToChar(job->RoadType);
			auto& severity     = photo["Severity"];
			for (size_t i = 0; i < Models.size(); i++)
				severity[Models[i].Name] = job->Results[i];
			batchSize++;
		}

		if (batchSize == UploadBatchSize || (job == nullptr && batchSize != 0)) {
			for (int attempt = 0; true; attempt++) {
				tsf::print("Upload #%v (%v, %v, %v, %v)\n", nUploads, QNotStarted.Size(), QDownloaded.Size(), QHaveRoadType.Size(), QDone.Size());
				auto req = http::Request::POST(PhotoProcessor::BaseUrl + "/api/ph/analysis");
				req.AddCookie("session", SessionCookie);
				req.Body = batch.dump();
				auto res = cx.Perform(req);
				if (res.Is200()) {
					batch["Photos"] = {};
					batchSize       = 0;
					nUploads++;
					break;
				}
				tsf::print("Upload failed (%v)\n", res.ToError().Message());
				if (attempt == MaxUploadAttempts) {
					tsf::print("Giving up and aborting\n");
					Finished = true;
					break;
				}
				os::Sleep((1 << attempt) * time::Second);
			}
		}
	}
}

Error PhotoProcessor::LoadModels() {
	Models.clear();
	Models.resize(3);
	Models[0].Name = "gravel_base_stones";
	Models[1].Name = "gravel_elevation";
	Models[2].Name = "gravel_undulations";

	// launch from cmd line, dev time
	string dir = "models";

	// launch from debugger
	if (!os::PathExists(dir))
		dir = "../../models";

	// docker
	if (!os::PathExists(dir))
		dir = "/deploy/models";

	if (!os::PathExists(dir))
		return Error("Unable to find models directory");

	try {
		MRoadType = torch::jit::load(path::Join(dir, "road_type.tm").c_str());
		for (auto& m : Models)
			m.Model = torch::jit::load(path::Join(dir, m.Name + ".tm").c_str());
	} catch (const exception& e) {
		return Error(e.what());
	}
	return Error();
}

void PhotoProcessor::DumpPhotos(std::vector<PhotoJob*> jobs) {
	string vizDir = "/home/ben/viz";
	for (auto j : jobs) {
		string fn = j->PhotoURL;
		fn        = strings::Replace(fn, ":", "-");
		fn        = strings::Replace(fn, "/", "-");
		fn        = path::Join(vizDir, fn);
		TensorToImg(j->RGB_1000_256).SaveJpeg(fn, 90);
	}
}

} // namespace roadproc
} // namespace imqs