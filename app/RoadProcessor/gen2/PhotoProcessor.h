#pragma once

namespace imqs {
namespace roadproc {

enum class RoadTypes {
	// PyTorch trainer sorts the classes alphabetically, so we just do the same here,
	// and we don't need to worry about anything further.
	Gravel,
	Ignore,
	JeepTrack,
	Tar,
	NONE,
};

char RoadTypeToChar(RoadTypes t);

// This holds all the state of a photo, as it passes through the various analysis stages
struct PhotoJob {
	int64_t                InternalID = 0; // Just an arbitrary counter, to measure progress, and aid debugging. NOT the same as the server-side ID.
	std::string            PhotoURL;
	torch::Tensor          RGB_1000_256;               // RGB x 1000 x 256 (4x downscaled, then bottom 1/3 crop)
	RoadTypes              RoadType = RoadTypes::NONE; // One of PhotoProcessor::RoadTypes
	ohash::map<int, float> Results;                    // Results of the various quality models. Key is the model index, value is the assessment (typically 1..5)
};

struct PhotoModel {
	std::string                                 Name; // This is the name of the model file. For example, "gravel_undulations"
	std::shared_ptr<torch::jit::script::Module> Model;
};

// Road processor
// We run 4 thread pools. Each of them is a stage in a pipeline.
// In other words, stage 1 must complete before stage 2 runs, and stage 2 must complete
// before stage 3 runs, etc.
// Most of the stages only have a single CPU thread, because most of the work is being done
// by the GPU.
// There is a queue in betweeen each stage, and one queue for the final result.
// We tune the maximum sizes of the queues, and the number of threads, in order to try and
// achieve maximum utilization of the GPU and CPU.
// In order to force a pipeline flush, we send a null job down it. This is always used at the
// end of the dataset.
class PhotoProcessor {
public:
	// We should only need a single thread for each neural network phase, because a single thread can
	// load up a bunch of sample in a batch
	int NumFetchThreads      = 8; // Threads that download, decode, and create tensor
	int NumRoadTypeThreads   = 1;
	int NumAssessmentThreads = 1;
	int NumUploadThreads     = 1;

	static std::string BaseUrl;

	std::atomic<bool> Finished;

	PhotoProcessor();

	static int Run(argparse::Args& args);

	Error RunInternal(std::string username, std::string password, std::string client, std::string prefix, int startAt = 0);

private:
	std::shared_ptr<torch::jit::script::Module> MRoadType; // This is a special model, because the others depend on it
	std::vector<PhotoModel>                     Models;
	std::string                                 SessionCookie; // Cookie on roads.imqs.co.za
	std::mutex                                  GPULock;       // Keep memory predictable by only running one model at a time

	TQueue<PhotoJob*> QNotStarted;                 // Jobs that have not been started yet
	TQueue<PhotoJob*> QDownloaded;                 // Jobs that have been downloaded, and decoded into a Tensor, and uploaded into CUDA memory
	TQueue<PhotoJob*> QHaveRoadType;               // Jobs that have had the RoadType model run on them, so we know if they're tar/gravel/etc
	TQueue<PhotoJob*> QDone;                       // Jobs that have had all applicable assessment evaluations run on them
	int               MaxQDownloaded         = 64; // Max number of jobs in the Downloaded queue
	int               MaxQHaveRoadType       = 64; // Max number of jobs in the HaveRoadType queue
	int               MaxQDone               = 64; // Max number of jobs in the Done queue
	int               RoadTypeBatchSize      = 8;  // GPU batch size for road type model
	int               GravelQualityBatchSize = 8;  // GPU batch size for all of the gravel road quality models
	int               UploadBatchSize        = 8;  // This isn't a GPU batch - this is a JSON batch for HTTP sending
	int               MaxDownloadAttempts    = 5;  // Max download attempts before giving up. We continue trying others
	int               MaxUploadAttempts      = 5;  // Max upload attempts before giving up. If we fail, then we abort the entire process.

	void FetchThread();
	void RoadTypeThread();
	void AssessmentThread();
	void UploadThread();

	Error LoadModels();

	static void DumpPhotos(std::vector<PhotoJob*> jobs);
};

} // namespace roadproc
} // namespace imqs