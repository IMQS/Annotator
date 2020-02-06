#pragma once

namespace imqs {
namespace roadproc {

enum class ImgNormalizeMode {
	UnityZeroMean, // -0.5 .. +0.5
};

std::string SmallTensorToString(const at::Tensor& t, const std::string& formatStr);
std::string SizeToString(const c10::IntArrayRef& size);
std::string SizeToString(const at::Tensor& t);

// Returns HWC (RGB)
at::Tensor ImgToTensor(const gfx::Image& img, ImgNormalizeMode mode = ImgNormalizeMode::UnityZeroMean);
// Input is an HWC (RGB) image
gfx::Image TensorToImg(at::Tensor t);

} // namespace roadproc
} // namespace imqs