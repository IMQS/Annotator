#include "pch.h"
#include "RoadType.h"
#include "TorchUtils.h"

using namespace std;

namespace imqs {
namespace roadproc {

Error RoadTypeModel::Load(std::string filename) {
	try {
		Model = torch::jit::load(filename.c_str());
	} catch (std::exception& e) {
		return Error(e.what());
	}
	return Error();
}

Error RoadTypeModel::Classify(const gfx::Image& _img, Type& type) {
	if (_img.Format != gfx::ImageFormat::RGBA && _img.Format != gfx::ImageFormat::RGBAP)
		return Error("Image must be RGBA");

	const gfx::Image* img = &_img;
	gfx::Image        tmp;
	if (_img.Width == 1920 && _img.Height == 1080) {
		tmp = _img.HalfSizeCheap();
		tmp = tmp.HalfSizeCheap();
		img = &tmp;
	}
	if (img->Width != 480 || img->Height != 270)
		return Error("Frame for road classifier must be RGB 1920 x 1080, or the native NN size of 480 x 270");

	img->SaveFile("/home/ben/tt1.png");
	auto t  = ImgToTensor(*img);
	auto dd = t.accessor<float, 3>();
	tsf::print("%f %f %f\n", dd[100][100][0], dd[100][100][1], dd[100][100][2]);
	//t      = t.cuda();
	TensorToImg(t).SaveFile("/home/ben/tt2.png");
	t       = t.permute({2, 0, 1}); // HWC -> CHW
	auto sh = t.sizes().vec();
	sh.insert(sh.begin(), 1); // insert batch channel with single dimension
	//tsf::print("%v\n", SizeToString(t.sizes()));
	t = t.reshape(sh);
	//tsf::print("input shape: %v\n", SizeToString(t.sizes()));
	auto res = Model->forward({t}).toTensor().cpu();
	res.squeeze_();
	tsf::print("output shape: %v\n", SizeToString(res.sizes()));
	tsf::print("output: %v\n", SmallTensorToString(res, "%3.2f"));
	auto am = torch::argmax(res).item().toInt();
	IMQS_ASSERT(am >= 0 && am <= 2);
	type = (Type) am;

	return Error();
}

int RoadTypeModel::Test(argparse::Args& args) {
	/*
	// This WORKS.
	{
		try {
			auto model = torch::jit::load("/home/ben/dev/roads/ai/dummy.tm");
			auto in    = torch::ones({2});
			in[0]      = 3;
			in[1]      = 4;
			auto res   = model->forward({in}).toTensor();
			tsf::print("output: %f\n", res.item().toFloat());
		} catch (std::exception& e) {
			tsf::print("%s\n", e.what());
		}
		return 0;
	}
	*/
	Error         err;
	RoadTypeModel rtm;
	err = rtm.Load("/home/ben/dev/roads/ai/roadtype.tm");
	if (!err.OK()) {
		tsf::print("%v\n", err.Message());
		return 1;
	}

	auto classify = [&](string filename) {
		gfx::Image img;
		img.LoadFile(filename);
		RoadTypeModel::Type rt;
		err = rtm.Classify(img, rt);
		//tsf::print("%v: %v\n", filename, (int) rt);
	};

	//classify("/home/ben/mldata/TypeOfRoad/gravel/vlcsnap-2019-02-22-15h06m06s523.png");
	//classify("/home/ben/mldata/TypeOfRoad/gravel/vlcsnap-2019-02-22-14h59m33s791.png");
	classify("/home/ben/mldata/TypeOfRoad/tar/vlcsnap-2019-02-22-14h38m37s010.png");
	//classify("/home/ben/mldata/TypeOfRoad/tar/vlcsnap-2019-02-22-15h03m01s929.png");
	//classify("/home/ben/mldata/TypeOfRoad/tar/vlcsnap-2019-02-22-15h03m59s737.png");

	/*
	//auto   res = torch::empty({1, 270, 480});
	//float* buf = (float*) malloc(270 * 480 * sizeof(float));
	for (int i = 0; i < 2; i++) {
		auto in  = torch::ones({1, 3, 270, 480});
		in       = in.cuda();
		auto out = rtm.Model->forward({in}).toTensor().cpu();
		tsf::print("out: %v, %v\n", SizeToString(out.sizes()), SmallTensorToString(out, "%.1f"));
		auto amax = torch::argmax(out);
		tsf::print("amax: %v, %v\n", SizeToString(amax.sizes()), amax.item().toFloat());
		//res.copy_(out[0]);
		//auto copy = torch::empty({1, 270, 480});
		//copy.copy_(out[0]);
		//auto copy = out[0].cpu();
		//auto src  = copy.accessor<float, 3>();
		//memcpy(buf, src.data(), 270 * 480 * sizeof(float));
		//int  width = 480;
		//for (int y = 0; y < 270; y++)
		//	memcpy(buf + width * y, src.data() + width * y, width * sizeof(float));
	}
	//free(buf);
	*/
	return 0;
}

} // namespace roadproc
} // namespace imqs