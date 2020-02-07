#pragma once

#include "RoadType.h"

namespace imqs {
namespace roadproc {

// This holds all the state of a photo, as it passes through the various analysis stages
struct PhotoJob {
	int64_t                InternalID = 0;                        // Just an arbitrary counter, to measure progress, and aid debugging. NOT the same as the server-side ID.
	std::string            PhotoURL;                              // eg gs://roadphoto.imqs.co.za/za.wc.st.--/2020/01-07/G0046237.JPG
	torch::Tensor          RGB;                                   // RGB image
	RoadTypeModel::Types   RoadType = RoadTypeModel::Types::NONE; // One of PhotoProcessor::RoadTypes
	ohash::map<int, float> WholeImageResults;                     // Results of the various (OLD) whole-image quality models. Key is the model index, value is the assessment (typically 1..5)
	gfx::Image             AnalysisImage;                         // A lum8 image - values are the category values from the model
	nlohmann::json         DBOutput;                              // This goes into the DB record (in the 'Console' DB in the cloud). Into field ph_analysis.analysis.

	std::string CloudStorageBucket() const; // eg roadphoto.imqs.co.za
	std::string CloudStoragePath() const;   // eg za.wc.st.--/2020/01-07/G0046237.JPG
};

} // namespace roadproc
} // namespace imqs