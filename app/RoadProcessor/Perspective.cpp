#include "pch.h"
#include "FeatureTracking.h"
#include "LensCorrection.h"
#include "Globals.h"
#include "Perspective.h"
#include "MeshRenderer.h"
#include "OpticalFlow.h"
#include "Mesh.h"

// build/run-roadprocessor -r --lens 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS' perspective /home/ben/mldata/DSCF3040.MOV
// build/run-roadprocessor -r --lens 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS' perspective /home/ben/mldata/mthata/Day3-4.MOV
// build/run-roadprocessor -r --lens 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS' perspective '/home/ben/mldata/train/ORT Day1 (2).MOV'
// build/run-roadprocessor -r --lens 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS' perspective '/home/ben/mldata/train/Day5 (17).MOV'
// docker run --runtime=nvidia --rm -v /home/ben/mldata:/mldata roadprocessor --lens 'Fujifilm X-T2,Samyang 12mm f/2.0 NCS CS' perspective /mldata/DSCF3040.MOV

// 7 samples per iteration, 10x1 grid
// mthata/DSCF0001-HG-3.MOV: -0.000385 (2 iterations)
// mthata/DSCF0001-HG-4.MOV: -0.000401 (2 iterations)
// mthata/DSCF0001-HG-5.MOV: -0.000402 (2 iterations)

// 7 samples per iteration, 10x2 grid
// DSCF3040.MOV: -0.000981 (2 iterations)
// mthata/DSCF0001-HG-3.MOV: -0.000391 (2 iterations)
// mthata/DSCF0001-HG-4.MOV: -0.000392 (2 iterations)
// mthata/DSCF0001-HG-5.MOV: -0.000396 (2 iterations)

// mthata/Day3-4.MOV: ZY: -0.000722

// Times.. on the first mthata video I'm trying, time for perspective computation is 1m40.

/*
The functions in here compute the perspective factor Z2, from a video.
We assume that the camera is pointed down at a plane, which makes the
unprojection function extremely simple:
X = x/(Z1 - Z2*y)
Y = y/(Z1 - Z2*y)
where x and y are coordinates for the pixel that we're rendering, and
and X and Y are the source coordinates from the raw camera frame.
For an FHD image, the top-left corner is (-960, -540), and the bottom-right corner is (960, 540).

Z1 is a scaling factor, which we want at a value somewhere around 1.8 (although this
depends on Z2). If we keep Z1 at 1.0, then the resulting image maintains the resolution
at the top of the frame, but it shrinks the resolution at the bottom of the frame,
so we're losing information at the bottom of the frame. We don't want to lose resolution
at the bottom of the frame, so we rather make the entire resulting image larger. The pixels
at the top of the frame will then be stretched, but this is preferable to having the
pixels at the bottom be squashed.

The resulting unperspectified image looks like an upside-down triangle, or a V, but the bottom
of the V is cut off.. aka a frustum. This areas outside of the frustum are undefined. When we
choose our scaling factor, we measure it by the bottom of the frustum. If we want to lose zero
precision at the bottom of the frame, then we make sure that the bottom edge of the frustum
is exactly 1920 pixels across. We can apply any arbitrary factor on top of that, if we want
to sacrifice resolution, for performance, but that shouldn't be necessary.

Solving for y, given Y:
(a = z1, b = Zx, c = Zy, u = Y, v = X)
With Wolfram Alpha, to solve for y: solve {y=(v*a+v*b*((u*a+u*c*y)/(1-u*b)))/(1-v*c)}
Result: y = -(a v)/(b u + c v - 1), which is what we use, and looks correct.

*/

using namespace std;
using namespace imqs::gfx;

