#pragma once

namespace imqs {
namespace dba {

// We don't support a generic AutoIncrement concept, because Postgres and Oracle are the
// only databases that we frequently work with, which support that generically, on any field.
// If you want an AutoIncrement field, then it must be the primary key.
enum class TypeFlags {
	None          = 0,
	NotNull       = 1,
	AutoIncrement = 2,
	GeomHasZ      = 4,
	GeomHasM      = 8,
	GeomNotMulti  = 16, // our default is to create a MultiLineString or MultiPolygon field. Setting this flag prevents that, so that you end up with a LineString field, for example.
};

inline TypeFlags  operator~(TypeFlags a) { return (TypeFlags) ~((uint32_t) a); }
inline TypeFlags  operator|(TypeFlags a, TypeFlags b) { return (TypeFlags)((uint32_t) a | (uint32_t) b); }
inline uint32_t   operator&(TypeFlags a, TypeFlags b) { return (uint32_t) a & (uint32_t) b; }
inline TypeFlags& operator|=(TypeFlags& a, TypeFlags b) {
	a = a | b;
	return a;
}

// Fundamental field types supported by dba
// To add a new type:
// 1. Make sure you really need to
// 2. Add it here, and go through Attrib.cpp, adding it to all of the switch statements
// 3. Possibly make it optional for a driver to support, by adding a new flag such as SqlDialectFlags::Float
// 4. Add support for it to the relevant drivers, and add unit tests in TestDrivers.cpp. Don't neglect TestDrivers_Postgres where we test COPY
// 5. Add relevant tests in TestDriver_SchemaReader
// 6. Add to parse_type() in SchemaParser.cpp
// 7. Search for "case Type::Int32" to find many other switch statements
// 8. Search for "case dba::Type::Int32" to find many other switch statements
enum class Type : uint8_t {
	Null   = 0, // This is a meta-type, because it is only used to indicate a null feature. It is not a real field type.
	Bool   = 1,
	Int16  = 2,
	Int32  = 3,
	Int64  = 4,
	Float  = 5,
	Double = 6,
	Text   = 7,
	Guid   = 8,
	// A Date & Time measurement (spanning any conceivable point in time).
	// For databases that have a day-only measurement (ie no time of the day), this
	// field is used. Most databases call this 'TimeStamp'.
	Date  = 9,
	Time  = 10, // A Time only measurement (spanning the 24 hours of a day). Most databases call this 'Time'.
	Bin   = 11, // Generic binary field.
	JSONB = 12, // Binary JSON. Inside Attrib, we store it as plain old textual JSON.
	// Geometry has the magic property that bit 5 (decimal 16) is always set.
	GeomPoint      = 16,
	GeomMultiPoint = 17,
	GeomPolyline   = 18,
	GeomPolygon    = 19,
	GeomAny        = 20, // Generic geometry field. Never used as an Attrib::Type. Only in Field::Type.
};

static_assert((int) Type::GeomAny < 32, "For geometry bitwise checks to work, geom types must be between 16 and32");

inline bool IsTypeGeom(Type t) {
	return !!((int) t & (int) Type::GeomPoint);
}

inline bool IsTypeGeomPoly(Type t) {
	return t == Type::GeomPolygon || t == Type::GeomPolyline;
}

inline bool IsTypeInt(Type t) {
	return t == Type::Int16 || t == Type::Int32 || t == Type::Int64;
}

inline bool IsTypeNumeric(Type t) {
	return t == Type::Int16 || t == Type::Int32 || t == Type::Int64 || t == Type::Float || t == Type::Double;
}

// Make all exactly 16 bytes, to help speed up comparisons
static const char* FieldNameTable_Names[] =
    {
#define FOUR "\0\0\0\0"
#define EIGHT "\0\0\0\0\0\0\0\0"
#define TWELVE "\0\0\0\0\0\0\0\0\0\0\0\0"
        // SYNC-FIELD_NAME_TABLE
        "null" TWELVE,
        "bool" TWELVE,
        "int16\0\0\0" EIGHT,
        "int32\0\0\0" EIGHT,
        "int64\0\0\0" EIGHT,
        "float\0\0\0" EIGHT,
        "double\0\0" EIGHT,
        "text" TWELVE,
        "date" TWELVE,
        "datetime" EIGHT,
        "guid" TWELVE,
        "time" TWELVE,
        "bin\0" TWELVE,
        "jsonb\0\0\0" EIGHT,
        "point\0\0\0" EIGHT,
        "multipoint\0\0" FOUR,
        "polyline" EIGHT,
        "polygon\0" EIGHT,
        "geometry" EIGHT,
#undef FOUR
#undef EIGHT
#undef TWELVE
};

// NOTE: To Parse a field type, see dba::schema::Field::ParseType()

inline const char* FieldTypeToString(Type ft, TypeFlags typeFlags = TypeFlags::None) {
	// Make all these values
	//bool hasZ = !!(typeFlags & FieldTypeFlagGeomHasZ);
	//bool hasM = !!(typeFlags & FieldTypeFlagGeomHasM);
	switch (ft) {
	// SYNC-FIELD_NAME_TABLE (array indices must be 0..N-1)
	case Type::Null: return FieldNameTable_Names[0];
	case Type::Bool: return FieldNameTable_Names[1];
	case Type::Int16: return FieldNameTable_Names[2];
	case Type::Int32: return FieldNameTable_Names[3];
	case Type::Int64: return FieldNameTable_Names[4];
	case Type::Float: return FieldNameTable_Names[5];
	case Type::Double: return FieldNameTable_Names[6];
	case Type::Text: return FieldNameTable_Names[7];
	case Type::Date: return FieldNameTable_Names[9];
	case Type::Guid: return FieldNameTable_Names[10];
	case Type::Time: return FieldNameTable_Names[11];
	case Type::Bin: return FieldNameTable_Names[12];
	case Type::JSONB: return FieldNameTable_Names[13];
	case Type::GeomPoint:
		/*if (hasZ && hasM)	return "pointzm";
		else if (hasZ)	return "pointz";
		else if (hasM)	return "pointm";*/
		return "point";
	case Type::GeomMultiPoint:
		/*if (hasZ && hasM)	return "multipointzm";
		else if (hasZ)	return "multipointz";
		else if (hasM)	return "multipointm";*/
		return "multipoint";
	case Type::GeomPolyline:
		/*if (hasZ && hasM)	return "polylinezm";
		else if (hasZ)	return "polylinez";
		else if (hasM)	return "polylinem";*/
		return "polyline";
	case Type::GeomPolygon:
		/*if (hasZ && hasM)	return "polygonzm";
		else if (hasZ)	return "polygonz";
		else if (hasM)	return "polygonm";*/
		return "polygon";
	case Type::GeomAny:
		/*if (hasZ && hasM)	return "geometryzm";
		else if (hasZ)	return "geometryz";
		else if (hasM)	return "geometrym";*/
		return "geometry";
	default: assert(false); return "";
	}
}

// Geometry Flags.
// These flags are stored inside Attrib.Value.Geom.Flags
enum class GeomFlags : uint32_t {
	None   = 0,
	Double = 1, // You must pick either Double or Float
	Float  = 2,
	HasZ   = 4,
	HasM   = 8,
	// Toggle this if your parts and rings are already in WKB order, and GeomPartFlag_ExteriorRing is set on exterior rings.
	// If this is toggled, then your outer rings are counter-clockwise, and your inner rings are clockwise.
	// Also, the order of the rings is, for example, OuterA, innerA1, innerA2, OuterB, innerB1, innerB2, innerB3.
	// In other words, an outer ring is followed immediately by all of it's inner rings.
	RingsInWKBOrder = 16,
	// The following types are only used internally, when serializing geometry to/from a memory buffer.
	PackedTypeShift      = 16,
	PackedTypeMask       = 31 << 16,
	PackedTypePoint      = (uint32_t) Type::GeomPoint << 16,
	PackedTypeMultiPoint = (uint32_t) Type::GeomMultiPoint << 16,
	PackedTypePolyline   = (uint32_t) Type::GeomPoint << 16,
	PackedTypePolygon    = (uint32_t) Type::GeomPoint << 16,
};

inline GeomFlags  operator|(GeomFlags a, GeomFlags b) { return (GeomFlags)((uint32_t) a | (uint32_t) b); }
inline uint32_t   operator&(GeomFlags a, GeomFlags b) { return (uint32_t) a & (uint32_t) b; }
inline GeomFlags& operator|=(GeomFlags& a, GeomFlags b) {
	a = (GeomFlags)((uint32_t) a | (uint32_t) b);
	return a;
}
inline uint32_t  GeomDimensions(GeomFlags f) { return 2 + (!!(f & GeomFlags::HasZ) ? 1 : 0) + (!!(f & GeomFlags::HasM) ? 1 : 0); }
inline size_t    GeomBytesPerVertex(GeomFlags f) { return (size_t) GeomDimensions(f) * (!!(f & GeomFlags::Float) ? 4 : 8); }
inline bool      GeomHasZ(GeomFlags f) { return !!(f & GeomFlags::HasZ); }
inline bool      GeomHasM(GeomFlags f) { return !!(f & GeomFlags::HasM); }
inline bool      GeomIsFloat(GeomFlags f) { return !!(f & GeomFlags::Float); }
inline bool      GeomIsDouble(GeomFlags f) { return !!(f & GeomFlags::Double); }
inline Type      GeomTypeFromPackedFlags(GeomFlags f) { return (Type)(((uint32_t) f & (uint32_t) GeomFlags::PackedTypeMask) >> (uint32_t) GeomFlags::PackedTypeShift); }
inline GeomFlags GeomMakePackedTypeFlag(Type t) { return (GeomFlags)((uint32_t) t << (uint32_t) GeomFlags::PackedTypeShift); }

enum GeomPartFlags : uint32_t {
	GeomPartFlag_Closed       = 0x80000000, // Part is closed
	GeomPartFlag_ExteriorRing = 0x40000000, // This is an exterior ring. The rings that follow are interior (until you hit another exterior, or the end)
	GeomPartFlag_Mask         = 0x000fffff,
	GeomPartFlag_MaxVertices  = GeomPartFlag_Mask - 1,
};

enum class SqlDialectFlags : uint64_t {
	None                         = 0,
	MultiRowInsert               = 1,    // DB can accept INSERT INTO table (a,b,c) VALUES ($1,$2,$3),($4,$5,$6),($7,$8,$9),...
	MultiRowDummyUnionInsert     = 2,    // DB can accept INSERT INTO table (a,b,c) (SELECT $1,$2,$3 FROM dummy UNION SELECT $4,$5,$6 FROM dummy UNION ...)
	AlterSchemaInsideTransaction = 4,    // DB can perform schema changes inside a transaction
	UUID                         = 8,    // DB has a UUID field type
	GeomZ                        = 16,   // DB geometry can store a "Z" dimension
	GeomM                        = 32,   // DB geometry can store an "M" dimension
	SpatialIndex                 = 64,   // DB can create a spatial index
	GeomSpecificFieldTypes       = 128,  // DB has specific geometry column types such as point, linestring, etc. If flag is not present, then all geometry goes into the same type of column
	Int16                        = 256,  // DB has a 16-bit integer type (signed)
	Float                        = 512,  // DB has a float32 type
	JSONB                        = 1024, // DB has a JSONB type
	NamedSchemas                 = 2048, // DB has named schemas
};

inline SqlDialectFlags operator|(SqlDialectFlags a, SqlDialectFlags b) { return (SqlDialectFlags)((uint64_t) a | (uint64_t) b); }
inline uint64_t        operator&(SqlDialectFlags a, SqlDialectFlags b) { return (uint64_t) a & (uint64_t) b; }

} // namespace dba
} // namespace imqs
