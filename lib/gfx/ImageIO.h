#pragma once

#include "Image.h"

typedef void* tjhandle;

namespace imqs {
namespace gfx {

// ImageIO is cheap to instantiate, but it does cache some state, like libjpegturbo "decompressor",
// so do not share an ImageIO instance between threads.
class ImageIO {
public:
	tjhandle JpegDecomp  = nullptr;
	tjhandle JpegEncoder = nullptr;

	ImageIO();
	~ImageIO();

	// Save to an image format.
	// lossy Q is applicable to jpeg
	// lossless Q is applicable to png
	Error Save(ImageFormat format, int width, int height, int stride, const void* buf, ImageType type, bool withAlpha, int lossyQ_0_to_100, int losslessQ_1_to_9, void*& encBuf, size_t& encSize);

	// Decodes a png or jpeg image into an RGBA memory buffer
	Error Load(const void* encBuf, size_t encLen, int& width, int& height, void*& buf);

	// Free an encoded buffer. The jpeg-turbo compressor uses a special allocator, so we need to free it specially too.
	static void FreeEncodedBuffer(ImageType type, void* encBuf);

	// Decodes a png image into an RGBA memory buffer
	Error LoadPng(const void* pngBuf, size_t pngLen, int& width, int& height, void*& buf);

	// Encode png
	Error SavePng(bool withAlpha, int width, int height, int stride, const void* buf, int zlibLevel, void*& encBuf, size_t& encSize);

	// Save png to file
	static Error SavePngFile(const std::string& filename, bool withAlpha, int width, int height, int stride, const void* buf, int zlibLevel);

	// Reads only the metadata out of a jpeg image
	Error LoadJpegHeader(const void* jpegBuf, size_t jpegLen, int* width, int* height, JpegSampling* sampling = nullptr, bool* isColor = nullptr);

	// Reads only the metadata out of a jpeg file
	Error LoadJpegFileHeader(const std::string& filename, int* width, int* height, JpegSampling* sampling = nullptr, bool* isColor = nullptr);

	// Decodes a jpeg image into a memory buffer of the desired type. Stride is natural, rounded up to the nearest 4 bytes.
	Error LoadJpeg(const void* jpegBuf, size_t jpegLen, int& width, int& height, void*& buf, TJPF format = TJPF_RGBA);

	// Decodes a jpeg image with downscaling by 1/2 or 1/4. scaleFactor can be 1,2,4, for 1/1, 1/2, 1/4 scales.
	Error LoadJpegScaled(const void* jpegBuf, size_t jpegLen, int scaleFactor, int& width, int& height, void*& buf, TJPF format = TJPF_RGBA);

	// Encode an RGBA buffer to jpeg
	Error SaveJpeg(ImageFormat format, int width, int height, int stride, const void* buf, int quality_0_to_100, JpegSampling sampling, void*& jpegBuf, size_t& jpegSize);

	// Save jpeg to file
	static Error SaveJpegFile(const std::string& filename, ImageFormat format, int width, int height, int stride, const void* buf, int quality_0_to_100 = 90, JpegSampling sampling = JpegSampling::Samp444);
};

} // namespace gfx
} // namespace imqs
