#include "pch.h"
#include "Canvas.h"

namespace imqs {
namespace gfx {

Canvas::Canvas(int width, int height, Color8 fill) {
	if (width != 0 && height != 0)
		Alloc(width, height, fill);
}

Canvas::~Canvas() {
	Reset();
}

void Canvas::Alloc(int width, int height, Color8 fill) {
	Reset();
	if (width == 0 || height == 0)
		return;
	Img.Alloc(ImageFormat::RGBA, width, height);
	// Lock the image, so that a user can't accidentally cause it's memory to be reallocated.
	// Doing so would invalidate RenderBuff
	Img.Locked = true;
	RenderBuff.attach((uint8_t*) Img.Data, width, height, Img.Stride);
	PixFormatRGBA.attach(RenderBuff);
	RenderBaseRGBA.attach(PixFormatRGBA);
	RenderAA_RGBA.attach(RenderBaseRGBA);
	//RasAA.gamma(agg::gamma_power(2.2));
	IsAlive     = true;
	InvalidRect = Rect32(0, 0, width, height);
	Fill(fill);
}

void Canvas::Reset() {
	Img.Locked = false;
	Img.Reset();
	//Buf     = nullptr;
	IsAlive = false;
}

void Canvas::Fill(Color8 color) {
	if (!IsAlive)
		return;

	FillRect(Rect32(0, 0, Width(), Height()), color);
}

void Canvas::FillRect(Rect32 box, Color8 color) {
	if (!IsAlive)
		return;

	RenderBaseRGBA.copy_bar(box.x1, box.y1, box.x2, box.y2, ColorToAggS8(color));
	InvalidRect.ExpandToFit(box);
}

void Canvas::Rect(Rect32 box, Color8 color, float linewidth) {
	if (!IsAlive)
		return;

	float v[8] = {
	    (float) box.x1,
	    (float) box.y1,
	    (float) box.x2,
	    (float) box.y1,
	    (float) box.x2,
	    (float) box.y2,
	    (float) box.x1,
	    (float) box.y2,
	};

	Line(true, 4, v, 2 * sizeof(float), color, linewidth);
}

void Canvas::Rect(RectF box, Color8 color, float linewidth) {
	if (!IsAlive)
		return;

	float v[8] = {
	    box.x1,
	    box.y1,
	    box.x2,
	    box.y1,
	    box.x2,
	    box.y2,
	    box.x1,
	    box.y2,
	};

	Line(true, 4, v, 2 * sizeof(float), color, linewidth);
}

void Canvas::Line(bool closed, int nvx, const float* vx, int vx_stride_bytes, Color8 color, float linewidth) {
	if (!IsAlive)
		return;

	RasAA.reset();
	RasAA.filling_rule(agg::fill_non_zero);

	agg::path_storage path;

	path.start_new_path();

	// emit first vertex
	path.move_to(vx[0], vx[1]);
	(char*&) vx += vx_stride_bytes;
	nvx--;

	// emit remaining vertices
	for (int i = 0; i < nvx; i++, (char*&) vx += vx_stride_bytes)
		path.line_to(vx[0], vx[1]);

	if (closed)
		path.close_polygon();

	TLineClipper                   clipped_line(path);
	TFillClipper                   clipped_fill(path);
	agg::conv_stroke<TLineClipper> clipped_line_stroked(clipped_line);
	agg::conv_stroke<TFillClipper> clipped_fill_stroked(clipped_fill);

	if (closed) {
		clipped_fill.clip_box(-linewidth, -linewidth, Width() + linewidth, Height() + linewidth);
		clipped_fill_stroked.line_cap(agg::butt_cap);
		clipped_fill_stroked.line_join(agg::miter_join);
		clipped_fill_stroked.width(linewidth);
		RasAA.add_path(clipped_fill_stroked);
	} else {
		clipped_line.clip_box(-linewidth, -linewidth, Width() + linewidth, Height() + linewidth);
		clipped_line_stroked.line_cap(agg::butt_cap);
		clipped_line_stroked.line_join(agg::miter_join);
		clipped_line_stroked.width(linewidth);
		RasAA.add_path(clipped_line_stroked);
	}

	RenderAA_RGBA.color(ColorToAggS8(color));

	RenderScanlines();
}

void Canvas::Line(float x1, float y1, float x2, float y2, Color8 color, float linewidth) {
	float vx[4] = {
	    x1,
	    y1,
	    x2,
	    y2,
	};
	Line(false, 2, vx, 2 * sizeof(float), color, linewidth);
}

void Canvas::Circle(float x, float y, float radius, Color8 color, float linewidth) {
	if (!IsAlive)
		return;

	RasAA.reset();
	agg::path_storage path;
	path.start_new_path();
	agg::ellipse elps;
	elps.init(x, y, radius, radius);
	path.concat_path(elps, 0);

	//TLineClipper clipped_line(path);
	//clipped_line.clip_box(-linewidth, -linewidth, Width() + linewidth, Height() + linewidth);
	//agg::conv_stroke<TLineClipper> clipped_line_stroked(clipped_line);
	//clipped_line_stroked.width(linewidth);

	agg::conv_stroke<agg::path_storage> stroked(path);
	stroked.width(linewidth);

	RasAA.add_path(stroked);
	RenderAA_RGBA.color(ColorToAggS8(color));
	RenderScanlines();
}

void Canvas::FillCircle(float x, float y, float radius, Color8 color) {
	if (!IsAlive)
		return;

	RasAA.reset();
	agg::path_storage path;
	path.start_new_path();
	agg::ellipse elps;
	elps.init(x, y, radius, radius);
	path.concat_path(elps, 0);

	RasAA.add_path(path);
	RenderAA_RGBA.color(ColorToAggS8(color));
	RenderScanlines();
}

void Canvas::FillPoly(int nvx, const float* vx, int vx_stride_bytes, Color8 color) {
	if (!IsAlive)
		return;

	RasAA.reset();
	agg::path_storage path;
	path.start_new_path();
	path.move_to(vx[0], vx[1]);
	(char*&) vx += vx_stride_bytes;
	for (int i = 1; i < nvx; i++) {
		path.line_to(vx[0], vx[1]);
		(char*&) vx += vx_stride_bytes;
	}

	RasAA.add_path(path);
	RenderAA_RGBA.color(ColorToAggS8(color));
	RenderScanlines();
}

void Canvas::StrokePoly(bool closed, int nvx, const float* vx, int vx_stride_bytes, Color8 color, float linewidth) {
	if (!IsAlive)
		return;

	RasAA.reset();
	agg::path_storage path;
	path.start_new_path();
	path.move_to(vx[0], vx[1]);
	(char*&) vx += vx_stride_bytes;
	for (int i = 1; i < nvx; i++) {
		path.line_to(vx[0], vx[1]);
		(char*&) vx += vx_stride_bytes;
	}
	if (closed)
		path.close_polygon();

	agg::conv_stroke<agg::path_storage> stroked(path);
	stroked.width(linewidth);

	RasAA.add_path(stroked);
	RenderAA_RGBA.color(ColorToAggS8(color));
	RenderScanlines();
}

/*
void Canvas::RenderSVG(const char* svg) {
	try {
		agg::svg::path_renderer path;
		agg::svg::parser        parse(path);
		parse.parse_mem(svg);

		typedef agg::pixfmt_rgba32                             pixfmt;
		typedef agg::renderer_base<pixfmt>                     renderer_base;
		typedef agg::renderer_scanline_aa_solid<renderer_base> renderer_solid;

		//int stride = key.Width * 4;
		//uint8_t* buf = (uint8_t*) MallocOrDie(stride * key.Height);
		//agg::rendering_buffer rbuf(buf, key.Width, key.Height, stride);

		pixfmt         pixf(RenderBuff);
		renderer_base  rb(pixf);
		renderer_solid ren(rb);

		//rb.clear(agg::rgba(1, 1, 1, 0));
		rb.clear(agg::rgba(0, 0, 0, 0));

		agg::rasterizer_scanline_aa<> ras;
		agg::scanline_p8              sl;
		agg::trans_affine             mtx;

		auto   vb       = parse.view_box();
		double vbWidth  = vb[2] - vb[0];
		double vbHeight = vb[3] - vb[1];
		double scale    = std::min(Width() / vbWidth, Height() / vbHeight);

		//ras.gamma(agg::gamma_power(1));
		//ras.gamma(agg::gamma_power(m_gamma.value()));
		//mtx *= agg::trans_affine_translation((m_min_x + m_max_x) * -0.5, (m_min_y + m_max_y) * -0.5);
		mtx *= agg::trans_affine_scaling(scale);
		//mtx *= agg::trans_affine_rotation(agg::deg2rad(m_rotate.value()));
		//mtx *= agg::trans_affine_translation((m_min_x + m_max_x) * 0.5 + m_x, (m_min_y + m_max_y) * 0.5 + m_y + 30);

		//m_path.expand(m_expand.value());
		//start_timer();
		path.render(ras, sl, ren, mtx, rb.clip_box(), 1.0);
		//double tm = elapsed_time();
		//unsigned vertex_count = m_path.vertex_count();

	} catch (const agg::svg::exception& ex) {
		Trace("Error rendering svg: %v", ex.msg());
	}
}

void Canvas::Text(float x, float y, float angle, float size, Color8 color, const char* font, const char* str) {
	if (size < 1.0f)
		return;
	const Font* fnt = Global()->FontStore->GetByFacename(font);
	if (!fnt)
		return;

	bool useCache = size <= 30 && angle == 0;
	int  isize    = (int) (size + 0.5f);

	// Our rendering mode here is not correct, in terms of gamma etc.
	// Rendering looks awful if you render to a transparent canvas, but it looks OK
	// when rendering to an opaque background (ie so that AGG does the blending, not the GPU)

	Vec2f pos(x, y);
	//Vec2f dir(cos(angle), -sin(angle));

	XO_ASSERT(angle == 0);
	auto col = ColorToAggS8(color);

	if (useCache) {
		auto                        cache = Global()->GlyphCache;
		std::lock_guard<std::mutex> cache_lock(cache->Lock);
		int                         flags = 0;
		int                         posX  = (int) pos.x;
		int                         posY  = (int) pos.y;
		for (auto ch : utfz::cp(str)) {
			GlyphCacheKey key(fnt->ID, ch, isize, flags);
			auto          glyph = cache->GetOrRenderGlyph(key);
			if (!glyph->IsNull()) {
				auto atlas = cache->GetAtlas(glyph->AtlasID);
				for (unsigned y = 0; y < glyph->Height; y++) {
					int         outX = posX + glyph->MetricLeft;
					int         outY = posY + y - glyph->MetricTop;
					const void* src  = atlas->DataAt(glyph->X, glyph->Y + y);
					if ((unsigned) outY >= RenderBuff.height())
						continue;
					PixFormatRGBA.blend_solid_hspan(outX, outY, glyph->Width, col, (const agg::int8u*) src);
				}
				posX += glyph->MetricHoriAdvance;
				// ignore vertical advance
			}
		}
	} else {
		std::lock_guard<std::mutex> ft_face_lock(fnt->FTFace_Lock);
		FT_Error                    e = FT_Set_Pixel_Sizes(fnt->FTFace, isize, isize);
		XO_ASSERT(e == 0);

		for (auto ch : utfz::cp(str)) {
			auto     iglyph  = FT_Get_Char_Index(fnt->FTFace, ch);
			uint32_t ftflags = FT_LOAD_RENDER;
			FT_Error e       = FT_Load_Glyph(fnt->FTFace, iglyph, ftflags);
			if (e != 0) {
				//Trace("Failed to load glyph for character %d (%d)\n", ch, iglyph);
				continue;
			}
			const FT_GlyphSlot& glyph = fnt->FTFace->glyph;
			const FT_Bitmap&    bmp   = glyph->bitmap;
			for (unsigned y = 0; y < bmp.rows; y++) {
				const void* src  = bmp.buffer + y * bmp.pitch;
				int         outX = (int) pos.x + glyph->bitmap_left;
				int         outY = (int) pos.y + y - glyph->bitmap_top;
				if ((unsigned) outY >= RenderBuff.height())
					continue;
				PixFormatRGBA.blend_solid_hspan(outX, outY, bmp.width, col, (const agg::int8u*) src);
			}

			pos.x += (float) glyph->advance.x / 64.0f;
			pos.y += (float) glyph->advance.y / 64.0f;
		}
	}
}
*/

//agg::rgba Canvas::ColorToAgg(Color8 c) {
//	c             = c.Premultiply();
//	//c             = c.PremultiplySRGB();
//	const float s = 1.0f / 255.0f;
//	return agg::rgba(c.r * s, c.g * s, c.b * s, c.a * s);
//}

agg::srgba8 Canvas::ColorToAggS8(Color8 c) {
	//c = c.Premultiply();
	return agg::srgba8(c.r, c.g, c.b, c.a);
}

agg::rgba8 Canvas::ColorToAgg8(Color8 c) {
	// If this is destined for a proper sRGB pipeline (ie our GPU render target),
	// then it's vital that you perform premultiplication in sRGB space. The tell-tale
	// sign if you do this in gamma space, is washed out colors.
	//c = c.Premultiply();
	//c = c.PremultiplySRGB();
	//c.r = Min(c.r, c.a);
	//c.g = Min(c.g, c.a);
	//c.b = Min(c.b, c.a);
	return agg::rgba8(c.r, c.g, c.b, c.a);
}

void Canvas::RenderScanlines() {
	agg::render_scanlines(RasAA, Scanline, RenderAA_RGBA);
	InvalidRect.ExpandToFit(Rect32(RasAA.min_x(), RasAA.min_y(), RasAA.max_x(), RasAA.max_y()));
}

} // namespace gfx
} // namespace imqs