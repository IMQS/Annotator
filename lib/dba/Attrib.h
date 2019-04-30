#pragma once

namespace imqs {
namespace dba {

#pragma pack(push)
#pragma pack(4)

class Allocator;

namespace varargs {
class Arg;
class OutArg;
} // namespace varargs

/* Variant attribute type that can hold any type of DB value (text, double, geometry, etc)

Attrib is a 16-byte union that can represent any of the values that the dba library supports.

For small data types, such as int32 or double, we store them inside the Attrib. Larger elements,
such as text and geometry, are stored externally, and Attrib maintains a pointer to the
actual storage location.

When storing larger elements (such as text or geometry), there are two types of locations where
external data can exist:
1. A thread-local heap
2. A custom allocator

The thread-local heap is managed by the MemPool class. Because this heap is thread-local, you cannot
copy Attribs from one thread to another, if their storage is on the thread-local heap. If you want
to copy Attribs between threads, then you need to make sure that they use option (2) - a custom
allocator, and you need to manage the lifetime of that custom allocator.

If an Attrib's data is stored on the thread-local heap, then it will free it's data whenever Reset
is called, or whenever Attrib's destructor runs.

However, if an Attrib's data comes from a custom allocator, then it will never make any attempt to
free that memory. This is evident from the fact that Allocator interface has no "free" method on it.
In other words, all of our custom allocators embrace the concept that a whole bunch of Attrib
storage is allocated bit by bit, and then freed all together in a single operation. This happens to
fit most use cases, for example when allocating storage for a result row of an SQL query.

Geometry Formats

Geometry is stored as either floats or doubles, and each vertex has either 2, 3, or 4 dimensions.
When 2 dimensions, a vertex is XY. If LatLon, then X = Lon and Y = Lat. If a vertex has 4
dimensions, then it has Z and M. In that case, the order is XYZM.

Float storage is specified in flags, and some provision has been made to support it in future,
but I haven't bothered to write all the code for it yet. I think it may be useful during rendering,
for example, if you get the database to project and clip for you, then float precision will be
sufficient, and you can save on 50% of your memory bandwidth.

Polyline/Polygon Parts

Polylines and Polygons consist of one or more parts. Each part is a connected set of line segments.
For a polygon, those line segments must join back on themselves, so that each part forms a closed
loop. For a polyline however, each part can optionally be closed or open. Many systems force one
to specify the starting vertex twice - once at the start, and once at the end. We don't do that.
Here, if your polyline part has 3 vertices, and it's closed, then you specify exactly three vertices,
and you include the part flag that tells us that it's closed.

The parts array looks like this:
[Part0, Part1,..., PartN-1, Sentinel] (array of uint32)
Each member of the parts array points to the first vertex of that part. The vertex list is a
continuous array of vertices, in the same order as the parts. Each 32-bit member of the part array has
some reserved high bits. The only bit presently used is bit 31 (ie the MSB). If that bit is set, then the
part is closed. We reserve other bits for storing things such as the winding order of the part
(ie if it's clockwise or counterclockwise, etc). A part can have at most 1 million vertices, but this
limit is arbitrary. It could be significantly higher, because we're unlikely to need 11 bits of extra data
for each part. However, one million seems like a reasonable limit anyway.

Example of a Polyline

    Vertices [a1, a2, a3, a4, b1, b2, b3, b4, b5, b6, b7]
    Parts    [0x80000000, 4, 11]

In this example, there are two parts. The first part has the GeomPartFlag_Closed flag set, which means
it is a closed. If we mask out the high 12 bits, then we're left with the value 0, which is the first
vertex of the part. (The first vertex of the first part must always be zero). The second part starts
at vertex 4. This tells us that the length of the first part is 4. So the vertices a1..a4 belong to
the first part. The second part is not closed, and it starts at vertex 4. It's length can be computed
as 11 - 4. The "11" is the sentinel part. It is equal to the total number of vertices. One has to store
the vertex count somewhere, and storing it this way means the part processing code doesn't need any
special logic for the last part.

The memory layout of polyline/polygon geometry data is

GeomHeader    Attrib::GeomHeader
Parts         array of uint32, padded up to an even number of 32-bit words
Vertices      array of doubles or floats

GeomHeader is 8 bytes, which causes Parts to be 8-byte aligned. If the Parts array has an odd number
of elements, then one extra element of padding is added after the parts, so that the vertices are
8-byte aligned.
We cannot store Vertices before Parts, because we need to be able to read the final element of the
Parts array in order to know how many vertices we have. But we cannot read the Parts array, because
we don't know where it starts, because we don't know how many vertices we have.
This is a chicken-and-egg problem.

The memory layout for points and multipoints is

GeomHeader
Vertices

(i.e. same as polyline/polygon, but without the parts)

WKB Ring Ordering

Well Known Binary dictates that outer rings are followed by their children inner rings.
Also, outer rings must be counter-clockwise, and inner rings must be clockwise. When
SetPoly is called, we check if the RingsInWKBOrder flag is set. If it's not set, then
we re-order the rings so that they adhere to the WKB ordering rules. Thereafter, we leave
the RingsInWKBOrder flag set on our geom flags. If you know that your data already
adheres to the spec, then you can set the flag, and avoid the potentially expensive
re-ordering operation.
If you do set the flag, then you must also ensure that your parts have the
GeomPartFlag_ExteriorRing flag set on all exterior rings, since RingsInWKBOrder implies
that also.

*/
class IMQS_DBA_API Attrib {
public:
	enum Flags : uint8_t {
		None       = 0,
		CustomHeap = 1, // Storage is on custom heap, so do not free on Reset/destruction
	};

