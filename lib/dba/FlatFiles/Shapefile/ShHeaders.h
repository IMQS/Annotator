#pragma once

namespace imqs {
namespace dba {
namespace shapefile {

#pragma pack(push)
#pragma pack(1)

// These limits are here so that malicious input doesn't cause us to consume all available memory
static const int MaxPolyParts    = 10 * 1000;
static const int MaxPolyVertices = 10 * 1000 * 1000;

struct MainHeader {
	int32_t FileCode;
	int32_t Reserved[5];
	int32_t Length;
	int32_t Version;
	int32_t Type;
	double  Xmin;
	double  Ymin;
	double  Xmax;
	double  Ymax;
	double  Zmin;
	double  Zmax;
	double  Mmin;
	double  Mmax;
};

struct RecordHeader {
	int32_t Index;
	int32_t ContentLength;
};

struct RecordIndex {
	static RecordIndex Create(int32_t posBytes, int32_t contLenBytes) {
		RecordIndex ri;
		ri.Position      = posBytes / 2;
		ri.ContentLength = contLenBytes / 2;
		return ri;
	}

	int32_t Position;
	int32_t ContentLength;

	/// Returns the position of the first byte of the record header in the SHP file.
	size_t PosInShpFile() { return Position * 2; }

	/// Returns the total length in bytes of the feature in the SHP file, including the 8 byte header as well as the contents.
	size_t LengthInShpFile() { return 8 + ContentLength * 2; }
};

struct ShNull {
	int32_t Type;
};

struct ShPoint {
	int32_t Type;
	double  X;
	double  Y;
};

struct ShPointM {
	int32_t Type;
	double  X;
	double  Y;
	double  M;
};

struct ShPointMZ {
	int32_t Type;
	double  X;
	double  Y;
	double  Z;
	double  M;
};

struct ShMultiPoint {
	int32_t Type;
	double  Xmin;
	double  Ymin;
	double  Xmax;
	double  Ymax;
	int32_t PointCount;
	// Points follow
};

struct ShRange {
	ShRange() {}
	ShRange(double min, double max) {
		Min = min;
		Max = max;
	}
	double Min;
	double Max;
};

// Polyline / Polygon
struct ShPoly {
	int32_t Type;
	double  Xmin;
	double  Ymin;
	double  Xmax;
	double  Ymax;
	int32_t NumParts;
	int32_t NumPoints;
	// int32_t[NumParts] indices into points array follows.
	// ShPoint[NumPoints] follows.
};

enum ShapeType {
	ShapeTypeNull        = 0,
	ShapeTypePoint       = 1,
	ShapeTypePolyline    = 3,
	ShapeTypePolygon     = 5,
	ShapeTypeMultiPoint  = 8,
	ShapeTypePointZ      = 11,
	ShapeTypePolylineZ   = 13,
	ShapeTypePolygonZ    = 15,
	ShapeTypeMultiPointZ = 18,
	ShapeTypePointM      = 21,
	ShapeTypePolylineM   = 23,
	ShapeTypePolygonM    = 25,
	ShapeTypeMultiPointM = 28,
	ShapeTypeMultiPatch  = 31
};

bool        IsValidType(int t);
std::string DescribeType(ShapeType t);

inline bool IsZType(ShapeType type) {
	return type == ShapeTypePointZ ||
	       type == ShapeTypeMultiPointZ ||
	       type == ShapeTypePolylineZ ||
	       type == ShapeTypePolygonZ;
}

inline bool IsMType(ShapeType type) {
	return type == ShapeTypePointM ||
	       type == ShapeTypeMultiPointM ||
	       type == ShapeTypePolylineM ||
	       type == ShapeTypePolygonM;
}

inline bool IsZOrMType(ShapeType type) {
	return IsZType(type) || IsMType(type);
}

inline bool IsMultiPointType(ShapeType type) {
	return type == ShapeTypeMultiPoint ||
	       type == ShapeTypeMultiPointM ||
	       type == ShapeTypeMultiPointZ;
}

inline bool IsPointType(ShapeType type) {
	return type == ShapeTypePoint ||
	       type == ShapeTypePointM ||
	       type == ShapeTypePointZ;
}

inline bool IsPolylineType(ShapeType type) {
	return type == ShapeTypePolyline ||
	       type == ShapeTypePolylineM ||
	       type == ShapeTypePolylineZ;
}

inline bool IsPolygonType(ShapeType type) {
	return type == ShapeTypePolygon ||
	       type == ShapeTypePolygonM ||
	       type == ShapeTypePolygonZ;
}

enum PatchType {
	PatchTypeTriStrip  = 0,
	PatchTypeTriFan    = 1,
	PatchTypeOuterRing = 2,
	PatchTypeInnerRing = 3,
	PatchTypeFirstRing = 4,
	PatchTypeRing      = 5
};

#pragma pack(pop)
} // namespace shapefile
} // namespace dba
} // namespace imqs