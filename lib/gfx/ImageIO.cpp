#include "pch.h"
#include "ImageIO.h"

namespace imqs {
namespace gfx {

static StaticError ErrJpegHead("Invalid jpeg header");
static StaticError ErrJpegDecode("Invalid jpeg image");
static StaticError ErrPngDecode("Invalid png image");
static StaticError ErrImageDecode("Invalid png or jpeg image");

ImageIO::ImageIO() {
}

ImageIO::~ImageIO() {
	if (JpegDecomp)
		tjDestroy(JpegDecomp);
	if (JpegEncoder)
		tjDestroy(JpegEncoder);
}

Error ImageIO::Save(ImageFormat format, int width, int height, int stride, const void* buf, ImageType type, bool withAlpha, int lossyQ_0_to_100, int losslessQ_1_to_9, void*& encBuf, size_t& encSize) {
	switch (type) {
	case ImageType::Jpeg: return SaveJpeg(format, width, height, stride, buf, lossyQ_0_to_100, JpegSampling::Samp444, encBuf, encSize);
	case ImageType::Png:
		if (!(format == ImageFormat::RGBA || format == ImageFormat::RGBAP))
			return Error::Fmt("Invalid image type %v for PNG save", (int) format);
		return SavePng(withAlpha, width, height, stride, buf, losslessQ_1_to_9, encBuf, encSize);
	default: return Error("Unsupported image type for compression");
	}
}

Error ImageIO::Load(const void* encBuf, size_t encLen, int& width, int& height, void*& buf) {
	if (encLen < 8)
		return ErrImageDecode;
	// See https://en.wikipedia.org/wiki/Magic_number_%28programming%29
	uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
	if (memcmp(png, encBuf, 8) == 0)
		return LoadPng(encBuf, encLen, width, height, buf);
	return LoadJpeg(encBuf, encLen, width, height, buf);
}

void ImageIO::FreeEncodedBuffer(ImageType type, void* encBuf) {
	switch (type) {
	case ImageType::Jpeg:
		tjFree((unsigned char*) encBuf);
		break;
	default:
		free(encBuf);
		break;
	}
}

Error ImageIO::LoadPng(const void* pngBuf, size_t pngLen, int& width, int& height, void*& buf) {
	// stbi's png loader is about 30% slower than libpng for huge png files, but for our typical sizes (ie tiles),
	// stbi's speed is indistinguishable (or maybe even faster, I don't remember) than libpng.
	int chan = 0;
	buf      = stbi_load_from_memory((const stbi_uc*) pngBuf, (int) pngLen, &width, &height, &chan, 4);
	if (!buf)
		return ErrPngDecode;
	return Error();
}

struct PngWriteBuf {
	void*  Buf = nullptr;
	size_t Len = 0;
	size_t Cap = 0;
};

static void PngWriteData(png_structp pp, png_bytep data, png_size_t len) {
	PngWriteBuf* wb = (PngWriteBuf*) png_get_io_ptr(pp);
	if (wb->Len + len > wb->Cap) {
		while (wb->Cap < wb->Len + len)
			wb->Cap *= 2;
		wb->Buf = imqs_realloc_or_die(wb->Buf, wb->Cap);
	}
	memcpy((uint8_t*) wb->Buf + wb->Len, data, len);
	wb->Len += len;
}

static void PngFlushData(png_structp pp) {
}

static void PngErrorFunc(png_structp pp, png_const_charp msg) {
	// This is not expected
	tsf::print("PNG error: %v\n", msg);
	IMQS_DIE_MSG(tsf::fmt("PNG error: %v", msg).c_str());
}

static void PngWarningFunc(png_structp pp, png_const_charp msg) {
	//tsf::print("PNG warning: %v\n", msg);
}

Error ImageIO::SavePng(bool withAlpha, int width, int height, int stride, const void* buf, int zlibLevel, void*& encBuf, size_t& encSize) {
	uint8_t*  buf8    = (uint8_t*) buf;
	uint8_t*  rgb     = nullptr;
	uint8_t** rows    = (uint8_t**) imqs_malloc_or_die(height * sizeof(void*));
	int       pngType = 0;
	if (withAlpha) {
		pngType = PNG_COLOR_TYPE_RGBA;
		for (int i = 0; i < (int) height; i++)
			rows[i] = buf8 + i * stride;
	} else {
		// libpng doesn't accept RGBA for an RGB image, so we need to transform our data from RGBA to RGB
		pngType = PNG_COLOR_TYPE_RGB;
		rgb     = (uint8_t*) imqs_malloc_or_die(width * height * 3);
		for (int i = 0; i < (int) height; i++) {
			auto rgba = buf8 + i * stride;
			auto line = rgb + i * width * 3;
			rows[i]   = line;
			for (int j = 0; j < width; j++) {
				*line++ = rgba[0];
				*line++ = rgba[1];
				*line++ = rgba[2];
				rgba += 4;
			}
		}
	}

	PngWriteBuf wb;
	wb.Cap = width * height;
	wb.Buf = imqs_malloc_or_die(wb.Cap); // assume 3:1 compression ratio

	//tsf::print("PNG version %v\n", PNG_LIBPNG_VER_STRING);
	auto pp = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, PngErrorFunc, PngWarningFunc);
	auto ip = png_create_info_struct(pp);
	png_set_write_fn(pp, &wb, PngWriteData, PngFlushData);
	png_set_IHDR(pp, ip, width, height, 8, pngType, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_compression_level(pp, zlibLevel);
	png_write_info(pp, ip);
	png_write_image(pp, rows);
	png_write_end(pp, nullptr);
	png_destroy_write_struct(&pp, &ip);

	free(rows);
	free(rgb);

	encBuf  = wb.Buf;
	encSize = wb.Len;

	return Error();
}

Error ImageIO::SavePngFile(const std::string& filename, bool withAlpha, int width, int height, int stride, const void* buf, int zlibLevel) {
	ImageIO self;
	void*   encBuf  = nullptr;
	size_t  encSize = 0;
	auto    err     = self.SavePng(withAlpha, width, height, stride, buf, zlibLevel, encBuf, encSize);
	if (!err.OK())
		return err;
	err = os::WriteWholeFile(filename, encBuf, encSize);
	self.FreeEncodedBuffer(ImageType::Png, encBuf);
	return err;
}

// The lack of const in the pointers we send libjpeg-turbo is for the older version that ships on Ubuntu 16.04

static int BytesPerSample(TJPF format) {
	switch (format) {
	case TJPF_RGB: return 3;
	case TJPF_BGR: return 3;
	case TJPF_RGBX: return 4;
	case TJPF_BGRX: return 4;
	case TJPF_XBGR: return 4;
	case TJPF_XRGB: return 4;
	case TJPF_GRAY: return 1;
	case TJPF_RGBA: return 4;
	case TJPF_BGRA: return 4;
	case TJPF_ABGR: return 4;
	case TJPF_ARGB: return 4;
	case TJPF_CMYK: return 4;
	default:
		IMQS_DIE();
		return 0;
	}
};

Error ImageIO::LoadJpeg(const void* jpegBuf, size_t jpegLen, int& width, int& height, void*& buf, TJPF format) {
	return LoadJpegScaled(jpegBuf, jpegLen, 1, width, height, buf, format);
}

Error ImageIO::LoadJpegHeader(const void* jpegBuf, size_t jpegLen, int* _width, int* _height, JpegSampling* _sampling, bool* _isColor) {
	if (!JpegDecomp)
		JpegDecomp = tjInitDecompress();
	int width      = 0;
	int height     = 0;
	int subsamp    = 0;
	int colorspace = 0;
	if (0 != tjDecompressHeader3(JpegDecomp, (uint8_t*) jpegBuf, (unsigned long) jpegLen, &width, &height, &subsamp, &colorspace))
		return ErrJpegHead;
	if (_width)
		*_width = width;
	if (_height)
		*_height = height;
	if (_sampling)
		*_sampling = (JpegSampling) subsamp;
	if (_isColor)
		*_isColor = colorspace != TJCS_GRAY;
	return Error();
}

Error ImageIO::LoadJpegFileHeader(const std::string& filename, int* width, int* height, JpegSampling* sampling, bool* isColor) {
	std::string buf;
	auto        err = os::ReadWholeFile(filename, buf);
	if (!err.OK())
		return err;
	return LoadJpegHeader(buf.data(), buf.size(), width, height, sampling, isColor);
}

Error ImageIO::LoadJpegScaled(const void* jpegBuf, size_t jpegLen, int scaleFactor, int& width, int& height, void*& buf, TJPF format) {
	if (!JpegDecomp)
		JpegDecomp = tjInitDecompress();
	int subsamp;
	int colorspace;
	if (0 != tjDecompressHeader3(JpegDecomp, (uint8_t*) jpegBuf, (unsigned long) jpegLen, &width, &height, &subsamp, &colorspace))
		return ErrJpegHead;
	width /= scaleFactor;
	height /= scaleFactor;
	int stride = BytesPerSample(format) * width;
	stride     = math::RoundUpInt(stride, 4);
	buf        = imqs_malloc_or_die(stride * height);
	if (0 != tjDecompress2(JpegDecomp, (uint8_t*) jpegBuf, (unsigned long) jpegLen, (uint8_t*) buf, width, stride, height, format, 0)) {
		free(buf);
		buf = nullptr;
		return Error(tjGetErrorStr());
	}
	return Error();
}

Error ImageIO::SaveJpeg(ImageFormat format, int width, int height, int stride, const void* buf, int quality_0_to_100, JpegSampling sampling, void*& jpegBuf, size_t& jpegSize) {
	int tjFormat = 0;
	switch (format) {
	case ImageFormat::Null: return Error("Invalid format 'Null' for SaveJpeg");
	case ImageFormat::F32_RG: return Error("Invalid format 'F32_RG' for SaveJpeg");
	case ImageFormat::F32_RGBA: return Error("Invalid format 'F32_RGBA' for SaveJpeg");
	case ImageFormat::RGBA: tjFormat = TJPF_RGBA; break;
	case ImageFormat::RGBAP: tjFormat = TJPF_RGBA; break;
	case ImageFormat::Gray:
		tjFormat = TJPF_GRAY;
		sampling = JpegSampling::SampGray;
		break;
	}

	if (!JpegEncoder)
		JpegEncoder = tjInitCompress();

	unsigned long size = 0;
	if (tjCompress2(JpegEncoder, (unsigned char*) buf, width, stride, height, tjFormat, (unsigned char**) &jpegBuf, &size, (int) sampling, quality_0_to_100, 0) != 0)
		return Error(tjGetErrorStr());
	jpegSize = size;
	return Error();
}

Error ImageIO::SaveJpegFile(const std::string& filename, ImageFormat format, int width, int height, int stride, const void* buf, int quality_0_to_100, JpegSampling sampling) {
	ImageIO self;
	void*   encBuf  = nullptr;
	size_t  encSize = 0;
	auto    err     = self.SaveJpeg(format, width, height, stride, buf, quality_0_to_100, sampling, encBuf, encSize);
	if (!err.OK())
		return err;
	err = os::WriteWholeFile(filename, encBuf, encSize);
	self.FreeEncodedBuffer(ImageType::Jpeg, encBuf);
	return err;
}

} // namespace gfx
} // namespace imqs
