#include "pch.h"
#include "Globals.h"
#include "Perspective.h"
#include "MeshRenderer.h"
#include "OpticalFlow.h"
#include "Bench.h"
#include "gen2/RoadType.h"
#include "gen2/PhotoProcessor.h"
#include "Experiments/CudaLearn.h"

namespace imqs {
namespace roadproc {
int Speed(argparse::Args& args);
int MeasureScale(argparse::Args& args);
int Stitch(argparse::Args& args);
int WebTiles(argparse::Args& args);
int Auto(argparse::Args& args);
} // namespace roadproc
} // namespace imqs

// TODO: move into maps/tests/gfx/raster.cpp
namespace imqs {
namespace gfx {
namespace raster {
void TestBilinear() {
	{
		// 16-bit two channel (used for image warping)
		uint16_t img[8] = {
		    0x0001, 0x0002, 0x0050, 0x0060, //
		    0x0888, 0x0999, 0xffff, 0xfff0, //
		};
		auto show = [&](int32_t u, int32_t v) {
			uint32_t w = imqs::gfx::raster::ImageBilinear_RG_U16(img, 2, 255, 255, u, v);
			uint16_t x = w & 0xffff;
			uint16_t y = w >> 16;
			tsf::print("%v %v -> %02x %02x\n", u, v, x, y);
		};
		show(0, 0);
		show(255, 255);
		show(127, 127);
		show(0, 64);
	}

	{
		// 24-bit two channel (used for image warping)
		uint32_t img[8] = {
		    0x000100, 0x000200, 0x005000, 0x006000, //
		    0x088800, 0x099900, 0xffffff, 0xffffff, //
		};
		auto show = [&](int32_t u, int32_t v) {
			uint64_t w = imqs::gfx::raster::ImageBilinear_RG_U24(img, 2, 255, 255, u, v);
			uint32_t x = w & 0xffffffff;
			uint32_t y = w >> 32;
			tsf::print("%v %v -> %02x %02x\n", u, v, x, y);
		};
		show(0, 0);
		show(255, 255);
		show(127, 127);
		show(0, 64);
	}
}
} // namespace raster
} // namespace gfx
} // namespace imqs

void UnitTestVideo() {
	imqs::video::NVVideo vid;
	//vid.OpenFile("/home/ben/mldata/DSCF3037.MOV");
	vid.OpenFile("/home/ben/mldata/DSCF3022.MOV");
	int w = 1920;
	int h = 1080;
	//w     = 1664;
	//h     = 936;
	//w = 1280;
	//h = 720;
	w = 1024;
	h = 576;
	//w = 512;
	//h = 288;
	vid.SetOutputResolution(w, h);
	imqs::gfx::Image img;
	img.Alloc(imqs::gfx::ImageFormat::RGBA, w, h, w * 4);
	tsf::print("Decoding...\n");
	vid.DecodeFrameRGBA(w, h, img.Data, w * 4);
	return;
	img.SaveFile("/home/ben/f1.jpeg");
	tsf::print("Done saving file\n");
	vid.DecodeFrameRGBA(w, h, img.Data, w * 4);
	vid.DecodeFrameRGBA(w, h, img.Data, w * 4);
	img.SaveFile("/home/ben/f3.jpeg");
	int j = 0;
	for (int i = 0; i < 1000; i++) {
		bool isSwitch = false;
		if (i % 30 == 0) {
			isSwitch = true;
			if (j % 2 == 0) {
				w = 1280;
				h = 720;
			} else {
				w = 1024;
				h = 576;
			}
			tsf::print("Switching resolution to %v x %v\n", w, h);
			img.SaveFile("/home/ben/f-BEFORE.jpeg");
			vid.SetOutputResolution(w, h);
			img.Alloc(imqs::gfx::ImageFormat::RGBA, w, h, w * 4);
			j++;
		}
		//tsf::print("Decoding %v\n", i);
		double ts  = 0;
		auto   err = vid.DecodeFrameRGBA(w, h, img.Data, w * 4, &ts);
		if (isSwitch)
			img.SaveFile("/home/ben/f-AFTER.jpeg");
		if (err == imqs::ErrEOF) {
			tsf::print("Video stream finished\n");
			break;
		} else if (!err.OK()) {
			tsf::print("error decoding frame: %v\n", err.Message());
		}
		if (i % 50 == 0)
			img.SaveFile("/home/ben/fN.jpeg");
		tsf::print("Decoded %.3f\n", ts);
	}
	tsf::print("Done many frames\n");
}

