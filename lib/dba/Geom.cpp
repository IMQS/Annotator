#include "pch.h"
#include "Geom.h"
#include "WKG/WKG.h"

namespace imqs {
namespace dba {
namespace geom {

static void WKG_Read_Info(void* context, int* type, int* numparts, uint32_t* vxstride, ptrdiff_t* moffset, ptrdiff_t* zoffset, int* srid) {
}

static void WKG_Read_GetPart(void* context, int part, double** vertices, int* count, int* depth, int* closed) {
}

IMQS_DBA_API void EncodeWKB(const Attrib& src, io::Buffer& dst) {
	WKG_Reader reader;
	reader.Context = const_cast<Attrib*>(&src); // this is a safe cast - we do not modify Attrib, and always cast back to const Attrib*
	reader.Info    = WKG_Read_Info;
	reader.GetPart = WKG_Read_GetPart;
	uint32_t flags = 0;
	WKG_BinBytes(&reader, flags);
}

IMQS_DBA_API geom2d::BBox2d BBox2d(const Attrib& geom) {
	geom2d::BBox2d box;
	size_t         nv   = geom.GeomTotalVertexCount();
	size_t         ndim = 2 + (geom.GeomHasM() ? 1 : 0) + (geom.GeomHasZ() ? 1 : 0);
	if (geom.GeomIsDoubles()) {
		double* vx = (double*) geom.GeomVertices();
		for (size_t i = 0; i < nv; i++) {
			box.ExpandToFit(vx[0], vx[1]);
			vx += ndim;
		}
	} else {
		float* vx = (float*) geom.GeomVertices();
		for (size_t i = 0; i < nv; i++) {
			box.ExpandToFit((double) vx[0], (double) vx[1]);
			vx += ndim;
		}
	}
	return box;
}

// NOTE: It's harder than you'd think to templatize these three variants, especially with
// compilers doing smart memory-away static analysis. I did try, but gave up quickly.

IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec2d>& v) {
	size_t gdim = geom.GeomDimensions();
	int    start, count;
	geom.GeomPart(part, start, count);
	const double* src = geom.GeomVerticesDbl();
	src += start * gdim;
	for (int i = start; i < start + count; i++, src += gdim) {
		gfx::Vec2d vx;
		vx.n[0] = src[0];
		vx.n[1] = src[1];
		v.push_back(vx);
	}
}

IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec3d>& v) {
	size_t gdim = geom.GeomDimensions();
	int    start, count;
	geom.GeomPart(part, start, count);
	const double* src = geom.GeomVerticesDbl();
	src += start * gdim;
	for (int i = start; i < start + count; i++, src += gdim) {
		gfx::Vec3d vx;
		vx.n[0] = src[0];
		vx.n[1] = src[1];
		if (gdim >= 3)
			vx.n[2] = src[2];
		else
			vx.n[2] = 0;
		v.push_back(vx);
	}
}

IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec4d>& v) {
	size_t gdim = geom.GeomDimensions();
	int    start, count;
	geom.GeomPart(part, start, count);
	const double* src = geom.GeomVerticesDbl();
	src += start * gdim;
	for (int i = start; i < start + count; i++, src += gdim) {
		gfx::Vec4d vx;
		vx.n[0] = src[0];
		vx.n[1] = src[1];
		if (gdim >= 3)
			vx.n[2] = src[2];
		else
			vx.n[2] = 0;
		if (gdim >= 4)
			vx.n[3] = src[3];
		else
			vx.n[3] = 0;
		v.push_back(vx);
	}
}

IMQS_DBA_API double Distance2D(const Attrib& g1, const Attrib& g2) {
	if (g1.IsPoint() && g2.IsPoint()) {
		auto       p1 = g1.GeomVerticesDbl();
		auto       p2 = g2.GeomVerticesDbl();
		gfx::Vec2d v1(p1[0], p1[1]);
		gfx::Vec2d v2(p2[0], p2[1]);
		return v1.distance(v2);
	}
	return DBL_MAX;
}

IMQS_DBA_API bool PtInsidePoly(const Attrib& poly, double x, double y) {
	if (!poly.IsPoly())
		return false;
	const double* vx         = poly.GeomVerticesDbl();
	size_t        stride_dbl = poly.GeomDimensions();
	int           inside     = 0;
	for (size_t iRing = 0; iRing < poly.GeomNumParts(); iRing++) {
		int start, count;
		poly.GeomPart(iRing, start, count);
		inside ^= geom2d::PtInsidePoly(x, y, count, vx + start * stride_dbl, stride_dbl) ? 1 : 0;
	}
	return inside != 0;
}

IMQS_DBA_API double Area(const Attrib& poly) {
	if (!poly.IsPoly())
		return 0;
	double        area         = 0;
	const double* vx           = poly.GeomVerticesDbl();
	size_t        stride_dbl   = poly.GeomDimensions();
	size_t        numParts     = poly.GeomNumParts();
	bool          haveRingInfo = poly.GeomIsWKBOrder(); // If we don't have ring info, then assume that all rings are exterior
	for (size_t iRing = 0; iRing < numParts; iRing++) {
		int    start, count;
		auto   flags    = poly.GeomPart(iRing, start, count);
		double partArea = geom2d::PolygonArea(count, vx + start * stride_dbl, stride_dbl);
		if (!haveRingInfo || !!(flags & GeomPartFlag_ExteriorRing))
			area += partArea;
		else
			area -= partArea;
	}
	return area;
}

} // namespace geom
} // namespace dba
} // namespace imqs