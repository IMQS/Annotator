#pragma once

#include "Rect.h"
#include "Color8.h"

namespace imqs {
namespace gfx {

enum class ImageType {
	Null,
	Png,
	Jpeg,
};

const char* ImageTypeName(ImageType f);
ImageType   ParseImageType(const std::string& f);

enum class ChannelTypes {
	Null,
	Uint8,
	Float32,
};

enum class ImageFormat {
	Null,
	F32_RG,   // Two channels float32
	F32_RGBA, // Four channels float32
	RGBA,     // RGBA not premultipled
	RGBAP,    // RGBA premultipled
	Gray,
};

inline int BytesPerPixel(ImageFormat f) {
	switch (f) {
	case ImageFormat::Null: return 0;
	case ImageFormat::F32_RG: return 2 * sizeof(float);
	case ImageFormat::F32_RGBA: return 4 * sizeof(float);
	case ImageFormat::RGBA: return 4;
	case ImageFormat::RGBAP: return 4;
	case ImageFormat::Gray: return 1;
	}
	return 0;
}

inline ChannelTypes ChannelType(ImageFormat f) {
	switch (f) {
	case ImageFormat::Null: return ChannelTypes::Null;
	case ImageFormat::F32_RG: return ChannelTypes::Float32;
	case ImageFormat::F32_RGBA: return ChannelTypes::Float32;
	case ImageFormat::RGBA: return ChannelTypes::Uint8;
	case ImageFormat::RGBAP: return ChannelTypes::Uint8;
	case ImageFormat::Gray: return ChannelTypes::Uint8;
	}
	return ChannelTypes::Null;
}

inline int NumChannels(ImageFormat f) {
	switch (f) {
	case ImageFormat::Null: return 0;
	case ImageFormat::F32_RG: return 2;
	case ImageFormat::F32_RGBA: return 4;
	case ImageFormat::RGBA: return 4;
	case ImageFormat::RGBAP: return 4;
	case ImageFormat::Gray: return 1;
	}
	return 0;
}

// Photoshop switches from 444 to 420 when you go from 51% to 50% on the quality slider, so it
// makes sense to make the default 444, because we're usually interested in images in the
// upper quality range.
enum class JpegSampling {
	Samp444  = TJSAMP_444,
	Samp422  = TJSAMP_422,
	Samp420  = TJSAMP_420,
	SampGray = TJSAMP_GRAY,
	Samp440  = TJSAMP_440,
	Samp411  = TJSAMP_411,
};

// An image in memory.
// The buffer is by default owned by the Image object, but it doesn't need to be.
// There is no reference counting, so if you construct a window from an image, then
// you must ensure that the original object outlives the window object.
class Image {
public:
	enum ContructMode {
		ConstructCopy,          // Allocate new buffer that is owned by Image, and copy data into it
		ConstructWindow,        // Assign the data, but do not take ownership of it (ie do not free it)
		ConstructTakeOwnership, // Assume that we now own the given buffer, and will take care of freeing it
	};

	uint8_t*    Data    = nullptr;
	int         Stride  = 0;
	int         Width   = 0;
	int         Height  = 0;
	ImageFormat Format  = ImageFormat::Null;
	bool        OwnData = true;
	bool        Locked  = false; // If true, then any attempt to realloc the memory will cause a runtime assert

	Image() {}
	Image(ImageFormat format, ContructMode mode, int stride, void* data, int width, int height);
	Image(ImageFormat format, int width, int height, int stride = 0);
	Image(const Image& b);
	Image(Image&& b);
	~Image();

	Image& operator=(const Image& b);
	Image& operator=(Image&& b);

	void  Reset();                                                          // Free memory, and reset all fields
	void  Alloc(ImageFormat format, int width, int height, int stride = 0); // Allocate memory and initialize data structure
	Image Window(int x, int y, int width, int height) const;                // Returns a window into Image, at the specified rectangle. Does not copy memory. Parent must outlive window.
	Image Window(Rect32 rect) const;                                        // Returns a window into Image, at the specified rectangle. Does not copy memory. Parent must outlive window.
	void  Fill(Color8 color);
	void  Fill(Rect32 rect, Color8 color);
	Image AsType(ImageFormat fmt) const;
	Image HalfSizeCheap() const;                                          // Downscale by 1/2, in gamma/sRGB space (this is why it's labeled cheap. correct downscale is in linear space, not sRGB)
	Image HalfSizeLinear() const;                                         // Downscale by 1/2, in linear light space. Slower, but correct.
	Image HalfSizeSIMD() const;                                           // Downscale by 1/2, in gamma/sRGB space, using SIMD
	void  BoxBlur(int size, int iterations);                              // Box blur of size [1 + 2 * size], repeated 'iterations' times
	void  CopyFrom(const Image& src);                                     // Copies as much from src into this as possible
	void  CopyFrom(const Image& src, Rect32 srcRect, Rect32 dstRect);     // Source and destination rectangles are clipped before copying, but they must be equal in size
	void  CopyFrom(const Image& src, Rect32 srcRect, int dstX, int dstY); // Source rectangle is clipped before copying
	bool  IsAlphaUniform(uint8_t& alpha) const;                           // Read every alpha value, and if it's all the same, then set alpha to that value and return true. Otherwise, return false.
	Image ExtractAlpha() const;                                           // Create a Gray image that contains only our alpha channel
	void  BlendOnto(Image& dst) const;                                    // Blend this image onto 'dst. Images must be the same size, and RGBA, or the function will panic

	Error LoadFile(const std::string& filename);
	Error LoadBuffer(const void* buffer, size_t size);
	Error SavePng(const std::string& filename, bool withAlpha = true, int zlibLevel = 5) const;
	Error SaveJpeg(const std::string& filename, int quality = 90, JpegSampling sampling = JpegSampling::Samp444) const;
	Error SaveFile(const std::string& filename) const;

	uint8_t*        At(int x, int y) { return Data + (y * Stride) + x * BytesPerPixel(); }
	const uint8_t*  At(int x, int y) const { return Data + (y * Stride) + x * BytesPerPixel(); }
	uint32_t*       At32(int x, int y) { return (uint32_t*) (Data + (y * Stride) + x * BytesPerPixel()); }
	const uint32_t* At32(int x, int y) const { return (const uint32_t*) (Data + (y * Stride) + x * BytesPerPixel()); }
	float*          AtF32(int x, int y) { return (float*) (Data + (y * Stride) + x * BytesPerPixel()); }
	const float*    AtF32(int x, int y) const { return (const float*) (Data + (y * Stride) + x * BytesPerPixel()); }
	uint8_t*        Line(int y) { return Data + (y * Stride); }
	const uint8_t*  Line(int y) const { return Data + (y * Stride); }
	uint32_t*       Line32(int y) { return (uint32_t*) (Data + (y * Stride)); }
	const uint32_t* Line32(int y) const { return (const uint32_t*) (Data + (y * Stride)); }
	int             BytesPerPixel() const { return gfx::BytesPerPixel(Format); }
	int             NumChannels() const { return gfx::NumChannels(Format); }
	ChannelTypes    ChannelType() const { return gfx::ChannelType(Format); }
	size_t          BytesPerLine() const { return gfx::BytesPerPixel(Format) * Width; }
};


} // namespace gfx
} // namespace imqs
