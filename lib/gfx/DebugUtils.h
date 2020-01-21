#pragma once

#include "Vec2.h"

namespace imqs {
namespace gfx {
namespace debug {

class DebugSurface {
public:
	struct Poly {
		std::vector<Vec2d> V;
		bool               Closed = false;
	};
	std::vector<Poly> Polys;

	template <typename T>
	void AddPoly(size_t nVx, const T* vx, int strideInUnitsOfT, bool closed);

	void DrawFile(std::string filename, int width = 512);
};

template <typename T>
void DebugSurface::AddPoly(size_t nVx, const T* vx, int strideInUnitsOfT, bool closed) {
	Poly p;
	p.Closed   = closed;
	const T* v = vx;
	for (size_t i = 0; i < nVx; i++) {
		p.V.push_back(gfx::Vec2d(v[0], v[1]));
		v += strideInUnitsOfT;
	}
	Polys.push_back(std::move(p));
}

} // namespace debug
} // namespace gfx
} // namespace imqs