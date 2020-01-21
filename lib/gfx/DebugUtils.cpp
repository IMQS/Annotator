#include "pch.h"
#include "DebugUtils.h"
#include "Canvas.h"
#include "Rect.h"

using namespace std;

namespace imqs {
namespace gfx {
namespace debug {

void DebugSurface::DrawFile(std::string filename, int width) {
	geom2d::BBox2d bb;
	for (const auto& p : Polys) {
		for (const auto& v : p.V) {
			bb.ExpandToFit(v.x, v.y);
		}
	}
	double aspect = bb.Width() / (bb.Height() + 1e-6);
	aspect        = math::Clamp(aspect, 0.5, 2.0);
	int    height = int(width / aspect);
	Canvas c;
	c.Alloc(width, height, Color8::White());

	Vec2d  tx(-bb.X1 + bb.Width() * 0.02, -bb.Y1 + bb.Width() * 0.02);
	double scale = 0.95 * ((double) c.Width() / bb.Width());

	Color8 colors[] = {
	    {200, 0, 0, 255},
	    {200, 100, 0, 255},
	    {100, 200, 0, 255},
	    {0, 200, 0, 255},
	    {0, 200, 100, 255},
	    {0, 100, 200, 255},
	    {0, 0, 200, 255},
	    {100, 0, 200, 255},
	    {200, 0, 100, 255},
	};
	size_t ncolors = arraysize(colors);

	size_t icol = 0;
	for (size_t i = 0; i < Polys.size(); i++) {
		const auto&   p = Polys[i];
		vector<Vec2f> f;
		for (const auto& v : p.V) {
			auto t = scale * (v + tx);
			f.emplace_back((float) t.x, (float) t.y);
		}
		auto col = colors[i % ncolors];
		c.StrokePoly(p.Closed, (int) p.V.size(), &f[0].x, sizeof(f[0]), col, 1.0f);
	}
	c.GetImage()->SaveFile(filename);
}

} // namespace debug
} // namespace gfx
} // namespace imqs