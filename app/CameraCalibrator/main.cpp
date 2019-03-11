#include "pch.h"
#include "third_party/xo/templates/xoWinMain.cpp"

using namespace std;

typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

static bool  Manipulate = true;
static float ZFactor1   = 1.00f;
static float ZFactor2   = -0.0007f;

uint32 Bilinear_x64(uint32 a, uint32 b, uint32 c, uint32 d, uint32 ix, uint32 iy) {
	// By Nils Pipenbrinck.
	uint64 mask = 0x00ff00ff00ff00ffLL;
	uint64 a64  = (a | ((uint64) a << 24)) & mask;
	uint64 b64  = (b | ((uint64) b << 24)) & mask;
	uint64 c64  = (c | ((uint64) c << 24)) & mask;
	uint64 d64  = (d | ((uint64) d << 24)) & mask;
	a64         = a64 + (((b64 - a64) * ix + mask) >> 8);
	c64         = c64 + (((d64 - c64) * ix + mask) >> 8);
	a64 &= mask;
	c64 &= mask;
	a64 = a64 + (((c64 - a64) * iy + mask) >> 8);
	a64 &= mask;
	return (uint32)(a64 | (a64 >> 24));
}

uint32 ImageBilinear(const void* src, int width, int uclamp, int vclamp, int32 u, int32 v) {
	if (u < 0)
		u = 0;
	if (u >= uclamp)
		u = uclamp;
	if (v < 0)
		v = 0;
	if (v >= vclamp)
		v = vclamp;
	uint32  iu   = (uint32) u >> 8;
	uint32  iv   = (uint32) v >> 8;
	uint32  ru   = (uint32) u & 0xff;
	uint32  rv   = (uint32) v & 0xff;
	uint32* src1 = (uint32*) src + iv * width + iu;
	uint32* src2 = (uint32*) src + (iv + 1) * width + iu;
	return Bilinear_x64(src1[0], src1[1], src2[0], src2[1], ru, rv);
}

// ux and uy are base-256
void UnprojectPos(int32 x, int32 y, int32& u, int32& v) {
	float cx = 1920 / 2;
	float cy = 1080 / 2;
	float fx = (float) x - cx;
	float fy = (float) y - cy;
	float z  = ZFactor1 + ZFactor2 * fy;
	fx /= z;
	fy /= z;
	fx += cx;
	fy += cy;
	fx *= 256;
	fy *= 256;
	u = (int32) fx;
	v = (int32) fy;
}

void UnprojectImage(void* dst, int dstWidth, int dstHeight, int dstStride, const void* src, int srcWidth, int srcHeight, int srcStride) {
	int32   srcClampU = (srcWidth - 1) * 256 - 1;
	int32   srcClampV = (srcHeight - 1) * 256 - 1;
	uint32* dst32     = (uint32*) dst;
#pragma omp parallel for
	for (int dstY = 0; dstY < dstHeight; dstY++) {
		for (int dstX = 0; dstX < dstWidth; dstX++) {
			int32 u, v;
			UnprojectPos(dstX, dstY, u, v);
			uint32 color                  = ImageBilinear(src, srcWidth, srcClampU, srcClampV, u, v);
			dst32[dstY * dstWidth + dstX] = color;
		}
	}
}

void xoMain(xo::SysWnd* wnd) {
	using namespace imqs;

	auto doc      = wnd->Doc();
	auto root     = &doc->Root;
	auto zfactor1 = root->AddText("zfactor1");
	root->AddText(" -- ");
	auto zfactor2 = root->AddText("zfactor2");
	root->ParseAppendNode("<div style='break:after'/>");

	auto canvas     = root->AddCanvas();
	int  canvWidth  = 1920;
	int  canvHeight = 1080;
	canvas->SetImageSizeOnly(canvWidth, canvHeight);
	canvas->StyleParsef("width: 1860px; height: 1000px");

	//string       src = "C:\\Users\\benh\\Pictures\\perspective-1.png";
	//string       src = "/home/ben/Pictures/vlcsnap-2018-06-22-14h33m23s250.png";
	//string       src = "/home/ben/Pictures/snappy.png";
	//string src = "/home/ben/Pictures/vlcsnap-2018-08-27-15h11m17s398-bottom-half.png";
	//string src = "/home/ben/Pictures/vlcsnap-2018-08-27-15h11m17s398-bottom-2-thirds.png";
	//string       src = "/home/ben/Pictures/vlcsnap-2018-08-27-15h11m17s398-FHD.png";
	//string       src = "/home/ben/Pictures/vlcsnap-2018-08-28-16h56m07s892-002704-325-crop.png";
	string       src = "/home/ben/Pictures/vlcsnap-3436-middle.png";
	string       srcRaw;
	gfx::ImageIO imgIO;
	os::ReadWholeFile(src, srcRaw);
	void* img    = nullptr;
	int   width  = 0;
	int   height = 0;
	imgIO.LoadPng(srcRaw.data(), srcRaw.size(), width, height, img);

	auto lastDraw = new time::Time();
	*lastDraw     = time::Now();

	auto c2d = canvas->GetCanvas2D();
	/*
	uint8* imgB = (uint8*) img;
	for (int i = 0; i < height; i++) {
		memcpy(c2d->RowPtr(i), imgB + i * width * 4, width * 4);
	}
	*/
	UnprojectImage(c2d->Buffer(), canvWidth, canvHeight, canvWidth * 4, img, width, height, width * 4);

	canvas->ReleaseAndInvalidate(c2d);

	root->OnClick([=]() -> void {
		Manipulate = !Manipulate;
	});

	root->OnMouseMove([=](xo::Event& ev) -> void {
		if (!Manipulate)
			return;
		// for linux we have to rate limit, because I guess XO doesn't discard queued mousemove messages on linux.
		if (time::Now() - *lastDraw < 30 * time::Millisecond)
			return;
		ZFactor1 = 1.0 + (ev.PointsRel[0].x - 500) * 0.0015f;
		ZFactor2 = (ev.PointsRel[0].y - 800) * 0.000003f;
		zfactor1->SetText(tsf::fmt("%8.6f", ZFactor1).c_str());
		zfactor2->SetText(tsf::fmt("%8.6f", ZFactor2).c_str());
		auto c2d = canvas->GetCanvas2D();
		UnprojectImage(c2d->Buffer(), canvWidth, canvHeight, canvWidth * 4, img, width, height, width * 4);
		canvas->ReleaseAndInvalidate(c2d);
		*lastDraw = time::Now();
	});
}