	// We could theoretically go up to 2^31, but if you're working with massive blobs,
	// you should be dealing with them differently.
	static const size_t MaxDataLen = 1024 * 1024 * 1024;

	// This must be 8 byte aligned, so that we can pack doubles immediately after it
	struct GeomHeader {
		uint32_t NumParts; // Applies to all geometry types. For Points, NumParts = 1
		int32_t  SRID;     // If positive, then EPSG code. If negative, then a temporary projwrap code
	};

	union {
		bool    Bool;
		int16_t Int16;
		int32_t Int32;
		int64_t Int64;
		float   Float;
		double  Double;
		int32_t Time;
		Guid*   Guid;
		struct
		{
			int64_t Sec; // Internal representation of imqs::time::Time
			int32_t Nsec;
		} Date;
		struct
		{
			uint8_t* Data;
			int32_t  Size;
		} Bin;
		struct
		{
			// This is used by JSONB and Text
			char*   Data; // Null terminated
			int32_t Size; // Length excluding null terminator
		} Text;
		struct
		{
			GeomHeader* Head;
			GeomFlags   Flags;
		} Geom;
	} Value;

	Type    Type;
	uint8_t Flags;
	uint8_t __Padding[2];

	Attrib() {
		Type  = Type::Null;
		Flags = 0;
	}
	Attrib(const Attrib& b);
	Attrib(Attrib&& b);
	~Attrib();

	Attrib(const char* str, Allocator* alloc = nullptr);
	Attrib(const void* buf, size_t len, dba::Type type, Allocator* alloc = nullptr); // Can construct Text or Bin. len is allowed to be -1 for Text, but must be specified for Bin.
	Attrib(const std::string& str, Allocator* alloc = nullptr);
	Attrib(bool val);
	Attrib(int16_t val);
	Attrib(int32_t val);
	Attrib(int64_t val);
	Attrib(size_t val);
	Attrib(float val);
	Attrib(double val);
	Attrib(imqs::Guid val, Allocator* alloc = nullptr);
	Attrib(time::Time val);
	Attrib(const rapidjson::Value& val, Allocator* alloc = nullptr);
	Attrib(const nlohmann::json& val, Allocator* alloc = nullptr);

