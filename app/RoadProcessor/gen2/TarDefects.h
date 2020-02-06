#pragma once

#include "AnalysisModel.h"

namespace imqs {
namespace roadproc {

class TarDefectsModel : public AnalysisModel {
public:
	TarDefectsModel();
	Error Run(std::mutex& gpuLock, const torch::Tensor& input, const std::vector<PhotoJob*>& output) override;

	void DrawDebugImage(PhotoJob* job) const;
};

} // namespace roadproc
} // namespace imqs