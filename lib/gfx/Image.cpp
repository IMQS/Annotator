#include "pch.h"
#include "Image.h"
#include "ImageIO.h"
#include "Blend.h"

using namespace std;

namespace imqs {
namespace gfx {

// This macro provides backwards compatibility for older Microsoft VS and Linux distributions
// which does not support the _mm_loadu_si64 intrinsic.
#ifndef _mm_loadu_si64
#define _mm_loadu_si64(p) _mm_loadl_epi64((__m128i const*) (p))
#endif

const char* ImageTypeName(ImageType f) {
	switch (f) {
	case ImageType::Null: return "null";
	case ImageType::Jpeg: return "jpeg";
	case ImageType::Png: return "png";
	}
	return "";
}

ImageType ParseImageType(const std::string& f) {
	if (strings::eqnocase(f, "jpeg") || strings::eqnocase(f, "jpg"))
		return ImageType::Jpeg;
	else if (strings::eqnocase(f, "png"))
		return ImageType::Png;
	else
		return ImageType::Null;
}

Image::Image(ImageFormat format, ContructMode mode, int stride, void* data, int width, int height) {
	if (mode == ConstructCopy) {
		Alloc(format, width, height, stride);
		uint8_t* src = (uint8_t*) data;
		for (int y = 0; y < height; y++) {
			memcpy(Line(y), src + (size_t) y * (size_t) stride, BytesPerPixel() * width);
		}
	} else if (mode == ConstructWindow || mode == ConstructTakeOwnership) {
		IMQS_ASSERT(stride != 0);
		Format  = format;
		Data    = (uint8_t*) data;
		Width   = width;
		Height  = height;
		Stride  = stride;
		OwnData = mode == ConstructTakeOwnership;
	}
}

Image::Image(ImageFormat format, int width, int height, int stride) {
	Alloc(format, width, height, stride);
}

Image::Image(const Image& b) {
	*this = b;
}

Image::Image(Image&& b) {
	*this = std::move(b);
}

Image::~Image() {
	if (OwnData)
		free(Data);
}

Image& Image::operator=(const Image& b) {
	if (this != &b) {
		if (OwnData && Format == b.Format && Width == b.Width && Height == b.Height && Stride == b.Stride) {
			// No need to re-alloc
		} else {
			Reset();
			Alloc(b.Format, b.Width, b.Height);
		}
		for (int y = 0; y < Height; y++)
			memcpy(Line(y), b.Line(y), BytesPerLine());
	}
	return *this;
}

Image& Image::operator=(Image&& b) {
	if (this != &b) {
		Reset();
		memcpy(this, &b, sizeof(b));
		memset(&b, 0, sizeof(b));
	}
	return *this;
}

void Image::Reset() {
	IMQS_ASSERT(!Locked);
	if (OwnData)
		free(Data);
	Width   = 0;
	Height  = 0;
	Stride  = 0;
	Data    = nullptr;
	OwnData = false;
	Format  = ImageFormat::Null;
}

void Image::Alloc(ImageFormat format, int width, int height, int stride) {
	if (stride == 0) {
		stride = gfx::BytesPerPixel(format) * width;
		stride = 4 * ((stride + 3) / 4); // round up to multiple of 4 bytes
		if (stride != gfx::BytesPerPixel(format) * width)
			tsf::print("stride up from %v to %v\n", gfx::BytesPerPixel(format) * width, stride);
	}

	IMQS_ASSERT(stride >= gfx::BytesPerPixel(format) * width);

	if (Width == width && Height == height && Stride == stride && OwnData) {
		Format = format;
		return;
	}

	Reset();

	OwnData = true;
	Width   = width;
	Height  = height;
	Stride  = stride;
	Format  = format;
	Data    = (uint8_t*) imqs_malloc_or_die(Height * std::abs(Stride));
}

Image Image::Window(int x, int y, int width, int height) const {
	IMQS_ASSERT(width >= 0);
	IMQS_ASSERT(height >= 0);
	IMQS_ASSERT(x >= 0);
	IMQS_ASSERT(y >= 0);
	IMQS_ASSERT(x + width <= Width);
	IMQS_ASSERT(y + height <= Height);
	return Image(Format, ConstructWindow, Stride, const_cast<uint8_t*>(At(x, y)), width, height);
}

Image Image::Window(Rect32 rect) const {
	return Window(rect.x1, rect.y1, rect.Width(), rect.Height());
}

void Image::Fill(Color8 color) {
	Fill(Rect32(0, 0, Width, Height), color);
}

void Image::Fill(Rect32 rect, Color8 color) {
	IMQS_ASSERT(ChannelType() == ChannelTypes::Uint8);
	rect.x1        = math::Clamp(rect.x1, 0, Width);
	rect.y1        = math::Clamp(rect.y1, 0, Height);
	rect.x2        = math::Clamp(rect.x2, 0, Width);
	rect.y2        = math::Clamp(rect.y2, 0, Height);
	uint8_t gray   = color.Lum();
	bool    isGray = NumChannels() == 1;
	for (int y = rect.y1; y < rect.y2; y++) {
		uint32_t* dst = At32(rect.x1, y);
		size_t    x2  = rect.x2;
		if (isGray) {
			for (size_t x = rect.x1; x < x2; x++)
				*dst++ = gray;
		} else {
			for (size_t x = rect.x1; x < x2; x++)
				*dst++ = color.u;
		}
	}
}

#define MAKE_IMAGE_FORMAT_PAIR(a, b) (((uint32_t) a << 16) | (uint32_t) b)

Image Image::AsType(ImageFormat fmt) const {
	Image r;
	r.Alloc(fmt, Width, Height);
	uint32_t combo = MAKE_IMAGE_FORMAT_PAIR(Format, fmt);
	switch (combo) {
	case MAKE_IMAGE_FORMAT_PAIR(ImageFormat::RGBAP, ImageFormat::Gray):
	case MAKE_IMAGE_FORMAT_PAIR(ImageFormat::RGBA, ImageFormat::Gray):
		for (int y = 0; y < Height; y++) {
			size_t         w   = Width;
			const uint8_t* src = Line(y);
			uint8_t*       dst = r.Line(y);
			for (size_t x = 0; x < w; x++) {
				dst[0] = Color8(src[0], src[1], src[2], src[3]).Lum();
				dst += 1;
				src += 4;
			}
		}
		break;
	case MAKE_IMAGE_FORMAT_PAIR(ImageFormat::Gray, ImageFormat::RGBA):
	case MAKE_IMAGE_FORMAT_PAIR(ImageFormat::Gray, ImageFormat::RGBAP):
		for (int y = 0; y < Height; y++) {
			size_t         w   = Width;
			const uint8_t* src = Line(y);
			uint8_t*       dst = r.Line(y);
			for (size_t x = 0; x < w; x++) {
				dst[0] = src[0];
				dst[1] = src[0];
				dst[2] = src[0];
				dst[3] = 255;
				dst += 4;
				src += 1;
			}
		}
		break;
	default:
		IMQS_DIE();
	}
	return r;
}

Image Image::HalfSizeSIMD() const {
	IMQS_ASSERT(Format == ImageFormat::RGBA);

	Image dstImg;
	dstImg.Alloc(Format, Width / 2, Height / 2);
	unsigned outHeight = dstImg.Height;
	unsigned outWidth  = dstImg.Width;

	// expand two pixels in the lower 64-bits of a register (..AB), into (.Ar.Ag.Ab.Aa.Br.Bg.Bb.Ba)
	uint8_t m_expand8to16[16] = {7, 128, 6, 128, 5, 128, 4, 128, 3, 128, 2, 128, 1, 128, 0, 128};
	__m128i expand8To16       = _mm_loadu_si128((__m128i*) m_expand8to16);

	// pack
	uint8_t m_pack16to8[16] = {6, 4, 2, 0, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};
	__m128i pack16to8       = _mm_loadu_si128((__m128i*) m_pack16to8);

	for (unsigned y = 0; y < outHeight; y++) {
		const uint8_t* srcTop    = Line(y * 2);
		const uint8_t* srcBottom = Line(y * 2 + 1);
		uint8_t*       dst       = dstImg.Line(y);
		for (unsigned x = 0; x < outWidth; x++) {
			// Each pixel is 32 bits, and an SSE register0x10 is 128 bits, so that's 4 pixels.
			// We want only 2 pixels in each SSE register, so that we can add them together without overflowing
			// So we want to load 2 pixels from the top, then expand 8 to 16 bits, and then same for the bottom row.
			__m128i top    = _mm_loadu_si64(srcTop);
			__m128i bottom = _mm_loadu_si64(srcBottom);

			top    = _mm_shuffle_epi8(top, expand8To16);
			bottom = _mm_shuffle_epi8(bottom, expand8To16);
			// sum of bottom and top.
			__m128i sum = _mm_add_epi16(top, bottom);
			// we now want to sum the left and right halves of sum together
			// create a copy of sum with left and right swapped
			__m128i sumSwap = _mm_shuffle_epi32(sum, 0x4E);
			sum             = _mm_add_epi16(sum, sumSwap);
			// both sides of sum now contain the sum of the 4 pixels.
			// divide by 4.
			sum = _mm_srai_epi16(sum, 2);
			// we now have our final RGBA values, but they're in 16 registers. We need to pack them down to 8 bits.
			sum = _mm_shuffle_epi8(sum, pack16to8);
			// clang 7 doesn't have _mm_storeu_si32, so we need to store the register first, and then copy to 32-bit output. weird!
			//_mm_storeu_si32(dst, sum);
			uint32_t tmp[4];
			_mm_storeu_si128((__m128i*) tmp, sum);
			*((uint32_t*) dst) = tmp[0];
			srcTop += 8;
			srcBottom += 8;
			dst += 4;
		}
	}
	return dstImg;
}

Image Image::HalfSizeCheap() const {
	Image half;
	half.Alloc(Format, Width / 2, Height / 2);
	for (int y = 0; y < half.Height; y++) {
		auto   srcA = Line(y * 2);     // top line
		auto   srcB = Line(y * 2 + 1); // bottom line
		auto   dstP = half.Line(y);
		size_t dstW = half.Width;
		if (NumChannels() == 4) {
			for (size_t x = 0; x < dstW; x++) {
				uint32_t r = ((uint32_t) srcA[0] + (uint32_t) srcA[4] + (uint32_t) srcB[0] + (uint32_t) srcB[4]) >> 2;
				uint32_t g = ((uint32_t) srcA[1] + (uint32_t) srcA[5] + (uint32_t) srcB[1] + (uint32_t) srcB[5]) >> 2;
				uint32_t b = ((uint32_t) srcA[2] + (uint32_t) srcA[6] + (uint32_t) srcB[2] + (uint32_t) srcB[6]) >> 2;
				uint32_t a = ((uint32_t) srcA[3] + (uint32_t) srcA[7] + (uint32_t) srcB[3] + (uint32_t) srcB[7]) >> 2;
				dstP[0]    = r;
				dstP[1]    = g;
				dstP[2]    = b;
				dstP[3]    = a;
				srcA += 8;
				srcB += 8;
				dstP += 4;
			}
		} else if (NumChannels() == 1) {
			for (size_t x = 0; x < dstW; x++) {
				dstP[0] = ((uint32_t) srcA[0] + (uint32_t) srcA[1] + (uint32_t) srcB[0] + (uint32_t) srcB[1]) >> 2;
				srcA += 2;
				srcB += 2;
				dstP += 1;
			}
		} else {
			IMQS_DIE();
		}
	}
	return half;
}

// This is crazy slow. It needs to be vectorized.
Image Image::HalfSizeLinear() const {
	Image half;
	half.Alloc(Format, Width / 2, Height / 2);
	for (int y = 0; y < half.Height; y++) {
		auto   srcA = Line(y * 2);     // top line
		auto   srcB = Line(y * 2 + 1); // bottom line
		auto   dstP = half.Line(y);
		size_t dstW = half.Width;
		if (NumChannels() == 4) {
			for (size_t x = 0; x < dstW; x++) {
				float    r = 0.25f * (Color8::SRGBtoLinearU8(srcA[0]) + Color8::SRGBtoLinearU8(srcA[4]) + Color8::SRGBtoLinearU8(srcB[0]) + Color8::SRGBtoLinearU8(srcB[4]));
				float    g = 0.25f * (Color8::SRGBtoLinearU8(srcA[1]) + Color8::SRGBtoLinearU8(srcA[5]) + Color8::SRGBtoLinearU8(srcB[1]) + Color8::SRGBtoLinearU8(srcB[5]));
				float    b = 0.25f * (Color8::SRGBtoLinearU8(srcA[2]) + Color8::SRGBtoLinearU8(srcA[6]) + Color8::SRGBtoLinearU8(srcB[2]) + Color8::SRGBtoLinearU8(srcB[6]));
				uint32_t a = ((uint32_t) srcA[3] + (uint32_t) srcA[7] + (uint32_t) srcB[3] + (uint32_t) srcB[7]) >> 2;
				dstP[0]    = Color8::LinearToSRGBU8(r);
				dstP[1]    = Color8::LinearToSRGBU8(g);
				dstP[2]    = Color8::LinearToSRGBU8(b);
				dstP[3]    = a;
				srcA += 8;
				srcB += 8;
				dstP += 4;
			}
		} else if (NumChannels() == 1) {
			for (size_t x = 0; x < dstW; x++) {
				float a = Color8::SRGBtoLinearU8(srcA[0]);
				float b = Color8::SRGBtoLinearU8(srcA[1]);
				float c = Color8::SRGBtoLinearU8(srcB[0]);
				float d = Color8::SRGBtoLinearU8(srcB[1]);
				a       = 0.25f * (a + b + c + d);
				dstP[0] = Color8::LinearToSRGBU8(a);
				srcA += 2;
				srcB += 2;
				dstP += 1;
			}
		} else {
			IMQS_DIE();
		}
	}
	return half;
}

struct Color16 {
	union {
		struct
		{
#if ENDIANLITTLE
			uint16_t a, b, g, r;
#else
			uint16_t r : 16;
			uint16_t g : 16;
			uint16_t b : 16;
			uint16_t a : 16;
#endif
		};
		uint64_t u;
	};
	Color16() {}
	Color16(Color8 x) : r(x.r), g(x.g), b(x.b), a(x.a) {}
	Color16(uint16_t r, uint16_t g, uint16_t b, uint16_t a) : r(r), g(g), b(b), a(a) {}