	void SetNull();
	void SetBool(bool val);
	void SetInt16(int16_t val);
	void SetInt32(int32_t val);
	void SetInt64(int64_t val);
	void SetFloat(float val);
	void SetDouble(double val);
	void SetText(const char* str, size_t len = -1, Allocator* alloc = nullptr); // If 'str' is null, then the data is just allocated, and you can fill it in yourself
	void SetText(const std::string& str, Allocator* alloc = nullptr);
	void SetJSONB(const char* str, size_t len = -1, Allocator* alloc = nullptr); // If 'str' is null, then the data is just allocated, and you can fill it in yourself
	void SetJSONB(const std::string& str, Allocator* alloc = nullptr);
	void SetJSONB(const rapidjson::Value& val, Allocator* alloc = nullptr);
	void SetJSONB(const nlohmann::json& val, Allocator* alloc = nullptr);
	void SetGuid(const Guid& val, Allocator* alloc = nullptr);
	void SetDate(time::Time val);
	void SetBin(const void* val, size_t len, Allocator* alloc = nullptr);
	void SetPoint(GeomFlags flags, const float* vx, int srid, Allocator* alloc = nullptr);
	void SetPoint(GeomFlags flags, const double* vx, int srid, Allocator* alloc = nullptr);
	void SetMultiPoint(GeomFlags flags, int numPoints, const float* vx, int srid, Allocator* alloc = nullptr);
	void SetMultiPoint(GeomFlags flags, int numPoints, const double* vx, int srid, Allocator* alloc = nullptr);
	void SetPoly(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const float* vx, int srid, Allocator* alloc = nullptr);  // See comment about WKB Ring Order
	void SetPoly(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const double* vx, int srid, Allocator* alloc = nullptr); // See comment about WKB Ring Order
	void SetPolyline(GeomFlags flags, int numVertices, const double* vx, int srid, Allocator* alloc = nullptr);                                 // numVertices can have GeomPartFlag_Closed, to create a closed polyline
	void Set(const varargs::Arg& arg, Allocator* alloc);

	static Attrib MakeJSONB(const std::string& str, Allocator* alloc = nullptr);
	static Attrib MakeJSONB(const char* str, size_t len = -1, Allocator* alloc = nullptr);

	static Attrib MakePoint(double x, double y, int srid, Allocator* alloc = nullptr);
	static Attrib MakePolylineXY(size_t n, const double* xy, int srid, Allocator* alloc = nullptr);

	// These functions are for injecting some state into an Attrib, so that there is no memory copy for
	// things like text and binary blobs. Obviously, the memory that you use here must outlive the Attrib object.
	// See also SetTempGeomRaw()
	void SetTempText(const char* str, size_t len);
	void SetTempJSONB(const char* str, size_t len);
	void SetTempBin(const void* bin, size_t len);
	void SetTempGuid(const Guid* g);

	bool    ToBool() const;
	int16_t ToInt16() const;
	int32_t ToInt32() const;
	int64_t ToInt64() const;
	float   ToFloat() const;
	double  ToDouble() const;
	// 'buf' is a buffer of 'bufLen' bytes, that will hold the resulting string
	// and the null terminator.
	// In all cases, ToText returns the exact number of bytes necessary in 'buf'.
	// If 'bufLen' is less than that number of bytes, then nothing is written.
	size_t      ToText(char* buf, size_t bufLen) const;
	Guid        ToGuid() const;
	time::Time  ToDate() const;
	void        ToJson(rapidjson::Value& out, rapidjson::Document::AllocatorType& allocator) const;
	std::string ToSQLString() const;
	Error       FromJson(const rapidjson::Value& in, dba::Type fieldType, Allocator* alloc = nullptr);
	std::string ToWKTString() const;
	std::string ToString() const;
	const char* RawString() const; // Asserts if type is not text
	size_t      TextLen() const;   // Asserts if type is not text

	// For a version of ConvertTo() that can take an allocator, see CopyTo()
	Attrib ConvertTo(dba::Type dstType) const;
	Error  AssignTo(imqs::dba::varargs::OutArg& arg) const;

	template <typename T>
	void To(T& dst) const;
	void To(bool& dst) const { dst = ToBool(); }
	void To(int16_t& dst) const { dst = ToInt16(); }
	void To(int32_t& dst) const { dst = ToInt32(); }
	void To(int64_t& dst) const { dst = ToInt64(); }
	void To(size_t& dst) const { dst = ToInt64(); }
	void To(std::string& dst) const { dst = ToString(); }
	void To(time::Time& dst) const { dst = ToDate(); }
	void To(Attrib& dst) const { dst = *this; }

