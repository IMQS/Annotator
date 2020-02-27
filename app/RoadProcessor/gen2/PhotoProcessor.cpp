#include "pch.h"
#include "PhotoProcessor.h"
#include "TorchUtils.h"
#include "CloudStorage.h"
#include "../Globals.h"

using namespace std;

namespace imqs {
namespace roadproc {

// A photo and some information about the analysis that has already been run on it
struct PhotoAndAnalysis {
	std::string PhotoUrl;

	// The version of the model that you're interested in.
	// For example, if modelName is "tar_defects", then ModelVersion could be "tar-1.2.0-1.0.1".
	// This means that when the analysis was last run for the tar_defects model, then the version
	// of the model that performed that analysis was "tar-1.2.0-1.0.1".
	// If ModelVersion is empty, then the model in question has not yet been run on this photo.
	std::string ModelVersion;
};

struct BusyLock {
	std::atomic<int>* Counter;
	BusyLock(std::atomic<int>& counter) {
		Counter = &counter;
		*Counter += 1;
	}
	~BusyLock() {
		*Counter -= 1;
	}
};

static Error FindPhotosOnServer(std::string baseUrl, std::string client, std::string prefix, std::string modelName, std::vector<PhotoAndAnalysis>& photos) {
	if (baseUrl == "")
		return Error("FindPhotosOnServer: BaseURL is empty");

	// the LIKE statement on the API side looks like this:
	//   like := "gs://roadphoto.imqs.co.za/" + client + "/" + prefix + "%%"

	// eg curl https://roads.imqs.co.za/api/ph/photos?client=za.nl.um.--&prefix=2019/2019-02-27/164GOPRO

	string                     api = baseUrl + "/api/ph/photos?";
	ohash::map<string, string> q   = {
        {"client", client},
        {"prefix", prefix},
        {"include_analysis", "1"},
    };
	string fullUrl = api + url::Encode(q);

	auto resp = http::Client::Get(fullUrl);
	if (!resp.Is200()) {
		// This is often the first error that you'll see from this system, so we add extra detail
		return Error::Fmt("Error fetching photos from %v: %v", fullUrl, resp.ToError().Message());
	}
	// Example analysis JSON:
	// {"Models": {"tar_defects": {"Count": {"curb": 707, "none": 7445, "brick": 3, "manhole": 181, "not-road": 175, "long-crack": 1}, "Version": "1.0.0-1.0.0"}}}

	nlohmann::json jroot;
	auto           err = nj::ParseString(resp.Body, jroot);
	if (!err.OK())
		return err;
	if (!jroot.is_array())
		return Error("JSON response from list_photos is not an array");
	for (size_t i = 0; i < jroot.size(); i++) {
		const auto& j = jroot[i];
		if (!nj::Has(j, "Photo")) {
			tsf::print("Warning: JSON record has no 'Photo' member\n");
			continue;
		}
		const auto&      jPhoto = j["Photo"];
		PhotoAndAnalysis p;
		p.PhotoUrl = nj::GetString(jPhoto, "URL");
		if (modelName != "") {
			if (nj::Has(j, "Analysis")) {
				const auto& jAnalysis = j["Analysis"];
				if (nj::Has(jAnalysis, "Models") && nj::Has(jAnalysis["Models"], modelName.c_str())) {
					const auto& jModel = nj::GetObject(jAnalysis["Models"], modelName.c_str());
					p.ModelVersion     = nj::GetString(jModel, "Version");
				}
			}
		}
		photos.push_back(move(p));
	}

	return Error();
}

PhotoProcessor::PhotoProcessor() {
	QNotStarted.Initialize(true);
	QDownloaded.Initialize(true);
	QHaveRoadType.Initialize(true);
	QDone.Initialize(true);
	TotalUploaded   = 0;
	BusyLockCounter = 0;

	CloudStorage.Bucket    = "roadphoto.imqs.co.za";
	CloudStorage.Platform  = "gcs";
	CloudStorage.LastLogin = time::Now() - 100 * time::Hour;

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
	auto           cloudKey = args.Params[4];
	PhotoProcessor pp;
	pp.BaseUrl = args.Get("server");
	pp.RedoAll = args.Has("all");
	auto err   = pp.RunInternal(username, password, client, prefix, cloudKey);
	if (!err.OK()) {
		tsf::print("Error: %v\n", err.Message());
		return 1;
	}
	return 0;
}

Error PhotoProcessor::RunInternal(string username, string password, string client, string prefix, std::string cloudStorageAuthFile) {
	Finished      = false;
	StartTime     = time::Now();
	TotalUploaded = 0;

	tsf::print("Logging in to 'Console' service\n");
	http::Connection cx;
	auto             err = global::Login(cx, BaseUrl, username, password, SessionCookie);
	if (!err.OK())
		return err;

	tsf::print("Logging in to Cloud Storage system\n");

	CloudStorage.AuthFilename = cloudStorageAuthFile;
	err                       = CloudLogin();
	if (!err.OK())
		return err;

	tsf::print("Loading models\n");
	err = LoadModels();
	if (!err.OK())
		return err;

	tsf::print("Publishing model details\n");
	err = PublishModels();
	if (!err.OK())
		return err;

	tsf::print("Preparing photo queue\n");
	vector<PhotoAndAnalysis> photos;
	err = FindPhotosOnServer(BaseUrl, client, prefix, Model->ModelName, photos);
	if (!err.OK())
		return err;

	// Remove photos that have already been processed
	vector<string> remainingPhotos;
	string         modelVersion = Model->CombinedVersion();
	for (const auto& p : photos) {
		if (RedoAll || p.ModelVersion != modelVersion)
			remainingPhotos.push_back(p.PhotoUrl);
	}

	tsf::print("Found %v/%v photos to process\n", remainingPhotos.size(), photos.size());
	TotalPhotos = remainingPhotos.size();

	int64_t id = 0;
	for (const auto& url : remainingPhotos) {
		auto j        = new PhotoJob();
		j->PhotoURL   = url;
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

	// wait for all queues to drain
	while (QNotStarted.Size() != 0 || QDownloaded.Size() != 0 || QHaveRoadType.Size() != 0 || QDone.Size() != 0 || BusyLockCounter != 0) {
		os::Sleep(50 * time::Millisecond);
	}
	tsf::print("Finished\n");
	// set the Finished flag, to signal all threads to exit
	Finished           = true;
	int maxThreadCount = 100;
	for (int i = 0; i < maxThreadCount; i++) {
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
		BusyLock busy(BusyLockCounter);
		auto     job = QNotStarted.PopTailR();
		if (Finished)
			break;

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
		BusyLock busy(BusyLockCounter);
		auto     job = QDownloaded.PopTailR();
		if (Finished)
			break;

		if (!EnableRoadType) {
			PushToQueue(QHaveRoadType, MaxQHaveRoadType, job);
			continue;
		}

		auto t                  = job->RGB.permute({2, 0, 1}); // HWC -> CHW
		batch[batchJobs.size()] = t;
		batchJobs.push_back(job);

		if (batchJobs.size() != 0 && (batchJobs.size() == RoadTypeBatchSize || IsQueueDrained())) {
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
		BusyLock busy(BusyLockCounter);
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

		if (iGravelBatch == GravelQualityBatchSize || IsQueueDrained() || (job == nullptr && iGravelBatch != 0)) {
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
		BusyLock busy(BusyLockCounter);
		auto     job = QHaveRoadType.PopTailR();
		if (Finished)
			break;

		auto t                  = job->RGB.permute({2, 0, 1}); // HWC -> CHW
		batch[batchJobs.size()] = t;
		batchJobs.push_back(job);

		if (batchJobs.size() != 0 && (batchJobs.size() == Model->BatchSize || IsQueueDrained())) {
			torch::NoGradGuard nograd;
			auto               err = Model->Run(GPULock, batch, batchJobs);
			if (!err.OK())
				tsf::print("Error running model: %v\n", err.Message());

			// send jobs to the next stage
			for (size_t i = 0; i < batchJobs.size(); i++)
				PushToQueue(QDone, MaxQDone, batchJobs[i]);
			batchJobs.clear();
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

	batch["ModelVersion"] = Model->CombinedVersion();

	http::Connection cx;

	while (!Finished) {
		QDone.SemaphoreObj().wait();
		BusyLock busy(BusyLockCounter);
		auto     job = QDone.PopTailR();
		if (Finished)
			break;

		Error err;
		for (int attempt = 0; attempt < 5; attempt++) {
			err = CloudLoginIfExpired();
			if (err.OK())
				break;
			tsf::print("Failed to login to cloud: %v\n", err.Message());
		}
		if (!err.OK()) {
			tsf::print("Giving up on cloud login, and aborting\n");
			Finished = true;
			break;
		}

		auto& jphotos = batch["Photos"];
		auto& jphoto  = jphotos[job->PhotoURL];
		// If we were to run multiple models, then we could conceptually merge the JSON here.
		// Alternatively, we could have multiple fields in the DB, one field for each model.
		// So many ways.. such arbitrary decisions!
		auto& jmodels     = jphoto["Models"];
		auto& jmodel      = jmodels[Model->ModelName];
		jmodel["Version"] = Model->CombinedVersion();
		jmodel["Count"]   = job->DBOutput;
		//tsf::print("Upload thread got %v\n", job->DBOutput.dump());
		// This was the old code for the gravel road stuff
		// photo["road_type"] = RoadTypeToChar(job->RoadType);
		// auto& severity     = photo["Severity"];
		// for (size_t i = 0; i < Models.size(); i++)
		// 	severity[Models[i].Name] = job->Results[i];

		string cloudPath = job->CloudStoragePath();

		string analysisPng;
		err = job->AnalysisImage.SavePngBuffer(analysisPng, false, 9);
		IMQS_ASSERT(err.OK());
		for (int attempt = 0; true; attempt++) {
			if (cloudPath.find('/') == 0) {
				cloudPath = cloudPath.substr(1);
			}
			string analysisPath = "analysis/" + Model->ModelName + "/" + cloudPath;
			err                 = UploadToCloudStorage(cx, CloudStorage, analysisPath, "image/png", analysisPng);
			if (err.OK())
				break;
			tsf::print("Upload %v to cloud storage failed: %v\n", analysisPath, err.Message());
			if (attempt == MaxUploadAttempts) {
				tsf::print("Giving up and aborting\n");
				Finished = true;
				break;
			}
			os::Sleep((1 << attempt) * time::Second);
		}
		batchSize++;

		if (batchSize != 0 && (batchSize == UploadBatchSize || IsQueueDrained())) {
			for (int attempt = 0; true; attempt++) {
				double photosPerSecond = ((double) TotalUploaded.load() + batchSize) / (time::Now() - StartTime).Seconds();
				auto   timeRemaining   = (((double) TotalPhotos - TotalUploaded) / photosPerSecond) * time::Second;
				tsf::print("Upload #%v of %v photos. Queues: (%v, %v, %v, %v). %.0f photos/minute. Remaining %v\n",
				           nUploads, batchSize, QNotStarted.Size(), QDownloaded.Size(), QHaveRoadType.Size(), QDone.Size(),
				           photosPerSecond * 60, timeRemaining.FormatTimeRemaining());
				auto req = http::Request::POST(PhotoProcessor::BaseUrl + "/api/ph/analysis");
				req.AddCookie("session", SessionCookie);
				tsf::print("Uploading %v\n", batch.dump());
				req.Body = batch.dump();
				auto res = cx.Perform(req);
				if (res.Is200()) {
					TotalUploaded += batchSize;
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

Error PhotoProcessor::PublishModels() {
	// Upload the model metadata to the 'console' service.
	// Metadata includes (and perhaps other things too):
	// * Version of the model
	// * Mapping from integer class to class name (eg class 0 = brick surface, class 1 = crack, class 2 = manhole, etc)
	string         fullVersion = Model->ModelName + "-" + Model->CombinedVersion();
	nlohmann::json meta;
	Model->Meta.SaveConsoleServerJson(meta);
	Model->CropParams.SaveConsoleServerJson(meta, 4000, 3000); // HACK - it would be better not to hardcode this.

	http::Connection cx;
	auto             req = http::Request::POST(PhotoProcessor::BaseUrl + "/api/ph/model/" + url::Encode(fullVersion));
	req.AddCookie("session", SessionCookie);
	tsf::print("Publishing model %v metadata\n", fullVersion);
	req.Body = meta.dump();
	auto res = cx.Perform(req);
	return res.ToError();
}

void PhotoProcessor::PushToQueue(TQueue<PhotoJob*>& queue, int maxQueueSize, PhotoJob* job) {
	while (queue.Size() >= maxQueueSize) {
		if (Finished) {
			// This is necessary when we get cancelled. For example, if the upload thread fails too many
			// times, then it will set Finished = true, and kill itself.
			break;
		}
		os::Sleep(50 * time::Millisecond);
	}
	queue.Push(job);
}

std::string PhotoProcessor::CombinedModelVersion() const {
	return Model->Meta.Version + "-" + Model->PostNNModelVersion;
}

bool PhotoProcessor::IsQueueDrained() {
	return QNotStarted.Size() == 0;
}

Error PhotoProcessor::CloudLoginIfExpired() {
	{
		// GCS token expiry is 1 hour, so 15 minute timeout is plenty
		lock_guard<mutex> lock(CloudStorageLock);
		if (time::Now() - CloudStorage.LastLogin < 15 * time::Minute)
			return Error();
	}
	return CloudLogin();
}

Error PhotoProcessor::CloudLogin() {
	lock_guard<mutex> lock(CloudStorageLock);
	auto              err = GCSLoginWithFile(CloudStorage.AuthFilename, CloudStorage.AuthToken);
	if (err.OK())
		CloudStorage.LastLogin = time::Now();
	return err;
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