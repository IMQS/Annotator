#pragma once

#include "PhotoJob.h"

namespace imqs {
namespace roadproc {

// ImageCropParams describes how we go from a 4000x3000 GoPro image to an image that we will feed the NN
struct ImageCropParams {
	int TargetWidth   = 0; // Image width required by the model(s). This alone defines the amount by which we scale the source imagery.
	int TargetHeight  = 0; // Image height required by the model(s)
	int BottomDiscard = 0; // Number of pixels from original 4000x3000 image that we discard.
};

// Information describing a model, such as the category labels
struct ModelMeta {
	std::string                  Version; // Version of the Neural Network. There is also the version of the C++ code, which is appended to this NN version.
	ohash::map<std::string, int> NameToCategory;
	std::vector<std::string>     CategoryToName;

	size_t NumCategories() const { return CategoryToName.size(); }

	Error LoadJson(const std::string& jStr);
};

// Base class of an analysis model
class AnalysisModel {
public:
	int                        BatchSize = 8;
	ImageCropParams            CropParams;                   // The shape desired
	torch::jit::script::Module Model;                        // The model
	ModelMeta                  Meta;                         // Category labels, etc
	std::string                ModelName;                    // eg "tar_defects", which will cause us to look for models/tar_defects.tm and models/tar_defects.json. This name goes into the DB.
	std::string                PostNNModelVersion = "1.0.0"; // Version of the C++ code that is running after the Neural Network

	// Run on input of shape BCHW, where B = BatchSize, C = 3, H = CropParams.TargetHeight, W = CropParams.TargetWidth
	virtual Error Run(std::mutex& gpuLock, const torch::Tensor& input, const std::vector<PhotoJob*>& output) = 0;

	// Wrapper around static Load()
	Error Load(std::string baseFilename);

	// Helper function to load a torch model from a ".tm" file, and the metadata from an associated ".json" file.
	// baseFilename does not include the extension.
	static Error Load(std::string baseFilename, torch::jit::script::Module& model, ModelMeta& meta);

	void SegmentationSummaryToJSON(const gfx::Image& result, nlohmann::json& jcount) const;
};

} // namespace roadproc
} // namespace imqs