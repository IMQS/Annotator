#include "pch.h"
#include "ShHeaders.h"

namespace imqs {
namespace dba {
namespace shapefile {

bool IsValidType(int t) {
	switch (t) {
	case ShapeTypePoint: return true;
	case ShapeTypePolyline: return true;
	case ShapeTypePolygon: return true;
	case ShapeTypeMultiPoint: return true;
	case ShapeTypePointZ: return true;
	case ShapeTypePolylineZ: return true;
	case ShapeTypePolygonZ: return true;
	case ShapeTypeMultiPointZ: return true;
	case ShapeTypePointM: return true;
	case ShapeTypePolylineM: return true;
	case ShapeTypePolygonM: return true;
	case ShapeTypeMultiPointM: return true;
	default: return false;
	}
}

std::string DescribeType(ShapeType t) {
	switch (t) {
	case ShapeTypeNull: return "Null";
	case ShapeTypePoint: return "Point";
	case ShapeTypePolyline: return "Polyline";
	case ShapeTypePolygon: return "Polygon";
	case ShapeTypeMultiPoint: return "MultiPoint";
	case ShapeTypePointZ: return "PointZ";
	case ShapeTypePolylineZ: return "PolylineZ";
	case ShapeTypePolygonZ: return "PolygonZ";
	case ShapeTypeMultiPointZ: return "MultiPointZ";
	case ShapeTypePointM: return "PointM";
	case ShapeTypePolylineM: return "PolylineM";
	case ShapeTypePolygonM: return "PolygonM";
	case ShapeTypeMultiPointM: return "MultiPointM";
	case ShapeTypeMultiPatch: return "MultiPatch";
	default: return "INVALID";
	}
}

} // namespace shapefile
} // namespace dba
} // namespace imqs