namespace imqs {
namespace roadproc {

Frustum FlattenParams::ComputeFrustum() const {
	return roadproc::ComputeFrustum(SensorCrop, PP);
}

std::string FlattenParams::ToJson() const {
	return tsf::fmt(R"({"z1":%.6f, "zx":%.6f, "zy":%.6f, "sensorCrop":[%v,%v,%v,%v]})", PP.Z1, PP.ZX, PP.ZY, SensorCrop.x1, SensorCrop.y1, SensorCrop.x2, SensorCrop.y2);
}

Error FlattenParams::ParseJson(const std::string& s) {
	*this = FlattenParams();
	nlohmann::json j;
	auto           err = nj::ParseString(s, j);
	if (!err.OK()) {
		return Error::Fmt("Error parsing flattening JSON: %v", err.Message());
	}
	PP.Z1     = nj::GetDouble(j, "z1", 1);
	PP.ZX     = nj::GetDouble(j, "zx", 0);
	PP.ZY     = nj::GetDouble(j, "zy", 0);
	auto crop = j.find("sensorCrop");
	if (crop != j.end()) {
		auto& cropv = *crop;
		if (crop->is_array() && crop->size() == 4) {
			SensorCrop.x1 = cropv[0].get<double>();
			SensorCrop.y1 = cropv[1].get<double>();
			SensorCrop.x2 = cropv[2].get<double>();
			SensorCrop.y2 = cropv[3].get<double>();
		} else {
			return Error("Invalid sensorCrop. Must be an array of 4 numbers");
		}
	}
	return Error();
}

void Frustum::DebugPrintParams(float z1, float zx, float zy, int frameWidth, int frameHeight) const {
	tsf::print("%.4f %10f %10f => %v x %v   %6.1f .. %6.1f (bottom scale %v, top scale %v)\n", z1, zx, zy, Width, Height, X1, X2, (X2 - X1) / frameWidth, (float) Width / frameWidth);
}

void Frustum::Polygon(Vec2f* poly, float expandX, float expandY) {
	poly[0] = Vec2f(-expandX, -expandY);
	poly[1] = Vec2f((float) Width / 2.0f + X1 - expandX, (float) Height + expandY);
	poly[2] = Vec2f((float) Width / 2.0f + X2 + expandX, (float) Height + expandY);
	poly[3] = Vec2f(Width + expandX, -expandY);
}

// This runs newton's method, until it produces a y value that is
// within stopAtPrecision distance from desiredY, or until stopAtIteration iterations.
// Returns the best-performing x.
// dx is the epsilon for gradient computation.
float NewtonSearch(float x, float dx, float desiredY, float stopAtPrecision, int stopAtIteration, function<float(float x)> func) {
	for (int i = 0; i < stopAtIteration; i++) {
		float y = func(x);
		if (fabs(y - desiredY) <= stopAtPrecision)
			break;
		float y0   = func(x - dx);
		float y1   = func(x + dx);
		float grad = (y1 - y0) / (dx * 2);
		if (grad == 0)
			break;
		x = x - (y - desiredY) / grad;
	}
	return x;
}

void FitQuadratic(const std::vector<std::pair<double, double>>& xy, double& a, double& b, double& c) {
	// http://mathforum.org/library/drmath/view/72047.html

	/*
	Now all you have to do is take your data (your eight points) and
	evaluate the various sums

			n
	Sj0 = sum x_i^j
			i=1

	for j = 0 through 4, and

			n
	Sj1 = sum x_i^j*y_i
			i=1

	for j = 0 through 2.  Then you substitute into the formulas for a, b,
	and c, and you are done!	
	*/

	double S[5][2];
	for (size_t j = 0; j < 5; j++) {
		S[j][0] = 0;
		S[j][1] = 0;
		for (size_t i = 0; i < xy.size(); i++) {
			S[j][0] += pow(xy[i].first, (double) j);
			S[j][1] += pow(xy[i].first, (double) j) * xy[i].second; // only used when j = 0,1,2
		}
	}

	/*
	Now we can use Cramer's Rule to give a, b, and c as formulas in these
	Sjk values.  They all have the same denominator:

		(S00*S20*S40 - S10^2*S40 - S00*S30^2 + 2*S10*S20*S30 - S20^3)	
	*/

	double denom = S[0][0] * S[2][0] * S[4][0] - pow(S[1][0], 2) * S[4][0] - S[0][0] * pow(S[3][0], 2) + 2 * S[1][0] * S[2][0] * S[3][0] - pow(S[2][0], 3);

	a = S[0][1] * S[1][0] * S[3][0] - S[1][1] * S[0][0] * S[3][0] - S[0][1] * pow(S[2][0], 2) + S[1][1] * S[1][0] * S[2][0] + S[2][1] * S[0][0] * S[2][0] - S[2][1] * pow(S[1][0], 2);
	b = S[1][1] * S[0][0] * S[4][0] - S[0][1] * S[1][0] * S[4][0] + S[0][1] * S[2][0] * S[3][0] - S[2][1] * S[0][0] * S[3][0] - S[1][1] * pow(S[2][0], 2) + S[2][1] * S[1][0] * S[2][0];
	c = S[0][1] * S[2][0] * S[4][0] - S[1][1] * S[1][0] * S[4][0] - S[0][1] * pow(S[3][0], 2) + S[1][1] * S[2][0] * S[3][0] + S[2][1] * S[1][0] * S[3][0] - S[2][1] * pow(S[2][0], 2);

	a /= denom;
	b /= denom;
	c /= denom;
}

// When we speak of normalized coordinates, we mean coordinates that are centered around the origin of the image

Vec2f FlatToCamera(float x, float y, float z1, float zx, float zy) {
	float z = z1 + zx * x + zy * y;
	return Vec2f(x / z, y / z);
}

// ux and uy are base-256
void FlatToCameraInt256(float z1, float zx, float zy, float x, float y, int32_t& u, int32_t& v) {
	float z  = z1 + zx * x + zy * y;
	float fx = x / z;
	float fy = y / z;
	fx *= 256;
	fy *= 256;
	u = (int32_t) fx;
	v = (int32_t) fy;
}

Vec2f FlatToCamera(int frameWidth, int frameHeight, float x, float y, float z1, float zx, float zy) {
	auto cam = FlatToCamera(x, y, z1, zx, zy);
	cam.x += float(frameWidth / 2);
	cam.y += float(frameHeight / 2);
	return cam;
}

Vec2f CameraToFlat(Vec2f cam, float z1, float zx, float zy) {
	// If zx is zero, then:
	//float yf = (cam.y * z1) / (1 - cam.y * zy);
	//float xf = cam.x * (z1 + zy * yf);
	// If zx is not zero, then:
	float yf = (cam.y * z1) / (1 - zx * (zy * cam.x * cam.y + cam.x));
	float xf = (cam.x * z1 + cam.x * zy * yf) / (1 - cam.x * zx);
	return Vec2f(xf, yf);
}

Vec2f CameraToFlat(int frameWidth, int frameHeight, Vec2f cam, float z1, float zx, float zy) {
	cam.x       = cam.x - float(frameWidth / 2);
	cam.y       = cam.y - float(frameHeight / 2);
	float yfOld = (cam.y * z1) / (1 - cam.y * zy);
	float xfOld = cam.x * (z1 + zy * yfOld);
	//float yf    = (cam.y * z1) / (1 - zx * (zy * cam.x * cam.y + cam.x));
	//float xf    = (cam.x * z1 + cam.x * zy * yf) / (1 - cam.x * zx);
	float yf = (cam.y * z1) / (1 - (zx * cam.x + zy * cam.y));
	float xf = cam.x * (z1 + zy * yf) / (1 - cam.x * zx);
	return Vec2f(xf, yf);
}

gfx::Vec2f CameraToFlat(int frameWidth, int frameHeight, gfx::Vec2f cam, PerspectiveParams pp) {
	return CameraToFlat(frameWidth, frameHeight, cam, pp.Z1, pp.ZX, pp.ZY);
}

// Given a camera sensor size, and perspective parameters, compute a crop of the sensor that
// we will use, which will limit the width of the flattened image to 4096.
// The scaling parameter pp.Z1 is also modified, so that the scale at the bottom of the frame remains 1.0
gfx::Rect32 ComputeSensorCrop(int frameWidth, int frameHeight, PerspectiveParams& pp, int maxFlattenedWidth) {
	auto widthAt = [=](float croppedHeight) -> float {
		// This is a newton search INSIDE a newton search
		PerspectiveParams ptemp = pp;
		ptemp.Z1                = FindZ1ForIdentityScaleAtBottom(frameWidth, croppedHeight, ptemp.ZX, ptemp.ZY);
		Frustum f               = ComputeFrustum(frameWidth, croppedHeight, ptemp);
		return f.Width;
	};
	// There is a giant discontinuity in the computed frustum width, if pp.ZY is large enough to cause the horizon to
	// be visible. What happens is that you end up with a divide by zero (or very close to zero), and this is what causes
	// the discontinuity. You can see it in action in CameraToFlat, inside the division that computes computes yf.
	// So, we need to make sure that we're always on the "correct" side of that cliff. The way to tell if we've crossed
	// over that cliff, is to run ComputeFrustum, and check if the width is negative. If the width is negative, it means
	// we're on the wrong side of the cliff.
	// So our first step here, is to reduce our initial frameHeight, so that we're on the ride side of the cliff.
	int croppedHeight = frameHeight;
	while (true) {
		Frustum f = ComputeFrustum(frameWidth, croppedHeight, pp);
		if (f.Width > 0)
			break;
		croppedHeight -= 32;
	}

	// this iteration tends to just get stuck on an integer that can't bring it within 0.1 of the desired maxFlattenedWidth,
	// but that's only because our objective function 'ComputeFrustum' takes integer inputs. But it's all OK - we just end up
	// rounding down, and we're well within our limits (maxFlattenedWidth is not a hard limit).
	croppedHeight = (int) NewtonSearch(croppedHeight, 1, maxFlattenedWidth, 0.1, 6, widthAt);
	croppedHeight = min(croppedHeight, frameHeight);
	pp.Z1         = FindZ1ForIdentityScaleAtBottom(frameWidth, croppedHeight, pp.ZX, pp.ZY);
	Frustum fTest = ComputeFrustum(frameWidth, croppedHeight, pp);

	// Has the newton search taken us back onto the other side of the cliff?
	IMQS_ASSERT(fTest.Width > 0);

	return Rect32(0, frameHeight - croppedHeight, frameWidth, frameHeight);
}

// For the time being (August 2018), it's OK to hardcode this limit to 4096. It makes our code
// simpler if we can just bake this constant in right here.
gfx::Rect32 ComputeSensorCropDefault(int frameWidth, int frameHeight, PerspectiveParams& pp) {
	return ComputeSensorCrop(frameWidth, frameHeight, pp, 4096);
}

// Given a camera frame width and height, compute a frustum such that the bottom edge of the frustum
// has the exact same resolution as the bottom edge of the camera frame.
Frustum ComputeFrustum(int frameWidth, int frameHeight, float z1, float zx, float zy) {
	auto    topLeft  = CameraToFlat(frameWidth, frameHeight, Vec2f(0, 0), z1, zx, zy);
	auto    topRight = CameraToFlat(frameWidth, frameHeight, Vec2f(frameWidth, 0), z1, zx, zy);
	auto    botLeft  = CameraToFlat(frameWidth, frameHeight, Vec2f(0, frameHeight), z1, zx, zy);
	auto    botRight = CameraToFlat(frameWidth, frameHeight, Vec2f(frameWidth, frameHeight), z1, zx, zy);
	Frustum f;
	f.Width  = (int) (topRight.x - topLeft.x);
	f.Height = (int) (botLeft.y - topLeft.y);
	f.X1     = botLeft.x;
	f.X2     = botRight.x;
	return f;
}

Frustum ComputeFrustum(int frameWidth, int frameHeight, PerspectiveParams pp) {
	return ComputeFrustum(frameWidth, frameHeight, pp.Z1, pp.ZX, pp.ZY);
}

Frustum ComputeFrustum(gfx::Rect32 sensorCrop, PerspectiveParams pp) {
	return ComputeFrustum(sensorCrop.Width(), sensorCrop.Height(), pp);
}

// Use an iterative solution to find a value for Z1 (ie scale), which produces a 1:1 scale at
// the bottom of our flattened space.
float FindZ1ForIdentityScaleAtBottom(int frameWidth, int frameHeight, float zx, float zy) {
	auto scale = [=](float x) -> float {
		auto f = ComputeFrustum(frameWidth, frameHeight, x, zx, zy);
		return (f.X2 - f.X1) / frameWidth;
	};
	return NewtonSearch(1.0, 0.001, 1.0, 1e-6, 10, scale);
}

static void PrintCamToFlat(int frameWidth, int frameHeight, float z1, float zx, float zy, Vec2f cam) {
	auto p = CameraToFlat(frameWidth, frameHeight, cam, z1, zx, zy);
	auto r = FlatToCamera(frameWidth, frameHeight, p.x, p.y, z1, zx, zy);
	tsf::print("(%5.0f,%5.0f) -> (%5.0f,%5.0f) -> (%5.0f,%5.0f)\n", cam.x, cam.y, p.x, p.y, r.x, r.y);
}

static void TestPerspectiveAndFrustum(float z1, float zx, float zy) {
	PerspectiveParams pp(z1, zx, zy);
	int               frameWidth  = 1920;
	int               frameHeight = 1080;
	// compute raw numbers on z1, z2
	auto f = ComputeFrustum(frameWidth, frameHeight, pp);
	f.DebugPrintParams(pp.Z1, pp.ZX, pp.ZY, frameWidth, frameHeight);

	// auto compute frustum
	pp.Z1 = FindZ1ForIdentityScaleAtBottom(frameWidth, frameHeight, pp.ZX, pp.ZY);
	f     = ComputeFrustum(frameWidth, frameHeight, pp);
	f.DebugPrintParams(pp.Z1, pp.ZX, pp.ZY, frameWidth, frameHeight);

	int w = frameWidth;
	int h = frameHeight;
	PrintCamToFlat(w, h, pp.Z1, pp.ZX, pp.ZY, Vec2f(0, 0));
	PrintCamToFlat(w, h, pp.Z1, pp.ZX, pp.ZY, Vec2f(w, 0));
	PrintCamToFlat(w, h, pp.Z1, pp.ZX, pp.ZY, Vec2f(0, h));
	PrintCamToFlat(w, h, pp.Z1, pp.ZX, pp.ZY, Vec2f(w, h));

	auto crop = ComputeSensorCropDefault(frameWidth, frameHeight, pp);
	f         = ComputeFrustum(frameWidth, crop.Height(), pp);
	tsf::print("Sensor crop: %v,%v - %v,%v (%v x %v)\n", crop.x1, crop.y1, crop.x2, crop.y2, crop.Width(), crop.Height());
	f.DebugPrintParams(pp.Z1, pp.ZX, pp.ZY, frameWidth, crop.Height());

	tsf::print("\n");
}

// Get ready to build up an image that has identical dimensions to 'camera', but with distortion correction
// applied. The output of this phase, is a 2D matrix, 1:1 with the original camera pixels, where every
// entry of the matrix is an XY pair, that points to the coordinates of the original, raw camera frame.
//float* fixedToRaw = (float*) imqs_malloc_or_die(camera.Width * camera.Height * 2);
// NOTE: We're using 16-bit coordinates here, and we allocate 12 bits for the pixel, and 4 bits for
// the sub-pixel. If this becomes too small (because camera res has increased), then rather use CUDA
// for this job, or switch to 32-bit coordinates. There is a bilinear lookup function for 32 bits,
// very similar to what's going on here for 16 bits.
// 4K res is 3840 x 2160. 12 bits of integer precision is enough for 4096, so we're OK here for 4K,
// but we'll see if 4 bits of sub-pixel precision is good enough.

// 12 + 4 = 16
const int DistortSubPixelBits = 4;
uint16_t* ComputeLensDistortionMatrix(int width, int height) {
	uint16_t* fixedToRaw = (uint16_t*) imqs_malloc_or_die(width * height * 2 * sizeof(uint16_t));

	for (int y = 0; y < height; y++) {
		global::Lens->ComputeDistortionForLine(y);
		const float* src = global::Lens->InterpPos;
		uint16_t*    dst = fixedToRaw + y * width * 2;
		float        fw  = (float) width;
		float        fh  = (float) height;
		for (int x = 0; x < width; x++) {
			// (01 Red) (23 Green) (45 Blue). Each pair is an XY coordinate, pointing into the raw image frame
			// We only use green, because we're not interested in chromatic abberations. The Lensfun docs say that
			// chromatic abberation fixing on lossy-compressed images (vs RAW) is just worse than nothing at all.
			float u = math::Clamp<float>(src[2], 0, fw);
			float v = math::Clamp<float>(src[3], 0, fw);
			dst[0]  = (uint16_t)(u * (1 << DistortSubPixelBits));
			dst[1]  = (uint16_t)(v * (1 << DistortSubPixelBits));
			dst += 2;
			src += 6;
		}
	}
	return fixedToRaw;
}

void RemovePerspective(const Image& camera, Image& flat, float z1, float zx, float zy, float originX, float originY) {
	if (global::LensFixedtoRaw == nullptr && global::Lens != nullptr)
		global::LensFixedtoRaw = ComputeLensDistortionMatrix(camera.Width, camera.Height);

	// Because our end product here is a frustum, we can compute straight lines down the edges, and
	// avoid sampling into those lines, which wins us a little bit of performance, because it's
	// less filtering that we need to do.
	auto  f     = ComputeFrustum(camera.Width, camera.Height, z1, zx, zy);
	float x1Inc = (f.X1 + f.Width / 2) / (float) flat.Height;
	float x2Inc = (f.X2 + f.Width / 2 - flat.Width) / (float) flat.Height;
	// start the lines one pixel in. If we make this buffer large enough, then we don't need to
	// clamp inside our bilinear filter, which is a tiny speedup.
	float x1Edge = 1;
	float x2Edge = flat.Width - 1;

	// the -1 on the width is there because we're doing bilinear filtering, and we reach into sample+1
	// I'm not sure this is 100% correct.. and in fact we should probably add a bias of 1/2 a pixel
	int32_t srcClampU = (camera.Width - 1) * 256 - 1;
	int32_t srcClampV = (camera.Height - 1) * 256 - 1;

	// weird.. getting less distortion during optical flow calculations with less correction off
	// UPDATE: version 2 of the sticher looks better with lens correction on
	bool doLensCorrection = global::Lens != nullptr;

#pragma omp parallel for
	for (int y = 0; y < flat.Height; y++) {
		uint32_t* dst32 = (uint32_t*) flat.Data;
		dst32 += y * flat.Width;
		int     srcWidth = camera.Width;
		void*   src      = camera.Data;
		int32_t camHalfX = 256 * camera.Width / 2;
		int32_t camHalfY = 256 * camera.Height / 2;
		size_t  xStart   = (size_t) ceil(x1Edge + (float) y * x1Inc);
		size_t  xEnd     = (size_t) floor(x2Edge + (float) y * x2Inc);
		float   yM       = (float) y + originY;
		float   xM       = originX + xStart;
		for (size_t x = xStart; x < xEnd; x++, xM++) {
			int32_t u, v;
			// Flat to undistorted camera
			FlatToCameraInt256(z1, zx, zy, xM, yM, u, v);
			u += camHalfX;
			v += camHalfY;
			if (doLensCorrection) {
				// undistorted camera to raw camera
				uint32_t fixed = raster::ImageBilinear_RG_U16(global::LensFixedtoRaw, srcWidth, srcClampU, srcClampV, u, v);
				u              = fixed & 0xffff;
				v              = fixed >> 16;
				// bring the distortion parameters up to the required 8 bits of sub-pixel precision
				u = u << (8 - DistortSubPixelBits);
				v = v << (8 - DistortSubPixelBits);
			}
			// read from raw camera
			uint32_t color = raster::ImageBilinearRGBA(src, srcWidth, srcClampU, srcClampV, u, v);
			dst32[x]       = color;
		}
	}
}

void RemovePerspective(const gfx::Image& camera, gfx::Image& flat, PerspectiveParams pp, float originX, float originY) {
	RemovePerspective(camera, flat, pp.Z1, pp.ZX, pp.ZY, originX, originY);
}

struct ImageDiffResult {
	size_t MatchCount  = 0;
	double XMean       = 0;
	double YMean       = 0;
	double XVar        = 0;
	double YVar        = 0;
	float  OptFlowDiff = 0;
};

// Returns the difference between the two images, after they've been unprojected, and aligned
static ImageDiffResult ImageDiff(MeshRenderer& rend, const Image& img1, const Image& img2, float zx, float zy) {
	bool debug = false;

	float z1         = FindZ1ForIdentityScaleAtBottom(img1.Width, img1.Height, zx, zy);
	auto  f          = ComputeFrustum(img1.Width, img1.Height, z1, zx, zy);
	auto  flatOrigin = CameraToFlat(img1.Width, img1.Height, Vec2f(0, 0), z1, zx, zy);
	int   orgX       = (int) flatOrigin.x;
	int   orgY       = (int) flatOrigin.y;

	Image flat1, flat2;
	flat1.Alloc(ImageFormat::RGBA, f.Width, f.Height);
	flat2.Alloc(ImageFormat::RGBA, f.Width, f.Height);

	//RemovePerspective(img1, flat1, z1, zx, zy, orgX, orgY);
	//RemovePerspective(img2, flat2, z1, zx, zy, orgX, orgY);
	//flat1.SaveFile("flat1.jpeg");
	//flat2.SaveFile("flat2.jpeg");

	rend.RemovePerspectiveAndCopyOut(img1, nullptr, PerspectiveParams(z1, zx, zy), flat1);
	rend.RemovePerspectiveAndCopyOut(img2, nullptr, PerspectiveParams(z1, zx, zy), flat2);
	//flat1.SaveFile("flat1.jpeg");
	//flat2.SaveFile("flat2.jpeg");

	int   windowX1 = flat1.Width / 2 + f.X1 + 2;
	int   windowX2 = flat1.Width / 2 + f.X2 - 2;
	int   windowY1 = 0;
	int   windowY2 = flat1.Height;
	Image crop1, crop2;
	crop1          = flat1.Window(windowX1, windowY1, windowX2 - windowX1, windowY2 - windowY1);
	crop2          = flat2.Window(windowX1, windowY1, windowX2 - windowX1, windowY2 - windowY1);
	auto    mcrop1 = ImageToMat(crop1);
	auto    mcrop2 = ImageToMat(crop2);
	cv::Mat m1, m2;
	cv::cvtColor(mcrop1, m1, cv::COLOR_RGB2GRAY);
	cv::cvtColor(mcrop2, m2, cv::COLOR_RGB2GRAY);

	KeyPointSet        kp1, kp2;
	vector<cv::DMatch> matches;
	ComputeKeyPointsAndMatch("FREAK", m1, m2, 5000, 0.1, 10, false, false, kp1, kp2, matches);

	if (debug) {
		ImageIO imgIO;
		imgIO.SavePngFile("/home/ben/flat1.png", gfx::ImageFormat::RGBA, false, flat1.Width, flat1.Height, flat1.Stride, flat1.Data, 1);
		imgIO.SavePngFile("/home/ben/flat2.png", gfx::ImageFormat::RGBA, false, flat2.Width, flat2.Height, flat2.Stride, flat2.Data, 1);
		cv::Mat outImg;
		cv::drawMatches(mcrop1, kp1.Points, mcrop2, kp2.Points, matches, outImg);
		auto diag = MatToImage(outImg);
		imgIO.SavePngFile("/home/ben/match.png", gfx::ImageFormat::RGBA, false, diag.Width, diag.Height, diag.Stride, diag.Data, 1);
	}

	vector<double> xdelta, ydelta;
	for (const auto& m : matches) {
		auto& p1 = kp1.Points[m.queryIdx];
		auto& p2 = kp2.Points[m.trainIdx];
		xdelta.push_back(p2.pt.x - p1.pt.x);
		ydelta.push_back(p2.pt.y - p1.pt.y);
	}
	auto xstats = math::MeanAndVariance<double, double>(xdelta);
	auto ystats = math::MeanAndVariance<double, double>(ydelta);

	ImageDiffResult res;
	res.MatchCount = matches.size();
	res.XMean      = xstats.first;
	res.YMean      = ystats.first;
	res.XVar       = xstats.second;
	res.YVar       = ystats.second;

	return res;
}

static ImageDiffResult MatchSingleBigBlock(const Image& img1, const Image& img2) {
	// local contrast is essential for uniformly gray roads (ie good roads)
	Image norm1 = img1;
	Image norm2 = img2;
	LocalContrast(norm1, 1, 5);
	LocalContrast(norm2, 1, 5);

	// 0.6 is a thumbsuck. we don't want too much travel (ie vehicle must not travel too fast, frame to frame).
	int maxYDelta = img1.Height * 0.6;
	int maxXDelta = 20;
	// these sizes are arbitrary. bigger = more accurate. smaller = faster.
	int mWidth  = img1.Width * 0.5;
	mWidth      = (mWidth / 8) * 8; // ensure we're 8 pixel aligned, which makes us 8*4 = 32 byte aligned (for SIMD in DiffSum) (VERY IMPORTANT for performance!!)
	int mHeight = 20;
	// "If the vehicle is moving forward, then r2 moves UP to find it's match in r1".
	Rect32 r2((img1.Width - mWidth) / 2, img1.Height - mHeight, 0, 0);
	r2.x2 = r2.x1 + mWidth;
	r2.y2 = r2.y1 + mHeight;
	IMQS_ASSERT(r2.x1 >= 0 && r2.y1 >= 0 && r2.x2 <= img1.Width && r2.y2 <= img1.Height);
	maxYDelta       = min<int>(maxYDelta, r2.y1);
	int64_t bestSum = INT64_MAX;
	for (int dx = -maxXDelta; dx <= maxXDelta; dx++) {
		for (int dy = -maxYDelta; dy <= 0; dy++) {
			Rect32 r1 = r2;
			r1.Offset(dx, dy);
			IMQS_ASSERT(r1.x1 >= 0 && r1.y1 >= 0 && r1.x2 <= img1.Width && r1.y2 <= img1.Height);
			int64_t sum = DiffSum(norm1, norm2, r1, r2);
			if (sum < bestSum)
				bestSum = sum;
		}
	}
	ImageDiffResult res;
	res.OptFlowDiff = (float) bestSum;
	res.MatchCount  = 101;
	res.XVar        = (float) bestSum;
	//tsf::print("diff: %f\n", (double) bestSum);
	return res;
}

static ImageDiffResult ImageDiffOptFlow(MeshRenderer& rend, const Image& img1, const Image& img2, Image& flat1, Image& flat2, Rect32 sensorCrop, PerspectiveParams pp) {
	bool debug = false;

	//float z1         = FindZ1ForIdentityScaleAtBottom(img1.Width, img1.Height, zx, zy);
	auto f          = ComputeFrustum(sensorCrop, pp);
	auto flatOrigin = CameraToFlat(img1.Width, img1.Height, Vec2f(0, 0), pp);
	int  orgX       = (int) flatOrigin.x;
	int  orgY       = (int) flatOrigin.y;

	int fX1     = f.Width / 2 + f.X1 + 2;
	int fX2     = f.Width / 2 + f.X2 - 2;
	int fY1     = f.Height / 2;
	int fY2     = f.Height;
	int fWidth  = fX2 - fX1;
	int fHeight = fY2 - fY1;

	// These alloc's should be null ops for all except the first sample
	flat1.Alloc(ImageFormat::RGBA, fWidth, fHeight);
	flat2.Alloc(ImageFormat::RGBA, fWidth, fHeight);

	rend.RemovePerspectiveAndCopyOut(img1.Window(sensorCrop), nullptr, pp, flat1, Rect32(fX1, fY1, fX2, fY2));
	rend.RemovePerspectiveAndCopyOut(img2.Window(sensorCrop), nullptr, pp, flat2, Rect32(fX1, fY1, fX2, fY2));

	bool useOneBigBlock = true;

	//img1.Window(sensorCrop).SaveFile("/home/ben/r1.png");
	//img2.Window(sensorCrop).SaveFile("/home/ben/r2.png");
	//flat1.SaveFile("/home/ben/p1.png");
	//flat2.SaveFile("/home/ben/p2.png");

	if (useOneBigBlock)
		return MatchSingleBigBlock(flat1, flat2);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// DEAD CODE BELOW
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int         bottomOffset = 30;
	OpticalFlow opt;
	opt.EnableMedianFilter = false;
	opt.SetupSearchDistances(img1.Width);
	// 0.7 is thumbsuck. we don't want frame-to-frame distances to be 0.7 of the frame height. that's too much movement.
	opt.AbsMinVSearch = max(opt.AbsMinVSearch, -int((sensorCrop.Height() - bottomOffset - opt.MatchRadius * 2) * 0.7));
	Mesh mesh;
	mesh.Initialize(10, 2);
	float spacing = opt.MatchRadius * 2.5f;
	int   mY      = flat1.Height - bottomOffset - mesh.Height * spacing;

	for (int y = 0; y < mesh.Height; y++) {
		for (int x = 0; x < mesh.Width; x++) {
			float    xf = ((float) x / (float) (mesh.Width - 1));
			Mesh::VX vx;
			vx.Pos        = Vec2f(xf * (float) flat1.Width, mY + y * spacing);
			vx.UV         = Vec2f(xf * (float) flat1.Width, mY + y * spacing);
			mesh.At(x, y) = vx;
		}
	}

	Vec2f bias(0, 0);
	auto  flow = opt.Frame(mesh, Frustum(), flat2, flat1, bias);
	//exit(1);
	auto avg = mesh.AvgValidDisplacement();

	//mesh.PrintValid();
	//mesh.Print(Rect32(0, 0, mesh.Width, mesh.Height));
	//mesh.PrintDeltaPos(Rect32(0, 0, mesh.Width, mesh.Height));

	//float         xdiff    = 0;
	float         nsamples = 0;
	vector<float> xdiff;
	vector<float> ydiff;
	for (int y = 0; y < mesh.Height; y++) {
		for (int x = 0; x < mesh.Width; x++) {
			if (mesh.At(x, y).IsValid) {
				const auto& vx = mesh.At(x, y);
				//xdiff.push_back();
				//xdiff += fabs(vx.Pos.x - vx.UV.x);
				xdiff.push_back(vx.Pos.x - vx.UV.x);
				ydiff.push_back(vx.Pos.y - vx.UV.y);
				nsamples++;
			}
		}
	}
	//xdiff /= nsamples;

	auto xstats = math::MeanAndVariance<float, float>(xdiff);
	auto ystats = math::MeanAndVariance<float, float>(ydiff);

	ImageDiffResult res;
	res.MatchCount  = 101; // bogus value to satisfy the "good enough match" criteria
	res.XMean       = avg.x;
	res.YMean       = -avg.y; // negate, so that positive numbers indicate forward travel
	res.XVar        = 10 * sqrt(xstats.second) + sqrt(ystats.second) + flow.Diff / 200.0f;
	res.YVar        = 0;
	res.OptFlowDiff = flow.Diff;

	return res;
}

// Downscale the two images a lot, and then return true if their diff exceeds a threshold
// This can incorrectly return FALSE if the road is extremely uniform.
bool IsVehicleMoving(const gfx::Image& img1, const gfx::Image& img2) {
	auto i1 = img1.HalfSizeCheap();
	auto i2 = img2.HalfSizeCheap();
	for (int i = 0; i < 4; i++) {
		i1 = i1.HalfSizeCheap();
		i2 = i2.HalfSizeCheap();
	}
	//i1.SaveJpeg("/home/ben/move1.jpeg");
	//i2.SaveJpeg("/home/ben/move2.jpeg");
	Rect32 r(0, 0, i1.Width, i1.Height);
	double diff = DiffSum(i1, i2, r, r) / (double) (i1.Width * i1.Height);
	// This constant of 4 here was determined by observing a few videos of Mthata.
	// To see stationary examples, find a video where the car was left idling for a while, before
	// it starts to move.
	return diff > 4;
}

static StaticError ErrTooFewGoodFrames("Too few good frames");

// If we have less than this number of valid (ie decent quality) frame pairs, then the entire process is considered a failure
static const int MinValidSamples = 7;

// we store nSamples many pairs of frames in memory
// A 1920x1080 RGBA image takes 8 MB memory. So 20 pairs is 316 MB, 100 pairs is 1.6 GB
static Error EstimateZY(vector<string> videoFiles, int nSamples, bool verbose, FlattenParams& bestParams) {
	video::VideoFile video;
	auto             err = video.OpenFile(videoFiles[0]);
	if (!err.OK())
		return err;

	// seconds apart each sample
	double spacing = video.GetVideoStreamInfo().DurationSeconds() / (nSamples + 1);

	int videoWidth  = video.Width();
	int videoHeight = video.Height();

	if (global::Lens != nullptr) {
		err = global::Lens->InitializeDistortionCorrect(videoWidth, videoHeight);
		if (!err.OK())
			return err;
	}

	if (verbose)
		tsf::print("Loading camera frames\n");
	vector<pair<Image, Image>> samples;
	for (int i = 0; i < nSamples; i++) {
		//auto err = video.SeekToSecond(40);
		auto err = video.SeekToSecond(i * spacing);
		if (!err.OK())
			return err;
		pair<Image, Image> sample;
		sample.first.Alloc(gfx::ImageFormat::RGBA, videoWidth, videoHeight);
		sample.second.Alloc(gfx::ImageFormat::RGBA, videoWidth, videoHeight);
		err = video.DecodeFrameRGBA(videoWidth, videoHeight, sample.first.Data, sample.second.Stride);
		err |= video.DecodeFrameRGBA(videoWidth, videoHeight, sample.second.Data, sample.second.Stride);
		if (!err.OK())
			return err;
		samples.push_back(std::move(sample));
	}

	// count the number of times we've noticed that in this frame the vehicle is not moving fast enough,
	// and also how many times it had too few matches
	vector<int> tooSlow;
	vector<int> tooFew;
	for (size_t i = 0; i < samples.size(); i++) {
		tooFew.push_back(0);
		tooSlow.push_back(0);
	}

	float zx = 0;

	MeshRenderer rend;
	err = rend.Initialize(100, 100);
	if (!err.OK())
		return err;

	if (verbose)
		tsf::print("Estimating on %v samples...\n", nSamples);

	auto estimate = [&](double zy_min, double zy_max, double& zy_best, double& zy_quadratic) -> Error {
		// pairs of zy and X match variance, which we fit a parabola to.
		vector<pair<double, double>> zyAndVar;

		int nsteps = 9;
		for (int step = 0; step < nsteps; step++) {
			PerspectiveParams pp;
			pp.ZY = zy_min + step * (zy_max - zy_min) / (nsteps - 1);
			//tsf::print("zy: %.10f\n", pp.ZY);
			//pp.ZY = -0.0009;

			// This sensor crop value must be consistent with the final value that the code uses, down below.
			auto sensorCrop = ComputeSensorCropDefault(videoWidth, videoHeight, pp);
			// downscale image
			pp.Z1 *= 0.5;
			auto f = ComputeFrustum(sensorCrop, pp);

			err = rend.ResizeFrameBuffer(f.Width, f.Height);
			if (!err.OK())
				return err;

			//tsf::print("zy: %.6f\n", zy);

			bool          useOpticalFlow = true;
			vector<Vec2d> stats;
			float         flowDiff = 0;
			Image         flat1, flat2;
			for (size_t i = 0; i < samples.size(); i++) {
				// once we've seen a frame as been "too slow" or "too few" too often, skip it forever
				if (tooFew[i] >= 5 || tooSlow[i] >= 5) {
					continue;
				}
				const auto& sample = samples[i];

				if (!IsVehicleMoving(sample.first, sample.second)) {
					tooFew[i]++;
					continue;
				}

				ImageDiffResult diff;
				if (useOpticalFlow)
					diff = ImageDiffOptFlow(rend, sample.first, sample.second, flat1, flat2, sensorCrop, pp);
				else
					diff = ImageDiff(rend, sample.first, sample.second, pp.ZX, pp.ZY);

				if (diff.MatchCount < 100) {
					tooFew[i]++;
				} else {
					//score += diff.XVar;
					//nsample++;
					//tsf::print("%3d, %5d, X:(%8.3f, %8.3f), Y(%8.3f, %8.3f) Flow(%8.3f)\n", i, diff.MatchCount, diff.XMean, diff.XVar, diff.YMean, diff.YVar, diff.OptFlowDiff);
					// We need to be moving forward, and YMean predicts this. Typical road speed is around 100 to 300 pixels.
					// [UPDATE] Gigantic perspective factors such as -0.0027 will masquerade here as a zero Y speed, so we can no longer
					// use this as a metric to say that the vehicle is not moving fast enough.
					//if (diff.YMean < 20) {
					//	tooSlow[i]++;
					//} else {
					stats.emplace_back(diff.XVar, diff.YVar);
					flowDiff += diff.OptFlowDiff;
					//}
				}
			}
			if (stats.size() < MinValidSamples) {
				if (verbose)
					tsf::print("Too few valid frames (z1 = %.8f, frames = %v/%v)\n", pp.Z1, stats.size(), samples.size());
				return ErrTooFewGoodFrames;
			}
			sort(stats.begin(), stats.end(), [](Vec2d a, Vec2d b) { return a.size() < b.size(); });
			double score = 0;
			//size_t burn  = stats.size() / 10;
			size_t burn = 0;
			for (size_t i = burn; i < stats.size() - burn; i++) {
				score += stats[i].x;
			}
			double nsample = stats.size() - 2 * burn;
			score /= nsample;
			flowDiff /= nsample;
			zyAndVar.emplace_back(pp.ZY, score);
			if (verbose)
				tsf::print("%.6f,%.3f,%v,%v,%v,[flow diff: %v]\n", pp.ZY, score, nsample, f.Width, f.Height, flowDiff);
			//z2 += 0.00005;
		}

		double a, b, c;
		FitQuadratic(zyAndVar, a, b, c);
		zy_quadratic = -b / (2 * a);
		if (verbose)
			tsf::print("quadratic best: %.6f\n", zy_quadratic);

		double lowestZY  = 0;
		double lowestVar = DBL_MAX;
		for (auto p : zyAndVar) {
			if (p.second < lowestVar) {
				lowestVar = p.second;
				lowestZY  = p.first;
			}
		}
		zy_best = lowestZY;
		//tsf::print("%v %v %v -> %v\n", a, b, c, bestZY);
		return Error();
	};

	double zy_abs_min = -0.0027;
	//double zy_abs_min = -0.001;
	double zy_abs_max = -0.0001;

	double zy_min             = zy_abs_min;
	double zy_max             = zy_abs_max;
	double radiusShrinkFactor = 0.3; // On every iteration, narrow the search window by this much
	for (int iter = 0; iter < 5; iter++) {
		double zyBest      = 0;
		double zyQuadratic = 0;
		auto   err         = estimate(zy_min, zy_max, zyBest, zyQuadratic);
		if (!err.OK())
			return err;
		bool quadraticGood = fabs(zyBest / zyQuadratic - 1) < 0.1;
		if (verbose)
			tsf::print("estimate: %.6f, %.6f (%v)\n", zyBest, zyQuadratic, quadraticGood ? "match" : "no match");
		if (quadraticGood)
			zyBest = zyQuadratic;
		zyBest        = math::Clamp(zyBest, zy_abs_min, zy_abs_max);
		double radius = radiusShrinkFactor * (zy_max - zy_min) / 2;
		zy_min        = zyBest - radius;
		zy_max        = zyBest + radius;
		zy_min        = max(zy_min, zy_abs_min);
		zy_max        = min(zy_max, zy_abs_max);
		// The call here to ComputeSensorCropDefault() must match the call inside estimate()
		bestParams.PP         = PerspectiveParams();
		bestParams.PP.ZY      = (float) zyBest;
		bestParams.SensorCrop = ComputeSensorCropDefault(videoWidth, videoHeight, bestParams.PP);
	}

	return Error();
}

Error DoPerspective(std::vector<std::string> videoFiles, bool verbose, FlattenParams& fp) {
	int nSamples = (int) (MinValidSamples * 1.5);
	//int   nSamples = 30;
	Error err;
	for (; nSamples < 200; nSamples = int(nSamples * 1.5)) {
		err = EstimateZY(videoFiles, nSamples, verbose, fp);
		if (err.OK())
			break;
		else if (err == ErrTooFewGoodFrames)
			continue;
		else if (!err.OK())
			return err;
	}
	if (!err.OK())
		return err;

	tsf::print(R"({"z1":%.6f, "zy":%.6f, "sensorCrop":[%v,%v,%v,%v]})"
	           "\n",
	           fp.PP.Z1, fp.PP.ZY, fp.SensorCrop.x1, fp.SensorCrop.y1, fp.SensorCrop.x2, fp.SensorCrop.y2);
	return Error();
}

int Perspective(argparse::Args& args) {
	//auto f1 = ComputeFrustum(1920, 1080, 1, 0, -0.00180);
	//auto f2 = ComputeFrustum(1920, 1080, 1, 0, -0.00185);
	//auto f3 = ComputeFrustum(1920, 1080, 1, 0, -0.00190);
	//for (float zy = -0.0025f; zy <= 0; zy += 0.0001f) {
	//	auto f = ComputeFrustum(1920, 1080, 1, 0, zy);
	//	tsf::print("%.6f: %v\n", zy, f.Width);
	//}
	/*
	{
		PerspectiveParams pp(1, 0, -0.0028);
		auto              sensorCrop = ComputeSensorCropDefault(1280, 720, pp);
		auto              f1         = ComputeFrustum(sensorCrop, pp);
		f1.DebugPrintParams(pp.Z1, pp.ZX, pp.ZY, sensorCrop.Width(), sensorCrop.Height());
	}
	TestPerspectiveAndFrustum(1, 0, -0.0025);
	TestPerspectiveAndFrustum(1, 0, -0.0013);
	TestPerspectiveAndFrustum(1, 0, -0.0007);
	*/
	//TestPerspectiveAndFrustum(1, 0.00001, -0.0007);
	auto          videoFiles = strings::Split(args.Params[0], ',');
	auto          verbose    = args.Has("verbose");
	FlattenParams fp;
	auto          err = DoPerspective(videoFiles, verbose, fp);
	if (!err.OK()) {
		tsf::print("Error measuring perspective: %v\n", err.Message());
		return 1;
	}
	return 0;
}

} // namespace roadproc
} // namespace imqs