	bool IsNull() const { return Type == Type::Null; }
	bool IsNumeric() const;    // Return true if Type is Int32, Int64, or Double
	bool IsInt16() const;      // Return true if Type is Int16
	bool IsInt32() const;      // Return true if Type is Int32
	bool IsInt64() const;      // Return true if Type is Int64
	bool IsFloat() const;      // Return true if Type is Float
	bool IsDouble() const;     // Return true if Type is Double
	bool IsText() const;       // Return true if Type is Text
	bool IsBool() const;       // Return true if Type is Bool
	bool IsPoint() const;      // Return true if Type is GeomPoint
	bool IsMultiPoint() const; // Return true if Type is GeomMultiPoint
	bool IsGeom() const;       // Return true if Type is GeomPoint, GeomMultiPoint, GeomPolygon, or GeomPolyline
	bool IsPoly() const;       // Return true if Type is GeomPolygon or GeomPolyline
	bool IsDate() const;       // Return true if Type is Date
	bool IsBin() const;        // Return true if Type is Bin
	bool IsJSONB() const;      // Return true if Type is JSONB

	// Returns +1 if this > b; zero if equal; -1 if this < b.
	int Compare(const Attrib& b) const;

	// If not a numeric type, uses ToDouble(), and then compares.
	// The idea is that at least one of the attributes is a numeric type, and the other one is a string.
	int CompareAsNum(const Attrib& b) const;

	// If dstType is Null, then copy directly, do not perform any conversion.
	void CopyTo(dba::Type dstType, Attrib& dst, Allocator* alloc = nullptr) const;
	void CopyFrom(const Attrib& b, Allocator* alloc = nullptr);

	// Get the starting address of our dynamic storage, or nullptr if this object has no dynamic storage
	void* DynData() const;

	int32_t    UnixSeconds32() const;  // Only applicable to Date type. Returns zero otherwise
	int64_t    UnixSeconds64() const;  // Only applicable to Date type. Returns zero otherwise
	double     UnixSecondsDbl() const; // Only applicable to Date type. Returns zero otherwise
	time::Time Date() const;           // Only applicable to Date type. Returns null Date otherwise

	uint32_t      GeomNumParts() const;
	GeomPartFlags GeomPart(size_t part, int& start, int& count) const;
	bool          GeomPartIsClosed(size_t part) const;
	void*         GeomVertices();
	const void*   GeomVertices() const;
	double*       GeomVerticesDbl();
	const double* GeomVerticesDbl() const;
	size_t        GeomTotalVertexCount() const;
	bool          GeomHasZ() const;
	bool          GeomHasM() const;
	size_t        GeomDimensions() const;
	bool          GeomIsDoubles() const;
	void          GeomAlterStorage(GeomFlags newFlags, Allocator* alloc = nullptr); // Alter [float/double, hasZ, hasM] attributes of geometry.
	bool          GeomConvertSRID(int newSRID);
	size_t        GeomRawSize() const;
	void          GeomCopyRawOut(void* dst) const;                                        // Copy our raw contents out into a buffer of size GeomRawSize()
	void          GeomCopyRawIn(const void* src, size_t len, Allocator* alloc = nullptr); // Copy our raw contents back in from a buffer that was written to with GeomCopyRawOut().
	void          SetTempGeomRaw(const void* src, size_t len);                            // Directly assign pointer
	const double* GeomFirstVertex() const;
	const double* GeomLastVertex() const;
	uint32_t      GeomNumExternalRings() const; // Only applicable to Polygon. For other types, returns GeomNumParts(). If GeomIsWKBOrder() is false, then this will return 0.
	bool          GeomIsWKBOrder() const { return !!(Value.Geom.Flags & GeomFlags::RingsInWKBOrder); }
	int           GeomSRID() const { return Value.Geom.Head->SRID; }

	Attrib& operator=(const Attrib& b);
	Attrib& operator=(Attrib&& b);
	bool    operator==(const Attrib& b) const;
	bool    operator!=(const Attrib& b) const;
	bool    operator<(const Attrib& b) const;
	bool    operator<=(const Attrib& b) const;
	bool    operator>(const Attrib& b) const;
	bool    operator>=(const Attrib& b) const;