	Color8 ToColor8() const { return Color8(r, g, b, a); }
	operator Color8() const { return Color8(r, g, b, a); }
};

inline Color16 operator/(Color16 x, uint16_t div) {
	return Color16(x.r / div, x.g / div, x.b / div, x.a / div);
}

inline Color16 operator+(Color16 x, Color16 y) {
	return Color16(x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a);
}

inline Color16 operator-(Color16 x, Color16 y) {
	return Color16(x.r - y.r, x.g - y.g, x.b - y.b, x.a - y.a);
}

static void BoxBlurGray(uint8_t* src, uint8_t* dst, int width, int blurSize, int iterations) {
	IMQS_ASSERT(blurSize == 1); // just haven't bothered to code up other sizes, and it's only a hassle because of the left/right edge cases
	for (int iter = 0; iter < iterations; iter++) {
		unsigned sum = 0;
		if (blurSize == 1) {
			dst[0] = ((unsigned) src[0] + (unsigned) src[1]) >> 1;
			dst[1] = ((unsigned) src[0] + (unsigned) src[1] + (unsigned) src[2]) / 3;
			sum    = (unsigned) src[0] + (unsigned) src[1] + (unsigned) src[2];
		}
		size_t x = blurSize + 1;
		size_t w = width - blurSize;
		for (; x < w; x++) {
			sum    = sum - src[x - 2] + src[x + 1];
			dst[x] = sum / 3;
		}
		if (blurSize == 1) {
			sum    = sum - src[x - 2] + src[w - 1];
			dst[x] = sum / 3;
		}
		std::swap(src, dst);
	}
}

static void BlurRGBAPixel(int n, Color8* src, Color8* dst, libdivide::divider<int>& divider) {
	auto sum = _mm_setzero_si128();
	for (int i = 0; i < n; i++)
		sum = _mm_add_epi32(sum, _mm_cvtepu8_epi32(_mm_cvtsi32_si128((int) src[i].u)));
	sum /= divider;
	sum      = _mm_packs_epi32(sum, _mm_setzero_si128());
	sum      = _mm_packus_epi16(sum, _mm_setzero_si128());
	dst[0].u = _mm_cvtsi128_si32(sum);
}

static void BoxBlurRGBA(Color8* src, Color8* dst, int width, int blurSize, int iterations, libdivide::divider<int>* dividers) {
	for (int iter = 0; iter < iterations; iter++) {
		for (int i = 0; i < blurSize + 1; i++) {
			BlurRGBAPixel(i + 2, src, dst + i, dividers[i + 2]);
		}
		__m128i sum = _mm_setzero_si128();
		for (int i = 0; i < 2 * blurSize + 1; i++) {
			sum = _mm_add_epi32(sum, _mm_cvtepu8_epi32(_mm_cvtsi32_si128((int) src[i].u)));
		}
		size_t      x           = blurSize + 1;
		size_t      w           = width - blurSize;
		const auto& mainDivider = dividers[1 + blurSize * 2];
		for (; x < w; x++) {
			auto vOld = _mm_cvtsi32_si128((int) src[x - blurSize - 1].u);
			auto vNew = _mm_cvtsi32_si128((int) src[x + blurSize].u);
			vOld      = _mm_cvtepu8_epi32(vOld);
			vNew      = _mm_cvtepu8_epi32(vNew);
			sum       = _mm_sub_epi32(sum, vOld); // subtract outgoing value
			sum       = _mm_add_epi32(sum, vNew); // add incoming value
			auto out  = sum;
			out       = out / mainDivider; // divide by blurSize
			out       = _mm_packs_epi32(out, _mm_setzero_si128());
			out       = _mm_packus_epi16(out, _mm_setzero_si128());
			dst[x].u  = _mm_cvtsi128_si32(out);
		}
		//dst[x] = (Color16(src[w - 2]) + Color16(src[w - 1])) / 2;
		//BlurRGBAPixel(2, src + w - 2, dst + width - 1, dividers[2]);
		//BlurRGBAPixel(3, src + w - 3, dst + width - 2, dividers[3]);
		for (int i = 0; i < blurSize; i++) {
			BlurRGBAPixel(i + 2, src + w - blurSize - 1 - i, dst + width - 1 - i, dividers[i + 2]);
		}
		std::swap(src, dst);
	}
} // namespace gfx

void Image::BoxBlur(int size, int iterations) {
	IMQS_ASSERT(NumChannels() == 1 || NumChannels() == 4);
	IMQS_ASSERT(Width >= size * 2 + 1);
	IMQS_ASSERT(Height >= size * 2 + 1);
	bool isRGBA = NumChannels() == 4;

	uint8_t* buf1 = (uint8_t*) imqs_malloc_or_die((max(Width, Height) + 1) * NumChannels());
	uint8_t* buf2 = (uint8_t*) imqs_malloc_or_die((Height + 1) * NumChannels());

	// these are just sentinels to make sure we're not overwriting memory
	buf1[max(Width, Height) * NumChannels()] = 254;
	buf2[Height * NumChannels()]             = 254;

	bool evenIterations = (unsigned) iterations % 2 == 0;

	// dividers[2] divides by 2
	// dividers[3] divides by 3, etc
	int                      maxDivider = size * 2 + 1;
	libdivide::divider<int>* dividers   = new libdivide::divider<int>[maxDivider + 1];
	for (int i = 2; i <= maxDivider; i++) {
		dividers[i] = libdivide::divider<int>(i);
	}

	for (int y = 0; y < Height; y++) {
		uint8_t* src = Line(y);
		if (isRGBA)
			BoxBlurRGBA((Color8*) src, (Color8*) buf1, Width, size, iterations, dividers);
		else
			BoxBlurGray(src, buf1, Width, size, iterations);
		if (!evenIterations)
			memcpy(src, buf1, Width * NumChannels());
	}

	for (int x = 0; x < Width; x++) {
		// For the verticals, first copy each line into a buffer, so that we can run multiple iterations fast
		size_t h      = Height;
		int    stride = Stride;
		if (isRGBA) {
			uint32_t* src = At32(x, 0);
			uint32_t* buf = (uint32_t*) buf1;
			for (size_t y = 0; y < h; y++, (char*&) src += Stride)
				buf[y] = *src;
		} else {
			uint8_t* src = At(x, 0);
			for (size_t y = 0; y < h; y++, src += Stride)
				buf1[y] = *src;
		}

		if (isRGBA)
			BoxBlurRGBA((Color8*) buf1, (Color8*) buf2, Height, size, iterations, dividers);
		else
			BoxBlurGray(buf1, buf2, Height, size, iterations);

		// copy back out
		if (isRGBA) {
			uint32_t* src   = At32(x, 0);
			uint32_t* final = evenIterations ? (uint32_t*) buf1 : (uint32_t*) buf2;
			for (size_t y = 0; y < h; y++, (char*&) src += Stride)
				*src = final[y];
		} else {
			uint8_t* src   = At(x, 0);
			uint8_t* final = evenIterations ? buf1 : buf2;
			for (size_t y = 0; y < h; y++, src += Stride)
				*src = final[y];
		}
	}

	IMQS_ASSERT(buf1[max(Width, Height) * NumChannels()] == 254); // sentils to make sure we haven't overwritten memory
	IMQS_ASSERT(buf2[Height * NumChannels()] == 254);

	free(buf1);
	free(buf2);
}

void Image::CopyFrom(const Image& src) {
	CopyFrom(src, Rect32(0, 0, src.Width, src.Height), 0, 0);
}

void Image::CopyFrom(const Image& src, Rect32 srcRect, Rect32 dstRect) {
	IMQS_ASSERT(srcRect.Width() == dstRect.Width());
	IMQS_ASSERT(srcRect.Height() == dstRect.Height());
	IMQS_ASSERT(src.Format == Format);

	if (srcRect.x1 < 0) {
		dstRect.x1 -= srcRect.x1;
		srcRect.x1 = 0;
	}
	if (srcRect.y1 < 0) {
		dstRect.y1 -= srcRect.y1;
		srcRect.y1 = 0;
	}
	if (srcRect.x2 > src.Width) {
		dstRect.x2 -= srcRect.x2 - src.Width;
		srcRect.x2 = src.Width;
	}
	if (srcRect.y2 > src.Height) {
		dstRect.y2 -= srcRect.y2 - src.Height;
		srcRect.y2 = src.Height;
	}

	if (dstRect.x1 < 0) {
		srcRect.x1 -= dstRect.x1;
		dstRect.x1 = 0;
	}
	if (dstRect.y1 < 0) {
		srcRect.y1 -= dstRect.y1;
		dstRect.y1 = 0;
	}
	if (dstRect.x2 > Width) {
		srcRect.x2 -= dstRect.x2 - Width;
		dstRect.x2 = Width;
	}
	if (dstRect.y2 > Height) {
		srcRect.y2 -= dstRect.y2 - Height;
		dstRect.y2 = Height;
	}

	if (srcRect.Width() <= 0 || srcRect.Height() <= 0)
		return;

	IMQS_ASSERT(srcRect.x1 >= 0 && srcRect.x2 <= src.Width && srcRect.y1 >= 0 && srcRect.y2 <= src.Height);
	IMQS_ASSERT(dstRect.x1 >= 0 && dstRect.x2 <= Width && dstRect.y1 >= 0 && dstRect.y2 <= Height);

	for (int y = 0; y < srcRect.Height(); y++)
		memcpy(At(dstRect.x1, dstRect.y1 + y), src.At(srcRect.x1, srcRect.y1 + y), srcRect.Width() * BytesPerPixel());
}

void Image::CopyFrom(const Image& src, Rect32 srcRect, int dstX, int dstY) {
	CopyFrom(src, srcRect, Rect32(dstX, dstY, dstX + srcRect.Width(), dstY + srcRect.Height()));
}

bool Image::IsAlphaUniform(uint8_t& _alpha) const {
	if (Height == 0)
		return false;
	if (Format == ImageFormat::RGBA || Format == ImageFormat::RGBAP) {
		uint8_t alpha = Line(0)[3];
		for (int y = 0; y < Height; y++) {
			auto p     = Line(y) + 3;
			int  width = Width;
			for (int x = 0; x < width; x++) {
				if (*p != alpha)
					return false;
				p += 4;
			}
		}
		_alpha = alpha;
		return true;
	}
	return false;
}

Image Image::ExtractAlpha() const {
	IMQS_ASSERT(Format == ImageFormat::RGBA || Format == ImageFormat::RGBAP);
	Image alpha;
	alpha.Alloc(ImageFormat::Gray, Width, Height);
	for (int y = 0; y < Height; y++) {
		int  width = Width;
		auto src   = Line(y) + 3;
		auto dst   = alpha.Line(y);
		for (int x = 0; x < width; x++) {
			*dst = *src;
			src += 4;
			dst += 1;
		}
	}
	return alpha;
}

void Image::BlendOnto(Image& dst) const {
	IMQS_ASSERT(Format == ImageFormat::RGBA || Format == ImageFormat::RGBAP);
	IMQS_ASSERT(dst.Format == ImageFormat::RGBA || dst.Format == ImageFormat::RGBAP);
	for (int y = 0; y < Height; y++) {
		int  width = Width;
		auto srcP  = (const Color8*) Line32(y);
		auto dstP  = (Color8*) dst.Line32(y);
		if (Format == ImageFormat::RGBA) {
			// multiply, then blend
			for (int x = 0; x < width; x++, srcP++, dstP++)
				BlendOver(srcP->Premultipied().u, (uint32_t*) dstP);
		} else {
			// just blend
			for (int x = 0; x < width; x++, srcP++, dstP++)
				BlendOver(srcP->u, (uint32_t*) dstP);
		}
	}
}

Error Image::LoadFile(const std::string& filename) {
	ImageIO io;
	string  raw;
	auto    err = os::ReadWholeFile(filename, raw);
	int     w   = 0;
	int     h   = 0;
	void*   buf = nullptr;
	err         = io.Load(raw.data(), raw.size(), w, h, buf);
	if (!err.OK())
		return err;
	*this = Image(ImageFormat::RGBA, ConstructTakeOwnership, w * 4, buf, w, h);
	return Error();
}

Error Image::LoadBuffer(const void* buffer, size_t size) {
	ImageIO io;
	int     w   = 0;
	int     h   = 0;
	void*   buf = nullptr;
	auto    err = io.Load(buffer, size, w, h, buf);
	if (!err.OK())
		return err;
	*this = Image(ImageFormat::RGBA, ConstructTakeOwnership, w * 4, buf, w, h);
	return Error();
}

Error Image::SavePng(const std::string& filename, bool withAlpha, int zlibLevel) const {
	return ImageIO::SavePngFile(filename, Format, withAlpha, Width, Height, Stride, Data, zlibLevel);
}

Error Image::SaveJpeg(const std::string& filename, int quality, JpegSampling sampling) const {
	if (Format == ImageFormat::Gray)
		sampling = JpegSampling::SampGray;
	return ImageIO::SaveJpegFile(filename, Format, Width, Height, Stride, Data, quality, sampling);
}

Error Image::SavePngBuffer(std::string& buffer, bool withAlpha, int zlibLevel) const {
	ImageIO io;
	void*   enc  = nullptr;
	size_t  size = 0;
	auto    err  = io.SavePng(Format, withAlpha, Width, Height, Stride, Data, zlibLevel, enc, size);
	if (!err.OK())
		return err;
	buffer.assign((const char*) enc, size);
	io.FreeEncodedBuffer(gfx::ImageType::Png, enc);
	return Error();
}

Error Image::SaveJpegBuffer(std::string& buffer, int quality, JpegSampling sampling) const {
	ImageIO io;
	void*   enc  = nullptr;
	size_t  size = 0;
	auto    err  = io.SaveJpeg(Format, Width, Height, Stride, Data, quality, sampling, enc, size);
	if (!err.OK())
		return err;
	buffer.assign((const char*) enc, size);
	io.FreeEncodedBuffer(gfx::ImageType::Jpeg, enc);
	return Error();
}

Error Image::SaveFile(const std::string& filename) const {
	auto ext = strings::tolower(path::Extension(filename));
	if (ext == ".png")
		return SavePng(filename, true, 1);
	else if (ext == ".jpeg" || ext == ".jpg")
		return SaveJpeg(filename);
	else
		return Error::Fmt("Unknown image file format '%v'", ext);
}

} // namespace gfx
} // namespace imqs
