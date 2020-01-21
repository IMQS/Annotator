#include "pch.h"

#include "Color8.h"
#include "Rect.h"
#include "Image.h"

namespace imqs {
namespace gfx {

/*
Some old notes from the original code from which this canvas was ripped (github.com/bmharper/xo):

	Simple canvas that uses AGG for rendering.
	Pixel format is RGBA 8 bits/sample, premultiplied alpha, sRGB.
	Blending is done in gamma space (ie not good).
	NOTE: If you modify the contents of the buffer without using the supplied
	functions, then you must call Invalidate() to let the system know what
	parts of the image have changed. When uploading textures to the GPU, we
	only send the modified region.

	NOTE!! Read all 4 parts of http://www.chez-jim.net/agg/gamma-correct-rendering-part-1, and implement
	an option from there. I think our only choice is to do linear blending into an 8-bit framebuffer.
	If we want to go for a 16-bit framebuffer, then any sRGB image that you load into a canvas is going to
	take 2x the space, which is enough of a hindrance to not make it worthwhile. I was thinking that you
	could avoid the branches of the linear -> sRGB binary search by creating a table of linear scales,
	such that your linear -> sRGB conversion is piecewise linear. That way you'd have no branches, but you
	would have a data-dependent multiply. You'd have to benchmark of course.
	But above and beyond that - I really don't know if it's possible to do premultiplied alpha with sRGB.
	I need to properly investigate that.

	MORE UPDATE.. I have switched to non-premultiplied alpha, just because it's the only option that seems
	vaguely sane. It's absolutely awful for blending, because you have the alpha divide on every blend.
	grrr..

	THE ANSWER! Comes from this tweet by Fabian Giesen: https://twitter.com/nothings/status/501513209757437952

	Fabian Giesen @rygorous 19 Aug 2014
	Replying to @nothings
	@nothings With sRGB encoding, I'd expect you to store sRGB(premul(x)) not premul(sRGB(x)).

*/
class Canvas {
public:
	Canvas(int width = 0, int height = 0, Color8 fill = Color8(0, 0, 0, 0));
	~Canvas();

	void Alloc(int width, int height, Color8 fill = Color8(0, 0, 0, 0));
	void Reset(); // Free memory

	// Buffer/State access (use Invalidate if you modify contents directly)
	void*        Buffer() { return RenderBuff.buf(); }
	const void*  Buffer() const { return RenderBuff.buf(); }
	void*        RowPtr(int line) { return RenderBuff.row_ptr(line); }
	const void*  RowPtr(int line) const { return RenderBuff.row_ptr(line); }
	void*        PixelPtr(int x, int y) { return RenderBuff.row_ptr(y) + 4 * (uint32_t) x; }
	const void*  PixelPtr(int x, int y) const { return RenderBuff.row_ptr(y) + 4 * (uint32_t) x; }
	int32_t      Stride() const { return RenderBuff.stride(); }
	uint32_t     StrideAbs() const { return RenderBuff.stride_abs(); }
	uint32_t     Width() const { return RenderBuff.width(); }
	uint32_t     Height() const { return RenderBuff.height(); }
	Rect32       GetInvalidRect() const { return InvalidRect; }                  // Retrieve the bounding rectangle of all pixels that have been modified
	void         Invalidate(Rect32 box) { InvalidRect.ExpandToFit(box); }        // Call this if you modify the buffer by directly accessing its memory
	void         Invalidate() { InvalidRect = Rect32(0, 0, Width(), Height()); } // Call this if you modify the buffer by directly accessing its memory
	const Image* GetImage() const { return &Img; }
	Image*       GetImage() { return &Img; }

	// Drawing functions
	void Fill(Color8 color);
	void FillRect(Rect32 box, Color8 color);
	void Rect(Rect32 box, Color8 color, float linewidth);
	void Rect(RectF box, Color8 color, float linewidth);
	void Line(bool closed, int nvx, const float* vx, int vx_stride_bytes, Color8 color, float linewidth);
	void Line(float x1, float y1, float x2, float y2, Color8 color, float linewidth);
	void Circle(float x, float y, float radius, Color8 color, float linewidth);
	void FillCircle(float x, float y, float radius, Color8 color);
	void FillPoly(int nvx, const float* vx, int vx_stride_bytes, Color8 color);
	void StrokePoly(bool closed, int nvx, const float* vx, int vx_stride_bytes, Color8 color, float linewidth);
	//void RenderSVG(const char* svg);
	//void Text(float x, float y, float angle, float size, Color8 color, const char* font, const char* str);

	void SetPixel(int x, int y, Color8 c) { ((uint32_t*) RenderBuff.row_ptr(y))[x] = c.u; }

protected:
	//typedef agg::pixfmt_srgba32_pre PixFormat;
	typedef agg::pixfmt_srgba32 PixFormat;
	//typedef agg::pixfmt_rgba32     PixFormat;

	typedef agg::renderer_base<PixFormat>                    TRenderBaseRGBA;
	typedef agg::renderer_scanline_aa_solid<TRenderBaseRGBA> TRendererAA_RGBA;
	typedef agg::rasterizer_scanline_aa_nogamma<>            TRasterScanlineAA;
	//typedef agg::rasterizer_scanline_aa<>              TRasterScanlineAA;
	typedef agg::conv_clip_polyline<agg::path_storage> TLineClipper;
	typedef agg::conv_clip_polygon<agg::path_storage>  TFillClipper;

	//uint8_t*              Buf = nullptr;
	Image                 Img;
	agg::scanline_u8      Scanline;
	TRasterScanlineAA     RasAA;
	agg::rendering_buffer RenderBuff;
	TRenderBaseRGBA       RenderBaseRGBA;
	TRendererAA_RGBA      RenderAA_RGBA;
	PixFormat             PixFormatRGBA;
	Rect32                InvalidRect;
	bool                  IsAlive = false; // We have a valid Image, and non-zero width and height

	//agg::rgba  ColorToAgg(Color8 c);
	agg::rgba8  ColorToAgg8(Color8 c);
	agg::srgba8 ColorToAggS8(Color8 c);
	void        RenderScanlines();
};

} // namespace gfx
} // namespace imqs