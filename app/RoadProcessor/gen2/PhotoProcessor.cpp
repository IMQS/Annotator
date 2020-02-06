#include "pch.h"
#include "PhotoProcessor.h"
#include "TorchUtils.h"
#include "../Globals.h"

using namespace std;

namespace imqs {
namespace roadproc {

static Error FindPhotosOnServer(std::string baseUrl, std::string client, std::string prefix, std::vector<std::string>& photoUrls) {
	if (baseUrl == "")
		return Error("FindPhotosOnServer: BaseURL is empty");

	// the LIKE statement on the API side looks like this:
	//   like := "gs://roadphoto.imqs.co.za/" + client + "/" + prefix + "%%"

	// eg curl http://roads.imqs.co.za/api/ph/photos?client=za.nl.um.--&prefix=2019/2019-02-27/164GOPRO

	string                     api = baseUrl + "/api/ph/photos?";
	ohash::map<string, string> q   = {
        {"client", client},
        {"prefix", prefix},
    };
	string fullUrl = api + url::Encode(q);

	auto resp = http::Client::Get(fullUrl);
	if (!resp.Is200()) {
		// This is often the first error that you'll see from this system, so we add extra detail
		return Error::Fmt("Error fetching photos from %v: %v", fullUrl, resp.ToError().Message());
	}

	nlohmann::json jroot;
	auto           err = nj::ParseString(resp.Body, jroot);
	if (!err.OK())
		return err;
	if (!jroot.is_array())
		return Error("JSON response from list_photos is not an array");
	for (size_t i = 0; i < jroot.size(); i++) {
		const auto& j = jroot[i];
		if (j.find("Photo") == j.end()) {
			tsf::print("Warning: JSON record has no 'Photo' member\n");
			continue;
		}
		const auto& jPhoto = j["Photo"];
		photoUrls.push_back(nj::GetString(jPhoto, "URL"));
	}

	return Error();
}

PhotoProcessor::PhotoProcessor() {
	QNotStarted.Initialize(true);
	QDownloaded.Initialize(true);
	QHaveRoadType.Initialize(true);
	QDone.Initialize(true);

	// Setup for tar_defects
	Model = new TarDefectsModel();

	bool debugMode = false;
	if (debugMode) {
		// reduce batch sizes for debugging
		NumFetchThreads   = 1;
		MaxQDownloaded    = 2;
		RoadTypeBatchSize = 2;
		//GravelQualityBatchSize = 2;
		UploadBatchSize = 2;
	}
}

int PhotoProcessor::Run(argparse::Args& args) {
	auto           username = args.Params[0];
	auto           password = args.Params[1];
	auto           client   = args.Params[2];
	auto           prefix   = args.Params[3];
	PhotoProcessor pp;
	pp.BaseUrl = args.Get("server");
	auto err   = pp.RunInternal(username, password, client, prefix, args.GetInt("resume"));
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
	auto             err = global::Login(cx, BaseUrl, username, password, SessionCookie);
	if (!err.OK())
		return err;

	tsf::print("Loading models\n");
	err = LoadModels();
	if (!err.OK())
		return err;

	tsf::print("Preparing photo queue\n");
	vector<string> photoUrls;
	err = FindPhotosOnServer(BaseUrl, client, prefix, photoUrls);
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
	tsf::print("Finished\n");
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
	http::Connection      cx;
	gfx::ImageIO          imgIO;
	avir::CImageResizer<> resizer(8, 0, avir::CImageResizerParamsDef());

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

		// We can get TurboJPEG to perform some downsampling for us, which is very cheap.
		// I've tested it on a single image, comparing to using AVIR to resize it from the original 4000x3000, and I can't
		// tell the difference between using JPEG 1/2 res and full res (ie both of these use AVIR to perform the final
		// downsampling). So we use the JPEG 1/2 or 1/4 res, because it's way more efficient.
		int   jpegFactor = 1;
		auto  cropParams = Model->CropParams;
		float resFactor  = (float) 4000 / (float) cropParams.TargetWidth;
		if (resFactor >= 4)
			jpegFactor = 4;
		else if (resFactor >= 2)
			jpegFactor = 2;

		int   width  = 0;
		int   height = 0;
		void* rgba   = nullptr;
		auto  err    = imgIO.LoadJpegScaled(resp.Body.data(), resp.Body.size(), jpegFactor, width, height, rgba, TJPF_RGBA);
		if (!err.OK()) {
			tsf::print("Failed to decode %v: %v\n", err.Message());
			delete job;
			continue;
		}
		if (width != 4000 / jpegFactor || height != 3000 / jpegFactor) {
			tsf::print("Expected input image to be 4000 x 3000, but image is %v x %v\n", width * jpegFactor, height * jpegFactor);
			delete job;
			continue;
		}
		gfx::Image raw(gfx::ImageFormat::RGBA, gfx::Image::ConstructWindow, width * 4, rgba, width, height);
		//raw.SavePng("/home/ben/viz/raw.png", true, 1);

		if (width != cropParams.TargetWidth) {
			resFactor          = (float) width / (float) cropParams.TargetWidth;
			uint8_t* src       = (uint8_t*) rgba;
			int      srcStride = width * 4;
			int      srcHeight = int((float) cropParams.TargetHeight * resFactor);
			src += height * srcStride;                                     // move to bottom of image
			src -= int(cropParams.BottomDiscard / jpegFactor) * srcStride; // discard bottom pixels
			src -= srcHeight * srcStride;                                  // move up, to produce the actual number of input pixels
			uint8_t*                resized = (uint8_t*) imqs_malloc_or_die(cropParams.TargetWidth * cropParams.TargetHeight * 4);
			avir::CImageResizerVars p;
			p.UseSRGBGamma = true;
			resizer.resizeImage(src, width, srcHeight, width * 4, resized, cropParams.TargetWidth, cropParams.TargetHeight, 4, 0, &p);
			free(rgba);
			rgba   = resized;
			width  = cropParams.TargetWidth;
			height = cropParams.TargetHeight;
		}

		gfx::Image ready(gfx::ImageFormat::RGBA, gfx::Image::ConstructTakeOwnership, width * 4, rgba, width, height);
		//gfx::Image quarter(gfx::ImageFormat::RGBA, gfx::Image::ConstructTakeOwnership, width * 4, rgba, width, height);
		//auto       quarter = half.HalfSizeSIMD();
		// crop, preserving only the bottom 1/3 of the frame
		//auto halfCrop    = half.Window(0, half.Height - 500, half.Width, 500);
		//auto quarterCrop = quarter.Window(0, quarter.Height - 256, quarter.Width, 256);
		//ready.SavePng("/home/ben/viz/scaled-2.png", true, 1);
		job->RGB = ImgToTensor(ready);

		tsf::print("Decoded %4d %v\n", job->InternalID, job->PhotoURL);

		PushToQueue(QDownloaded, MaxQDownloaded, job);
	}
}

void PhotoProcessor::RoadTypeThread() {
	http::Connection cx;
	gfx::ImageIO     imgIO;

	torch::Tensor     batch;
	vector<PhotoJob*> batchJobs; // the jobs inside this batch

	if (EnableRoadType)
		batch = torch::empty({RoadTypeBatchSize, 3, 256, 1000});

	while (!Finished) {
		QDownloaded.SemaphoreObj().wait();
		auto job = QDownloaded.PopTailR();
		if (Finished)
			break;

		// null job means a pipeline flush

		if (job && EnableRoadType) {
			auto t                  = job->RGB.permute({2, 0, 1}); // HWC -> CHW
			batch[batchJobs.size()] = t;
			batchJobs.push_back(job);
		}

		if (!EnableRoadType) {
			QHaveRoadType.Push(job);
			continue;
		}

		if ((!job || batchJobs.size() == RoadTypeBatchSize) && batchJobs.size() != 0) {
			torch::NoGradGuard nograd;
			GPULock.lock();
			auto res = MRoadType.forward({batch.cuda()}).toTensor().cpu();
			GPULock.unlock();
			auto amax = torch::argmax(res, 1);
			// res shape is [4,3] (BC)
			// amax shape is [4]
			//DumpPhotos(batchJobs);
			//tsf::print("res shape: %v\n", SizeToString(res.sizes()));
			//tsf::print("amax shape: %v\n", SizeToString(amax.sizes()));
			for (size_t i = 0; i < batchJobs.size(); i++) {
				batchJobs[i]->RoadType = (RoadTypeModel::Types) amax[i].item().toInt();
				//tsf::print("Road Type: %v\n", (int) batchJobs[i]->RoadType);
				PushToQueue(QHaveRoadType, MaxQHaveRoadType, batchJobs[i]);
			}
			batchJobs.clear();
		}
	}
}

/*
void PhotoProcessor::AssessmentThread_Gravel() {
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
			auto t                    = job->RGB.permute({2, 0, 1}); // HWC -> CHW
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
				results.push_back(m.Model.forward({batchCuda}).toTensor());
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
			for (int i = 0; i < iGravelBatch; i++)
				PushToQueue(QDone, MaxQDone, gravelJobs[i]);

			iGravelBatch = 0;
		}
	}
}
*/

void PhotoProcessor::AssessmentThread() {
	torch::Tensor     batch = torch::empty({Model->BatchSize, 3, Model->CropParams.TargetHeight, Model->CropParams.TargetWidth});
	vector<PhotoJob*> batchJobs; // the jobs inside this batch

	while (!Finished) {
		QHaveRoadType.SemaphoreObj().wait();
		auto job = QHaveRoadType.PopTailR();
		if (Finished)
			break;

		// null job means a pipeline flush

		if (job) {
			auto t                  = job->RGB.permute({2, 0, 1}); // HWC -> CHW
			batch[batchJobs.size()] = t;
			batchJobs.push_back(job);
		}

		if (batchJobs.size() == Model->BatchSize || (job == nullptr && batchJobs.size() != 0)) {
			torch::NoGradGuard nograd;
			auto               err = Model->Run(GPULock, batch, batchJobs);
			if (!err.OK())
				tsf::print("Error running model: %v\n", err.Message());

			// send jobs to the next stage
			for (size_t i = 0; i < batchJobs.size(); i++)
				PushToQueue(QDone, MaxQDone, batchJobs[i]);
			batchJobs.clear();
		}

		if (!job) {
			// Push pipeline flush
			PushToQueue(QDone, MaxQDone, nullptr);
		}
	}
}

void PhotoProcessor::UploadThread() {
	int64_t        nUploads  = 0;
	size_t         batchSize = 0;
	nlohmann::json batch;

	// Here is a little example of PNG compression rates for a small tar_defects analysis image:
	// Bytes   Res  Level
	// 25696 152x56-0.png
	//   735 152x56-1.png
	//   513 152x56-5.png
	//   439 152x56-9.png

	auto combinedModelVersion = Model->Meta.Version + "-" + Model->PostNNModelVersion;
	batch["ModelVersion"]     = combinedModelVersion;

	http::Connection cx;

	while (!Finished) {
		QDone.SemaphoreObj().wait();
		auto job = QDone.PopTailR();
		if (Finished)
			break;

		if (job) {
			auto& jphotos = batch["Photos"];
			auto& jphoto  = jphotos[job->PhotoURL];
			// If we were to run multiple models, then we could conceptually merge the JSON here.
			// Alternatively, we could have multiple fields in the DB, one field for each model.
			// So many ways.. such arbitrary decisions!
			auto& jmodels     = jphoto["Models"];
			auto& jmodel      = jmodels[Model->ModelName];
			jmodel["Version"] = combinedModelVersion;
			jmodel["Count"]   = job->DBOutput;
			//tsf::print("Upload thread got %v\n", job->DBOutput.dump());
			// This was the old code for the gravel road stuff
			// photo["road_type"] = RoadTypeToChar(job->RoadType);
			// auto& severity     = photo["Severity"];
			// for (size_t i = 0; i < Models.size(); i++)
			// 	severity[Models[i].Name] = job->Results[i];
			batchSize++;
		}

		if (batchSize == UploadBatchSize || (job == nullptr && batchSize != 0)) {
			for (int attempt = 0; true; attempt++) {
				tsf::print("Upload #%v of %v photos. Queues: (%v, %v, %v, %v)\n", nUploads, batchSize, QNotStarted.Size(), QDownloaded.Size(), QHaveRoadType.Size(), QDone.Size());
				auto req = http::Request::POST(PhotoProcessor::BaseUrl + "/api/ph/analysis");
				req.AddCookie("session", SessionCookie);
				tsf::print("Uploading %v\n", batch.dump());
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

	auto err = Model->Load(path::Join(dir, Model->ModelName));
	if (!err.OK())
		return err;

	/*
	Models.clear();
	Models.resize(3);
	Models[0].Name = "gravel_base_stones";
	Models[1].Name = "gravel_elevation";
	Models[2].Name = "gravel_undulations";

	try {
		MRoadType = torch::jit::load(path::Join(dir, "road_type.tm").c_str());
		for (auto& m : Models)
			m.Model = torch::jit::load(path::Join(dir, m.Name + ".tm").c_str());
	} catch (const exception& e) {
		return Error(e.what());
	}
	*/
	return Error();
}

void PhotoProcessor::PushToQueue(TQueue<PhotoJob*>& queue, int maxQueueSize, PhotoJob* job) {
	while (queue.Size() >= maxQueueSize) {
		os::Sleep(50 * time::Millisecond);
	}
	queue.Push(job);
}

void PhotoProcessor::DumpPhotos(std::vector<PhotoJob*> jobs) {
	string vizDir = "/home/ben/viz";
	for (auto j : jobs) {
		string fn = j->PhotoURL;
		fn        = strings::Replace(fn, ":", "-");
		fn        = strings::Replace(fn, "/", "-");
		fn        = path::Join(vizDir, fn);
		TensorToImg(j->RGB).SaveJpeg(fn, 90);
	}
}

} // namespace roadproc
} // namespace imqs