	ohash::hashkey_t        GetHashCode() const;
	static ohash::hashkey_t gethashcode(const dba::Attrib& a);

private:
	void            Reset();
	void            Free();
	void            SetTextWithLen(const char* str, size_t len, Allocator* alloc = nullptr);
	void            SetJSONBWithLen(const char* str, size_t len, Allocator* alloc = nullptr);
	void            PrepareTextOrJSONB(size_t len, Allocator* alloc = nullptr);
	void            PrepareText(size_t len, Allocator* alloc = nullptr);
	void            PrepareJSONB(size_t len, Allocator* alloc = nullptr);
	void            CopyVertexIn(void* dst, const void* src, GeomFlags flags);
	void            CopyVertexIn(void* dst, const void* src, GeomFlags flags, size_t vertexSize);
	void            CopyVerticesIn(int numVerts, const void* vx, GeomFlags flags);
	void            SetPointsInternal(dba::Type type, int numParts, GeomFlags flags, const void* vx, int srid, Allocator* alloc = nullptr);
	void            SetPolyInternal(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const void* vx, int srid, Allocator* alloc = nullptr);
	uint32_t*       GeomParts();
	const uint32_t* GeomParts() const;
	bool            GeomEquals(const Attrib& b) const;

	bool TextEq(const char* s) const;
	bool TextEqNoCase(const char* s) const;

	static size_t PolyPartArraySize(int numParts);
};
#pragma pack(pop)

class AttribPtrGetHashCode {
public:
	static ohash::hashkey_t gethashcode(const Attrib* key) {
		return key->GetHashCode();
	}
};

template <class TPtr>
class AttribPtrGetKey_Set {
public:
	static const TPtr& getkey(const TPtr& data) { return data; }
	static bool        equals(const TPtr& a, const TPtr& b) { return *a == *b; } // this is the only difference to the standard ohash helper class: (*a == *b) vs (a == b)
};

template <class TPtr, class TVal>
class AttribPtrGetKey_Pair {
public:
	static const TPtr& getkey(const std::pair<TPtr, TVal>& data) { return data.first; }
	static bool        equals(const TPtr& a, const TPtr& b) { return *a == *b; } // this is the only difference to the standard ohash helper class: (*a == *b) vs (a == b)
};

inline bool Attrib::IsNumeric() const {
	return dba::IsTypeNumeric(Type);
}

inline bool Attrib::IsInt16() const {
	return Type == Type::Int16;
}

inline bool Attrib::IsInt32() const {
	return Type == Type::Int32;
}

inline bool Attrib::IsInt64() const {
	return Type == Type::Int64;
}

inline bool Attrib::IsFloat() const {
	return Type == Type::Float;
}

inline bool Attrib::IsDouble() const {
	return Type == Type::Double;
}

inline bool Attrib::IsText() const {
	return Type == Type::Text;
}

inline bool Attrib::IsBool() const {
	return Type == Type::Bool;
}

inline bool Attrib::IsPoint() const {
	return Type == Type::GeomPoint;
}

inline bool Attrib::IsMultiPoint() const {
	return Type == Type::GeomMultiPoint;
}

inline bool Attrib::IsPoly() const {
	return Type == Type::GeomPolygon || Type == Type::GeomPolyline;
}

inline bool Attrib::IsGeom() const {
	return Type == Type::GeomPoint || Type == Type::GeomMultiPoint || Type == Type::GeomPolygon || Type == Type::GeomPolyline;
}

inline bool Attrib::IsDate() const {
	return Type == Type::Date;
}

inline bool Attrib::IsBin() const {
	return Type == Type::Bin;
}

inline bool Attrib::IsJSONB() const {
	return Type == Type::JSONB;
}

} // namespace dba
} // namespace imqs

namespace ohash {
template <>
inline hashkey_t IMQS_DBA_API gethashcode(const imqs::dba::Attrib& a) {
	return a.GetHashCode();
};
} // namespace ohash
