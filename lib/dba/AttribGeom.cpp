#include "pch.h"
#include "Attrib.h"
#include "AttribGeom.h"
#include "WKG/WKG.h"
#include "Allocators.h"

namespace imqs {
namespace dba {
namespace WKB {

static void WKG_Read_Info(void* context, int* type, int* numparts, uint32_t* vxstride, ptrdiff_t* moffset, ptrdiff_t* zoffset, int* srid) {
	const Attrib* val = (const Attrib*) context;
	IMQS_ASSERT(GeomIsDouble(val->Value.Geom.Flags));

	switch (val->Type) {
	case Type::GeomPoint:
		*numparts = 1;
		*type     = WKG_Point;
		break;
	case Type::GeomMultiPoint:
		*numparts = 1;
		*type     = WKG_MultiPoint;
		break;
	case Type::GeomPolyline:
		*numparts = (int) val->GeomNumParts();
		*type     = *numparts > 1 ? WKG_MultiLineString : WKG_LineString;
		break;
	case Type::GeomPolygon: {
		*numparts = (int) val->GeomNumParts();
		*type     = val->GeomNumExternalRings() > 1 ? WKG_MultiPolygon : WKG_Polygon;
		break;
	}
	default:
		IMQS_DIE();
	}

	GeomFlags flags = val->Value.Geom.Flags;

	*zoffset = 0;
	*moffset = 0;

	*vxstride = GeomDimensions(flags);
	if (!!(flags & GeomFlags::HasZ) && !!(flags & GeomFlags::HasM)) {
		*zoffset = 2;
		*moffset = 3;
	} else if (!!(flags & GeomFlags::HasZ)) {
		*zoffset = 2;
	} else if (!!(flags & GeomFlags::HasM)) {
		*moffset = 2;
	}

	*srid = val->Value.Geom.Head->SRID;
}

static void WKG_Read_GetPart(void* context, int part, double** vertices, int* count, int* depth, int* closed) {
	const Attrib* val = (const Attrib*) context;
	IMQS_ASSERT(GeomIsDouble(val->Value.Geom.Flags));

	double*  vx   = (double*) val->GeomVertices();
	uint32_t dims = GeomDimensions(val->Value.Geom.Flags);

	if (val->IsPoly()) {
		int  pstart;
		auto pflags = val->GeomPart(part, pstart, *count);
		*closed     = !!(pflags & GeomPartFlag_Closed) ? 1 : 0;
		*depth      = !!(pflags & GeomPartFlag_ExteriorRing) ? WKG_RingOuter : WKG_RingInner;
		*vertices   = vx + dims * pstart;
	} else {
		// points
		*count    = (int) val->GeomTotalVertexCount();
		*vertices = vx;
	}
}

static uint32_t MakeWKGFlags(WriterFlags flags) {
	uint32_t f = 0;

	if (!!(flags & WriterFlags::Z))
		f |= WKG_Write_Z;
	if (!!(flags & WriterFlags::M))
		f |= WKG_Write_M;
	if (!!(flags & WriterFlags::EWKB))
		f |= WKG_Write_EWKB;
	if (!!(flags & WriterFlags::SRID))
		f |= WKG_Write_SRID;
	if (!!(flags & WriterFlags::Force_Multi))
		f |= WKG_Write_Force_Multi;
	if (!!(flags & WriterFlags::BigEndian))
		f |= WKG_Write_BigEndian;
	if (!!(flags & WriterFlags::LittleEndian))
		f |= WKG_Write_LittleEndian;

	return f;
}

size_t ComputeEncodedBytes(WriterFlags flags, const Attrib& val) {
	WKG_Reader reader;
	reader.Context = const_cast<Attrib*>(&val); // this is a safe cast - we do not modify Attrib, and always cast back to const Attrib*
	reader.Info    = WKG_Read_Info;
	reader.GetPart = WKG_Read_GetPart;
	return WKG_BinBytes(&reader, MakeWKGFlags(flags));
}

size_t Encode(WriterFlags flags, const Attrib& val, io::Buffer& buf) {
	Attrib     tmpCopy;
	WKG_Reader reader;
	reader.Info    = WKG_Read_Info;
	reader.GetPart = WKG_Read_GetPart;
	reader.Context = const_cast<Attrib*>(&val);
	if (val.Type == Type::GeomPolygon) {
		// To fix this, use SetPoly(), passing in your parts and vertices at the same time.
		// You want to get to the point inside Attrib::SetPolyInternal, where it calls geom::FixRingOrderWKB().
		IMQS_DEBUG_ASSERT(val.GeomIsWKBOrder());
		// Rather than crash in a release build, take the performance hit and fix up the geometry
		tmpCopy.CopyFrom(val);
		reader.Context = const_cast<Attrib*>(&tmpCopy);
	}
	size_t bytes = WKG_BinBytes(&reader, MakeWKGFlags(flags));
	buf.Ensure(bytes);
	size_t written = WKG_BinWrite(&reader, MakeWKGFlags(flags), buf.WritePos());
	IMQS_ASSERT(bytes == written);
	buf.Len += bytes;
	return bytes;
}

struct GeomParseData {
	smallvec<uint32_t> RingSize; // ring size and GeomPartFlags
	double*            Vert;
	bool               HasZ;
	bool               HasM;
};

static void Vertex_Count(WKG_Target* target, int part, int ring, int nvertex, int isClosed) {
	GeomParseData* data = (GeomParseData*) target->Context;
	uint32_t       rs   = nvertex;
	if (isClosed)
		rs |= GeomPartFlag_Closed;
	if (ring == 0 && (target->Type == WKG_Polygon || target->Type == WKG_MultiPolygon))
		rs |= GeomPartFlag_ExteriorRing;
	data->RingSize.push(rs);
}

static int Vertex_Decode(WKG_Target* target, int part, int ring, int vertex, int nvertex) {
	GeomParseData* data = (GeomParseData*) target->Context;
	data->Vert[0]       = target->VX;
	data->Vert[1]       = target->VY;
	int iv              = 2;
	if (data->HasZ)
		data->Vert[iv++] = target->VZ;
	if (data->HasM)
		data->Vert[iv++] = target->VM;
	data->Vert += iv;
	return WKG_BinParse_OK;
}

static Error MakeError(WKG_BinParseResult res) {
	switch (res) {
	case WKG_BinParse_OK: return Error();
	case WKG_BinParse_Stop: return Error("WKB parse aborted");
	case WKG_BinParse_Overrun: return Error("Invalid Well Known Binary geometry - buffer overrun");
	case WKG_BinParse_InvalidInput: return Error("Invalid Well Known Binary geometry - invalid data");
	}
	return Error("Unrecognized WKG error");
}

Error Decode(const void* buf, size_t len, Attrib& val, Allocator* alloc) {
	GeomParseData pdata;
	WKG_Target    target;
	target.Context = &pdata;
	target.Count   = Vertex_Count;
	target.Vertex  = Vertex_Decode;

	// Measure sizes, so that we can initialize attrib to correct size
	WKG_BinParseResult res = (WKG_BinParseResult) WKG_BinParse(buf, len, WKG_BinParseFlag_CountOnly, &target);
	if (res != WKG_BinParse_OK)
		return MakeError(res);

	// prepare attrib (alloc space for parts and vertices)
	// We trust that WKB data always has rings in the correct order. This is what the spec dictates,
	// but of course there may be rogue software out there that doesn't obey the rules.
	GeomFlags flags = GeomFlags::None;
	if (target.HasZ) {
		flags |= GeomFlags::HasZ;
		pdata.HasZ = true;
	}
	if (target.HasM) {
		flags |= GeomFlags::HasM;
		pdata.HasM = true;
	}

	switch (target.Type) {
	case WKG_LineString:
	case WKG_MultiLineString:
	case WKG_Polygon:
	case WKG_MultiPolygon: {
		// transform RingSize into Parts array
		// example: [5, 7, 2] -> [0, 5, 5+7, 5+7+2]
		uint32_t pos = 0;
		for (size_t i = 0; i < pdata.RingSize.size(); i++) {
			if (pos > GeomPartFlag_MaxVertices)
				return Error("Too many vertices in WKB");
			uint32_t ringLen   = pdata.RingSize[i] & GeomPartFlag_Mask;
			uint32_t ringFlags = pdata.RingSize[i] & ~GeomPartFlag_Mask;
			pdata.RingSize[i]  = pos | ringFlags;
			pos += ringLen;
		}
		// add sentinel
		pdata.RingSize.push(pos);
		break;
	}
	default:
		break;
	}

	switch (target.Type) {
	case WKG_Point:
		val.SetPoint(flags, (double*) nullptr, target.SRID, alloc);
		break;
	case WKG_MultiPoint:
		val.SetMultiPoint(flags, (int) pdata.RingSize[0], (double*) nullptr, target.SRID, alloc);
		break;
	case WKG_LineString:
	case WKG_MultiLineString:
		val.SetPoly(Type::GeomPolyline, flags, (int) pdata.RingSize.size() - 1, &pdata.RingSize[0], (double*) nullptr, target.SRID, alloc);
		break;
	case WKG_Polygon:
	case WKG_MultiPolygon:
		flags |= GeomFlags::RingsInWKBOrder;
		val.SetPoly(Type::GeomPolygon, flags, (int) pdata.RingSize.size() - 1, &pdata.RingSize[0], (double*) nullptr, target.SRID, alloc);
		break;
	}

	// decode vertices, and pack into Attrib
	pdata.Vert = (double*) val.GeomVertices();
	pdata.HasZ = !!target.HasZ;
	pdata.HasM = !!target.HasM;
	res        = (WKG_BinParseResult) WKG_BinParse(buf, len, 0, &target);
	if (res != WKG_BinParse_OK)
		return MakeError(res);

	return Error();
}
} // namespace WKB

namespace geom {

template <typename T>
struct RingInfo {
	uint32_t               Parent = -1;
	T                      Area;
	imqs::geom2d::BBox2<T> Bounds;
};

template <typename TReal>
void TFixRingOrderWKB(int vxDims, size_t numParts, const uint32_t* parts, const TReal* vx, uint32_t* newParts, TReal* newVx) {
	// The 'rings' vector is synonymous with 'parts'.
	smallvec<RingInfo<TReal>> rings;
	rings.resize(numParts);

	// Cache ring area and bounding box
	for (size_t i = 0; i < numParts; i++) {
		uint32_t start = parts[i] & GeomPartFlag_Mask;
		uint32_t end   = parts[i + 1] & GeomPartFlag_Mask;
		for (auto j = start; j < end; j++)
			rings[i].Bounds.ExpandToFit(vx[j * vxDims], vx[j * vxDims + 1]);
		rings[i].Area = (TReal) geom2d::PolygonArea(end - start, &vx[start * vxDims], vxDims);
	}

	// Build up an ownership map, showing which part is inside which other part.
	// If there is ambiguity, then we choose the largest (aka outer-most) containing part.
	// This ensures that even if there are nested parts (aka islands inside islands), then
	// those deeply nested islands will still belong to a single outer-most part.
	// Or put another way, you are either a child or a parent, but not both. This invariant
	// must hold true for the following phase, otherwise we could end up with lost
	// parts during the rewrite phase.
	for (size_t i = 0; i < numParts; i++) {
		// We only use the first vertex of part i to test for inclusion.
		// This will not work if the first vertex is on the boundary of the parent polygon.
		uint32_t istart         = parts[i] & GeomPartFlag_Mask;
		TReal    ix             = vx[istart * vxDims];
		TReal    iy             = vx[istart * vxDims + 1];
		size_t   bestParent     = -1;
		double   bestParentArea = -1;
		// Find the largest parent of 'i'
		for (size_t j = 0; j < numParts; j++) {
			if (i == j)
				continue;
			if (!rings[j].Bounds.IsInsideMe(rings[i].Bounds))
				continue;
			uint32_t jstart = parts[j] & GeomPartFlag_Mask;
			uint32_t jend   = parts[j + 1] & GeomPartFlag_Mask;
			if (geom2d::PtInsidePoly(ix, iy, jend - jstart, &vx[jstart * vxDims], vxDims)) {
				if (rings[j].Area > bestParentArea) {
					bestParent     = j;
					bestParentArea = rings[j].Area;
				}
			}
		}
		rings[i].Parent = (uint32_t) bestParent;
	}

	// Detect circular parents.
	// We first saw this where a Geometry had two identical parts. Our PtInsidePoly function
	// only works correctly if the rings do not touch.
	for (size_t i = 0; i < numParts; i++) {
		if (rings[i].Parent != -1 && rings[rings[i].Parent].Parent == i) {
			rings[rings[i].Parent].Parent = -1;
			rings[i].Parent               = -1;
		}
	}

	// Rewrite parts.
	uint32_t outPartPos = 0;
	uint32_t outVxPos   = 0;
	auto     rewrite    = [&](bool isTopLevel, size_t part) {
        uint32_t start       = parts[part] & GeomPartFlag_Mask;
        uint32_t end         = parts[part + 1] & GeomPartFlag_Mask;
        uint32_t len         = end - start;
        auto     orient      = geom2d::PolygonOrient(end - start, &vx[start * vxDims], vxDims);
        newParts[outPartPos] = outVxPos | GeomPartFlag_Closed;
        if (isTopLevel)
            newParts[outPartPos] |= GeomPartFlag_ExteriorRing;

        outPartPos++;
        // top-level parts must be CCW, and vice versa.
        bool reverse = (orient == geom2d::PolyOrient::CCW) != isTopLevel;
        if (reverse) {
            for (uint32_t i = 0; i < len; i++, outVxPos++)
                memcpy(&newVx[outVxPos * vxDims], &vx[(end - i - 1) * vxDims], vxDims * sizeof(TReal));
        } else {
            for (uint32_t i = 0; i < len; i++, outVxPos++)
                memcpy(&newVx[outVxPos * vxDims], &vx[(start + i) * vxDims], vxDims * sizeof(TReal));
        }
	};

	// Scan for top-level parts
	for (size_t i = 0; i < numParts; i++) {
		// A top-level part has no parent
		if (rings[i].Parent != -1)
			continue;

		// Rewrite the top-level part
		rewrite(true, i);

		// Find all children of part i, and write them out.
		for (size_t j = 0; j < numParts; j++) {
			if (rings[j].Parent == i)
				rewrite(false, j);
		}
	}

	// write sentinel
	newParts[outPartPos] = outVxPos;
}

void FixRingOrderWKB(bool isDoubles, int vxDims, size_t numParts, const uint32_t* parts, const void* vx, uint32_t* newParts, void* newVx) {
	if (isDoubles)
		TFixRingOrderWKB(vxDims, numParts, parts, (const double*) vx, newParts, (double*) newVx);
	else
		TFixRingOrderWKB(vxDims, numParts, parts, (const float*) vx, newParts, (float*) newVx);
}

} // namespace geom
} // namespace dba
} // namespace imqs
