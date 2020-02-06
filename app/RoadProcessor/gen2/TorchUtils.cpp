#include "pch.h"
#include "TorchUtils.h"

using namespace std;

namespace imqs {
namespace roadproc {

std::string SmallTensorToString(const at::Tensor& t, const std::string& formatStr) {
	string s;
	if (t.dim() == 3) {
		auto p = t.accessor<float, 3>();
		for (int y = 0; y < t.size(0); y++) {
			s += "[ ";
			for (int x = 0; x < t.size(1); x++) {
				s += tsf::fmt(formatStr.c_str(), p[y][x][0]);
				s += " ";
			}
			s += "]\n";
		}
	} else if (t.dim() == 1) {
		auto p = t.accessor<float, 1>();
		s += "[ ";
		for (int x = 0; x < t.size(0); x++) {
			s += tsf::fmt(formatStr.c_str(), p[x]);
			s += " ";
		}
		s += "]";
	} else {
		IMQS_ASSERT(t.dim() == 3);
	}
	return s;
}

std::string SizeToString(const c10::IntArrayRef& size) {
	string s = "[";
	for (size_t i = 0; i < size.size(); i++)
		s += tsf::fmt("%d,", size[i]);
	if (size.size() != 0)
		s.erase(s.end() - 1);
	s += "]";
	return s;
}

std::string SizeToString(const at::Tensor& t) {
	return SizeToString(t.sizes());
}

at::Tensor ImgToTensor(const gfx::Image& img, ImgNormalizeMode mode) {
	IMQS_ASSERT(img.NumChannels() == 4);
	IMQS_ASSERT(img.Stride == img.NumChannels() * img.Width);
	auto t = torch::from_blob(img.Data, {img.Height, img.Width, img.NumChannels()}, torch::kU8);
	t      = t.slice(2, 0, 3); // remove alpha
	t      = t.toType(torch::kF32);
	if (mode == ImgNormalizeMode::UnityZeroMean) {
		t.mul_(1.0f / 255.0f);
		t.add_(-0.5f);

		//auto buf = t.accessor<float, 3>();
		//tsf::print("%f %f %f\n", buf[100][100][0], buf[100][100][1], buf[100][100][2]);
		//auto vmin = torch::min(t).item().toFloat();
		//auto vmax = torch::max(t).item().toFloat();
		//tsf::print("%f %f\n", vmin, vmax);
	} else {
		IMQS_DIE();
	}
	return t;
}

gfx::Image TensorToImg(at::Tensor t) {
	t = t.clone();
	t = t.cpu();
	if (t.dim() == 4 && t.size(0) == 1) {
		// get rid of batch channel
		t = t.squeeze(0);
	}
	IMQS_ASSERT(t.dim() == 3);
	int height = t.size(0);
	int width  = t.size(1);
	int chan   = t.size(2);
	IMQS_ASSERT(chan == 3);
	t.add_(0.5f);
	t.mul_(255.0f);
	t = t.toType(torch::kU8);
	gfx::Image img;
	img.Alloc(gfx::ImageFormat::RGBA, width, height);
	for (int y = 0; y < height; y++) {
		uint8_t* dst = img.Line(y);
		uint8_t* src = t.accessor<uint8_t, 3>().data() + y * width * chan;
		for (int x = 0; x < width; x++) {
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = 255;
			dst += 4;
			src += 3;
		}
	}
	return img;
}

} // namespace roadproc
} // namespace imqs