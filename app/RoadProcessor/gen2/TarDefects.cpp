#include "pch.h"
#include "TarDefects.h"
#include "TorchUtils.h"

using namespace std;

namespace imqs {
namespace roadproc {

TarDefectsModel::TarDefectsModel() {
	BatchSize          = 1;
	ModelName          = "tar_defects";
	PostNNModelVersion = "1.0.0"; // Change this when the C++ code changes. The Neural Network model version lives inside Meta.Version
	// Sizes must be a multiple of 64 (I think!. Could be 32.. or 16.. or 8)
	CropParams.TargetWidth   = 1216;
	CropParams.TargetHeight  = 448;
	CropParams.BottomDiscard = 150; // in case part of the car is visible in the bottom of the frame
}

Error TarDefectsModel::Run(std::mutex& gpuLock, const torch::Tensor& input, const std::vector<PhotoJob*>& output) {
	gpuLock.lock();
	auto batchRes = Model.forward({input.cuda()}).toTensor();
	gpuLock.unlock();

	//tsf::print("Result size: %v\n", SizeToString(batchRes));
	// Shape of res: [1,12,56,152]
	int resWidth  = (int) batchRes.size(3);
	int resHeight = (int) batchRes.size(2);

	for (size_t i = 0; i < output.size(); i++) {
		auto job  = output[i];
		auto res  = batchRes[i];
		res       = res.permute({1, 2, 0}); // channels to back
		auto amax = res.argmax(2);          // pick category with highest score
		amax      = amax.toType(torch::kU8);
		//tsf::print("amax size: %v\n", SizeToString(amax));
		amax              = amax.cpu();
		auto       srcBuf = amax.accessor<uint8_t, 2>();
		gfx::Image img;
		img.Alloc(gfx::ImageFormat::Gray, resWidth, resHeight);
		for (int y = 0; y < resHeight; y++) {
			int            srcStride = srcBuf.stride(0);
			const uint8_t* srcLine   = srcBuf.data() + srcBuf.stride(0) * y;
			uint8_t*       dstLine   = img.Line(y);
			memcpy(dstLine, srcLine, resWidth);
		}
		job->TarDefectImage = move(img);
		// Analyze compression factor vs size
		// job->TarDefectImage.SavePng("/home/ben/viz/152x56-0.png", false, 0);
		// job->TarDefectImage.SavePng("/home/ben/viz/152x56-1.png", false, 1);
		// job->TarDefectImage.SavePng("/home/ben/viz/152x56-5.png", false, 5);
		// job->TarDefectImage.SavePng("/home/ben/viz/152x56-9.png", false, 9);
		SegmentationSummaryToJSON(job->TarDefectImage, job->DBOutput);
		//DrawDebugImage(job);
	}

	return Error();
}

void TarDefectsModel::DrawDebugImage(PhotoJob* job) const {
	auto       cats = job->TarDefectImage;
	gfx::Image rgb  = TensorToImg(job->RGB);

	// Produce an upscaled version of the categories image.
	// While upscaling, apply a palette.
	gfx::Image blowup;
	blowup.Alloc(gfx::ImageFormat::RGBA, rgb.Width, rgb.Height);

	// model produces 8x lower resolution than input, hence the 3 bit shift
	const int resShift = 3;

	int numCat   = (int) Meta.CategoryToName.size();
	int noDefect = Meta.NameToCategory.get("none"); // Tar road, without any defects

	for (int y = 0; y < blowup.Height; y++) {
		int sy = y >> resShift;
		for (int x = 0; x < blowup.Width; x++) {
			int             sx   = x >> resShift;
			uint8_t         val  = *cats.At(sx, sy);
			double          hval = 360 * val / (double) numCat;
			ColorSpace::Lch lch(65, 95, hval);
			ColorSpace::Rgb rgbColor;
			lch.To(&rgbColor);
			uint8_t r = (uint8_t)(math::Clamp(rgbColor.r, 0.0, 255.0));
			uint8_t g = (uint8_t)(math::Clamp(rgbColor.g, 0.0, 255.0));
			uint8_t b = (uint8_t)(math::Clamp(rgbColor.b, 0.0, 255.0));
			if (val == noDefect) {
				*blowup.At32(x, y) = gfx::Color8(0, 0, 0, 0).u;
			} else {
				*blowup.At32(x, y) = gfx::Color8(r, g, b, 100).u;
			}
		}
	}

	blowup.BlendOnto(rgb);
	rgb.SaveJpeg("/home/ben/viz/res.jpg", 95);
}

} // namespace roadproc
} // namespace imqs