int main(int argc, char** argv) {
	using namespace imqs::roadproc;
	imqs::Error err;

	// You'll want to disable this if you're running under valgrind or ASAN
	//imqs::logging::SetupCrashHandler("RoadProcessor");

	imqs::video::VideoFile::Initialize();
	//imqs::gfx::raster::TestBilinear();
	//UnitTestVideo();
	//return 0;

	//imqs::gfx::Image img;
	////img.LoadFile("/home/ben/Pictures/vlcsnap-2018-08-30-10h09m46s586.png");
	////img.LoadFile("/home/ben/Pictures/vlcsnap-2018-06-22-14h33m23s250.png");
	////img.LoadFile("/home/ben/Pictures/vlcsnap-2018-08-30-10h18m24s551.png");
	//img.LoadFile("/home/ben/Pictures/vlcsnap-2018-08-28-16h56m07s892.png");
	////img.BoxBlur(2, 3);
	//imqs::roadproc::LocalContrast(img, 3, 3);
	//img.SaveFile("/home/ben/Pictures/blur.jpeg");
	//return 1;

	// This little chunk of code is very useful to verifying the sanity of our OpenGL system
	//imqs::roadproc::MeshRenderer rend;
	//auto                         err = rend.Initialize(800, 800);
	//if (!err.OK()) {
	//	tsf::print("Error: %v\n", err.Message());
	//	return 1;
	//}
	//rend.Clear(imqs::gfx::Color8(255, 255, 255, 255));
	//rend.DrawTestLines();
	//rend.SaveToFile("test.png");
	//return 0;

	//ColorSpace::Lch lch(95, 95, 150);
	//ColorSpace::Rgb rgb(255, 255, 0);
	////rgb.To(&lch);
	////tsf::print("%v, %v, %v\n", lch.l, lch.c, lch.h);
	//lch.To(&rgb);
	//tsf::print("%v, %v, %v\n", rgb.r, rgb.g, rgb.b);
	//return 0;

	argparse::Args args("Usage: RoadProcessor [options] <command>");
	args.AddValue("e", "lensdb", "Camera/Lens database", "/usr/local/share/lensfun/version_2/");
	args.AddValue("l", "lens", "Lens correction (eg 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS'");

	auto perspective = args.AddCommand("perspective <video>", "Compute perspective flattening parameters - final output line is JSON.", Perspective);
	perspective->AddSwitch("v", "verbose", "Show progress");

	auto speed = args.AddCommand("speed <flatten JSON> <video[,video2,...]>",
	                             "Compute car speed from interframe differences\nOne or more videos can be specified."
	                             " Separate multiple videos with commas. This version uses optical flow on flattened images",
	                             Speed);
	speed->AddSwitch("", "csv", "Write CSV output (otherwise JSON)");
	speed->AddValue("o", "outfile", "Write output to file", "stdout");
	speed->AddValue("s", "start", "Start time in seconds (for debugging)", "0");

	auto measureScale = args.AddCommand("measure-scale <video> <position track> <flatten JSON>", "Measure scale, in meters per pixel.", MeasureScale);

	auto stitch = args.AddCommand("stitch <video> <bitmap dir> <position track> <flatten JSON>", "Unproject video frames and stitch together.", Stitch);

	stitch->AddValue("n", "number", "Number of frames", "-1");
	stitch->AddValue("s", "start", "Start time in seconds", "0");
	stitch->AddValue("m", "mpp", "Meters per pixel", "0");
	stitch->AddSwitch("d", "dryrun", "Don't actually write anything to the infinite bitmap");

	auto webtiles = args.AddCommand("webtiles <infinite bitmap>", "Create web tiles from infinite bitmap", WebTiles);

	auto cmdAuto = args.AddCommand("auto <username> <password> <infinite bitmap> <video[,video2,...]>", "Do everything to get stitched imagery out", Auto);
	cmdAuto->AddValue("", "flatten", "Flatten parameters definition (JSON)", "");
	cmdAuto->AddValue("", "speed", "Speed track (JSON)");
	cmdAuto->AddValue("m", "mpp", "Meters per pixel", "0");

	auto bench = args.AddCommand("bench", "Various internal benchmarks", Bench);

	auto photos = args.AddCommand("photos <username> <password> <client> <prefix> <cloud storage credentials file>", "Run the gen2 models on GoPro photos", PhotoProcessor::Run);
	photos->AddValue("r", "resume", "Start at the given photo number (0 is the first)", "0");
	photos->AddValue("s", "server", "Server where the 'console' service runs", "http://roads.imqs.co.za");

	if (!args.Parse(argc, (const char**) argv))
		return 1;

	err = global::Initialize();
	if (!err.OK()) {
		tsf::print("Program initialization failed: %v\n", err.Message());
		return 1;
	}

	if (args.Get("lens") != "") {
		global::Lens = new LensCorrector();
		auto err     = global::Lens->LoadDatabase(args.Get("lensdb"));
		if (!err.OK()) {
			tsf::print("Error loading lens database: %v\n", err.Message());
			return 1;
		}
		err = global::Lens->LoadCameraAndLens(args.Get("lens"));
		if (!err.OK()) {
			tsf::print("Error loading camera/lens: %v\n", err.Message());
			return 1;
		}
	}

	// testing...
	//imqs::roadproc::TestCuda();
	//return 1;

	int ret = args.ExecCommand();

	free(global::LensFixedtoRaw);
	delete global::Lens;

	global::Shutdown();

	return ret;
}
