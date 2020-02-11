#pragma once

#include "PhotoJob.h"
#include "AnalysisModel.h"
#include "RoadType.h"
#include "TarDefects.h"
#include "CloudStorage.h"

namespace imqs {
namespace roadproc {

struct PhotoModel {
	std::string                Name; // This is the name of the model file. For example, "gravel_undulations"
	torch::jit::script::Module Model;
};

// Road processor
//
// This was originally written to process the extremely simple models for umhlathuze which operate on the entire image,
// and emit just a single number for a dimension, such as "gravel quality".
//
// Later, I extended it to be able to operate on the semantic segmentation model that emits tar defects.
//
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
	int                 NumFetchThreads      = 8; // Threads that download, decode, and create tensor
	int                 NumRoadTypeThreads   = 1;
	int                 NumAssessmentThreads = 2;
	int                 NumUploadThreads     = 4;
	bool                EnableRoadType       = false;   // This isn't necessary for our tar_defects model
	bool                RedoAll              = false;   // Rerun analysis on all photos. If false, then only perform analysis on photos that have not yet been processed.
	AnalysisModel*      Model                = nullptr; // The one and only analysis model. It wouldn't be hard to have a few models here, instead of just one.
	std::string         BaseUrl;                        // URL where 'console' DB service is running (default from command line args is http://roads.imqs.co.za)
	std::atomic<bool>   Finished;                       // Toggled at the end of RunInternal(), after all queues have drained
	std::atomic<size_t> TotalUploaded;                  // Total number of photos that have been uploaded since RunInternal() started
	std::atomic<size_t> TotalPhotos;                    // Total number of photos that are going to be processed
	time::Time          StartTime;                      // Time when RunInternal() started
	CloudStorageDetails CloudStorage;

	PhotoProcessor();

	static int Run(argparse::Args& args);

	Error RunInternal(std::string username, std::string password, std::string client, std::string prefix, std::string cloudStorageAuthFile);

private:
	torch::jit::script::Module MRoadType;     // This is a special model, because the others depend on it
	std::string                SessionCookie; // Cookie on roads.imqs.co.za
	std::mutex                 GPULock;       // Keep memory predictable by only running one model at a time
	std::atomic<int>           BusyLockCounter;

	//std::vector<PhotoModel>    Models;

	TQueue<PhotoJob*> QNotStarted;              // Jobs that have not been started yet
	TQueue<PhotoJob*> QDownloaded;              // Jobs that have been downloaded, and decoded into a torch Tensor
	TQueue<PhotoJob*> QHaveRoadType;            // Jobs that have had the RoadType model run on them, so we know if they're tar/gravel/etc
	TQueue<PhotoJob*> QDone;                    // Jobs that have had all applicable assessment evaluations run on them
	int               MaxQDownloaded      = 64; // Max number of jobs in the Downloaded queue
	int               MaxQHaveRoadType    = 64; // Max number of jobs in the HaveRoadType queue
	int               MaxQDone            = 64; // Max number of jobs in the Done queue
	int               RoadTypeBatchSize   = 8;  // GPU batch size for road type model
	int               UploadBatchSize     = 8;  // This isn't a GPU batch - this is a JSON batch for sending results to HTTP
	int               MaxDownloadAttempts = 5;  // Max download attempts before giving up. We continue trying others
	int               MaxUploadAttempts   = 5;  // Max upload attempts before giving up. If we fail, then we abort the entire process.

	// Old dead code
	//int               GravelQualityBatchSize = 8;  // GPU batch size for all of the gravel road quality models
	//void  AssessmentThread_Gravel();

	void        FetchThread();
	void        RoadTypeThread();
	void        AssessmentThread();
	void        UploadThread();
	Error       LoadModels();
	void        PushToQueue(TQueue<PhotoJob*>& queue, int maxQueueSize, PhotoJob* job);
	std::string CombinedModelVersion() const;
	bool        IsQueueDrained();

	static void DumpPhotos(std::vector<PhotoJob*> jobs);
};

} // namespace roadproc
} // namespace imqs