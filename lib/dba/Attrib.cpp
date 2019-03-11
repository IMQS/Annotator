#include "pch.h"
#include "Attrib.h"
#include "AttribGeom.h"
#include "mem.h"
#include "Allocators.h"
#include "Geom.h"
#include "Containers/VarArgs.h"
#include "WKG/WKT.h"

namespace imqs {
namespace dba {

const size_t Attrib::MaxDataLen;

static int Lower(int c) {
	return (c >= 'A' && c <= 'Z') ? c + 'a' - 'A' : c;
}

Attrib::~Attrib() {
	Free();
}

Attrib::Attrib(const Attrib& b) {
	Type  = Type::Null;
	Flags = 0;
	*this = b;
}

Attrib::Attrib(Attrib&& b) {
	Type  = Type::Null;
	Flags = 0;
	*this = std::move(b);
}

Attrib::Attrib(const char* str, Allocator* alloc) : Type(Type::Null), Flags(0) {
	SetText(str, -1, alloc);
}

Attrib::Attrib(const void* buf, size_t len, dba::Type type, Allocator* alloc) : Type(Type::Null), Flags(0) {
	if (type == Type::Text)
		SetText((const char*) buf, len, alloc);
	else if (type == Type::Bin)
		SetBin(buf, len, alloc);
	else
		IMQS_DIE_MSG("Invalid Attrib construction");
}

Attrib::Attrib(const std::string& str, Allocator* alloc) : Type(Type::Null), Flags(0) {
	SetText(str.c_str(), str.length(), alloc);
}

Attrib::Attrib(bool val) : Type(Type::Bool), Flags(0) {
	Value.Bool = val;
}

Attrib::Attrib(int16_t val) : Type(Type::Int16), Flags(0) {
	Value.Int16 = val;
}

Attrib::Attrib(int32_t val) : Type(Type::Int32), Flags(0) {
	Value.Int32 = val;
}

Attrib::Attrib(int64_t val) : Type(Type::Int64), Flags(0) {
	Value.Int64 = val;
}

Attrib::Attrib(size_t val) : Type(Type::Int64), Flags(0) {
	Value.Int64 = (int64_t) val;
}

Attrib::Attrib(float val) : Type(Type::Float), Flags(0) {
	Value.Float = val;
}

Attrib::Attrib(double val) : Type(Type::Double), Flags(0) {
	Value.Double = val;
}

Attrib::Attrib(imqs::Guid val, Allocator* alloc) : Type(Type::Null), Flags(0) {
	SetGuid(val, alloc);
}

Attrib::Attrib(time::Time val) : Type(Type::Null), Flags(0) {
	SetDate(val);
}

void Attrib::SetNull() {
	Reset();
}

void Attrib::SetBool(bool val) {
	Reset();
	Type       = Type::Bool;
	Value.Bool = val;
}

void Attrib::SetInt16(int16_t val) {
	Reset();
	Type        = Type::Int16;
	Value.Int16 = val;
}

void Attrib::SetInt32(int32_t val) {
	Reset();
	Type        = Type::Int32;
	Value.Int32 = val;
}

void Attrib::SetInt64(int64_t val) {
	Reset();
	Type        = Type::Int64;
	Value.Int64 = val;
}

void Attrib::SetFloat(float val) {
	Reset();
	Type        = Type::Float;
	Value.Float = val;
}

void Attrib::SetDouble(double val) {
	Reset();
	Type         = Type::Double;
	Value.Double = val;
}

void Attrib::SetText(const char* str, size_t len, Allocator* alloc) {
	SetTextWithLen(str, len == -1 ? strlen(str) : len, alloc);
}

void Attrib::SetText(const std::string& str, Allocator* alloc) {
	SetTextWithLen(str.c_str(), str.length(), alloc);
}

void Attrib::SetTextWithLen(const char* str, size_t len, Allocator* alloc) {
	PrepareText(len, alloc);
	if (str) {
		memcpy(Value.Text.Data, str, len);
		Value.Text.Data[len] = 0;
	}
}

void Attrib::SetGuid(const Guid& val, Allocator* alloc) {
	Reset();
	Type = Type::Guid;
	if (alloc) {
		Value.Guid = (Guid*) alloc->Alloc(16);
		Flags |= Flags::CustomHeap;
	} else {
		Value.Guid = MemPool::GetPoolForThread()->AllocGuid();
	}
	*Value.Guid = val;
}

void Attrib::SetDate(time::Time val) {
	Reset();
	if (val.IsNull())
		return;
	Type = Type::Date;
	val.Internal(Value.Date.Sec, Value.Date.Nsec);
}

void Attrib::SetBin(const void* val, size_t len, Allocator* alloc) {
	IMQS_ASSERT(len < MaxDataLen - 1);
	Reset();
	Type = Type::Bin;
	if (alloc) {
		if (len != 0)
			Value.Bin.Data = (uint8_t*) alloc->Alloc(len);
		else
			Value.Bin.Data = nullptr;
		Flags |= Flags::CustomHeap;
	} else {
		if (len != 0)
			Value.Bin.Data = (uint8_t*) MemPool::GetPoolForThread()->AllocBin(len);
		else
			Value.Bin.Data = nullptr;
	}
	Value.Bin.Size = (int) len;
	if (len != 0 && val != nullptr)
		memcpy(Value.Bin.Data, val, len);
}

void Attrib::SetPoint(GeomFlags flags, const float* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Double));
	flags = flags | GeomFlags::Float;
	SetPointsInternal(Type::GeomPoint, 1, flags, vx, srid, alloc);
}

void Attrib::SetPoint(GeomFlags flags, const double* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Float));
	flags = flags | GeomFlags::Double;
	SetPointsInternal(Type::GeomPoint, 1, flags, vx, srid, alloc);
}

void Attrib::SetMultiPoint(GeomFlags flags, int numPoints, const float* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Double));
	flags = flags | GeomFlags::Float;
	SetPointsInternal(Type::GeomMultiPoint, numPoints, flags, vx, srid, alloc);
}

void Attrib::SetMultiPoint(GeomFlags flags, int numPoints, const double* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Float));
	flags = flags | GeomFlags::Double;
	SetPointsInternal(Type::GeomMultiPoint, numPoints, flags, vx, srid, alloc);
}

void Attrib::SetPoly(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const float* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Double));
	flags = flags | GeomFlags::Float;
	SetPolyInternal(type, flags, numParts, parts, vx, srid, alloc);
}

void Attrib::SetPoly(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const double* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(!(flags & GeomFlags::Float));
	flags = flags | GeomFlags::Double;
	SetPolyInternal(type, flags, numParts, parts, vx, srid, alloc);
}

void Attrib::SetPolyline(GeomFlags flags, int numVertices, const double* vx, int srid, Allocator* alloc) {
	// Hint: partFlags would read easier if it was "0 | (numVertices & ~GeomPartFlag_Mask)", but that's just weird to OR with 0
	uint32_t partFlags = numVertices & ~GeomPartFlag_Mask;
	uint32_t vertices  = numVertices & GeomPartFlag_Mask;
	uint32_t parts[2]  = {partFlags, vertices};
	SetPoly(dba::Type::GeomPolyline, flags, 1, parts, vx, srid, alloc);
}

void Attrib::Set(const varargs::Arg& arg, Allocator* alloc) {
	switch (arg.Type) {
	case Type::Bool: SetBool(arg.Bool); break;
	case Type::Int16: SetInt16(arg.I16); break;
	case Type::Int32: SetInt32(arg.I32); break;
	case Type::Int64: SetInt64(arg.I64); break;
	case Type::Text: SetText(arg.Txt, alloc); break;
	case Type::Float: SetFloat(arg.Flt); break;
	case Type::Double: SetDouble(arg.Dbl); break;
	case Type::Guid: SetGuid(*arg.Guid, alloc); break;
	case Type::GeomAny:
		CopyFrom(*arg.Attrib, alloc);
		break; // GeomAny is a special value in this context
	default:
		IMQS_DIE_MSG("Unimplemented Attrib::Set(vararg)");
	}
}

Attrib Attrib::MakePoint(double x, double y, int srid, Allocator* alloc) {
	Attrib v;
	double xy[] = {x, y};
	v.SetPoint(GeomFlags::Double, xy, srid, alloc);
	return v;
}

Attrib Attrib::MakePolylineXY(size_t n, const double* xy, int srid, Allocator* alloc) {
	Attrib v;
	v.SetPolyline(GeomFlags::Double, (int) n, xy, srid, alloc);
	return v;
}

void Attrib::SetTempText(const char* str, size_t len) {
	Type            = dba::Type::Text;
	Flags           = Attrib::Flags::CustomHeap;
	Value.Text.Data = const_cast<char*>(str);
	Value.Text.Size = (int32_t) len;
}

void Attrib::SetTempBin(const void* bin, size_t len) {
	Type           = dba::Type::Bin;
	Flags          = Attrib::Flags::CustomHeap;
	Value.Bin.Data = (uint8_t*) bin;
	Value.Bin.Size = (int32_t) len;
}

void Attrib::SetTempGuid(const Guid* g) {
	Type       = dba::Type::Guid;
	Flags      = Attrib::Flags::CustomHeap;
	Value.Guid = const_cast<Guid*>(g);
}

bool Attrib::ToBool() const {
	switch (Type) {
	case Type::Null: return false;
	case Type::Bool: return Value.Bool;
	case Type::Int16: return Value.Int16 != 0;
	case Type::Int32: return Value.Int32 != 0;
	case Type::Int64: return Value.Int64 != 0;
	case Type::Float: return Value.Float != 0;
	case Type::Double: return Value.Double != 0;
	case Type::Text:
		return !(TextEqNoCase("false") || TextEq("0"));
	case Type::Guid: return true;
	case Type::Date: return true;
	case Type::Time: return true;
	case Type::Bin: return Value.Bin.Size != 0;
	default: return true;
	}
}

int16_t Attrib::ToInt16() const {
	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool ? 1 : 0;
	case Type::Int16: return Value.Int16;
	case Type::Int32: return (int16_t) Value.Int32;
	case Type::Int64: return (int16_t) Value.Int64;
	case Type::Float: return (int16_t) Value.Float;
	case Type::Double: return (int16_t) Value.Double;
	case Type::Text: return (int16_t) atoi(Value.Text.Data);
	case Type::Guid: return 0;
	case Type::Date: return (int16_t) UnixSeconds32();
	case Type::Time: return (int16_t) Value.Time;
	case Type::Bin: return 0;
	default: return 0;
	}
}

int32_t Attrib::ToInt32() const {
	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool ? 1 : 0;
	case Type::Int16: return Value.Int16;
	case Type::Int32: return Value.Int32;
	case Type::Int64: return (int32_t) Value.Int64;
	case Type::Float: return (int32_t) Value.Float;
	case Type::Double: return (int32_t) Value.Double;
	case Type::Text: return atoi(Value.Text.Data);
	case Type::Guid: return 0;
	case Type::Date: return UnixSeconds32();
	case Type::Time: return Value.Time;
	case Type::Bin: return 0;
	default: return 0;
	}
}

int64_t Attrib::ToInt64() const {
	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool ? 1 : 0;
	case Type::Int16: return Value.Int16;
	case Type::Int32: return Value.Int32;
	case Type::Int64: return Value.Int64;
	case Type::Float: return (int64_t) Value.Float;
	case Type::Double: return (int64_t) Value.Double;
	case Type::Text: return AtoI64(Value.Text.Data);
	case Type::Guid: return 0;
	case Type::Date: return UnixSeconds64();
	case Type::Time: return Value.Time;
	case Type::Bin: return 0;
	default: return 0;
	}
}

float Attrib::ToFloat() const {
	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool ? 1.0f : 0.0f;
	case Type::Int16: return (float) Value.Int16;
	case Type::Int32: return (float) Value.Int32;
	case Type::Int64: return (float) Value.Int64;
	case Type::Float: return Value.Float;
	case Type::Double: return (float) Value.Double;
	case Type::Text: return (float) strtod(Value.Text.Data, nullptr);
	case Type::Guid: return 0.0f;
	case Type::Date: return (float) UnixSecondsDbl();
	case Type::Time: return (float) Value.Time;
	case Type::Bin: return 0.0f;
	default: return 0.0f;
	}
}

double Attrib::ToDouble() const {
	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool ? 1 : 0;
	case Type::Int16: return (double) Value.Int16;
	case Type::Int32: return (double) Value.Int32;
	case Type::Int64: return (double) Value.Int64;
	case Type::Float: return Value.Float;
	case Type::Double: return Value.Double;
	case Type::Text: return strtod(Value.Text.Data, nullptr);
	case Type::Guid: return 0;
	case Type::Date: return (double) UnixSecondsDbl();
	case Type::Time: return (double) Value.Time;
	case Type::Bin: return 0;
	default: return 0;
	}
}

size_t Attrib::ToText(char* buf, size_t bufLen) const {
	char   staticFixed[128];
	size_t fixedLen = 0;

	// If bufLen is at least as big as our static buffer, then we
	// can write small items directly into buf.
	char* fixed = bufLen >= sizeof(staticFixed) ? buf : staticFixed;

	switch (Type) {
	case Type::Null:
		if (bufLen >= 1)
			buf[0] = 0;
		return 1;
	case Type::Bool:
		if (bufLen >= 2) {
			buf[0] = Value.Bool ? '1' : '0';
			buf[1] = 0;
		}
		return 2;
	case Type::Int16:
		fixedLen = ItoA(Value.Int16, fixed, 10) + 1;
		break;
	case Type::Int32:
		fixedLen = ItoA(Value.Int32, fixed, 10) + 1;
		break;
	case Type::Int64:
		fixedLen = I64toA(Value.Int64, fixed, 10) + 1;
		break;
	case Type::Float:
		sprintf(fixed, "%f", Value.Float);
		fixedLen = strlen(fixed) + 1;
		break;
	case Type::Double:
		sprintf(fixed, "%f", Value.Double);
		fixedLen = strlen(fixed) + 1;
		break;
	case Type::Text:
		if (bufLen >= Value.Text.Size + 1 && bufLen != 0) // "bufLen != 0" is here to satisfy clang-analyze [LLVM 3.8]
			memcpy(buf, Value.Text.Data, Value.Text.Size + 1);
		return Value.Text.Size + 1;
	case Type::Guid:
		if (bufLen >= 37)
			Value.Guid->ToString(buf);
		return 37;
	case Type::Date:
		if (bufLen >= 28)
			Date().Format8601(buf, 0);
		return 28;
	//case Type::Time:
	//	// TODO
	//	return 0;
	case Type::Bin:
		if (bufLen >= Value.Bin.Size * 2 + 1)
			strings::ToHex(Value.Bin.Data, Value.Bin.Size, buf);
		return Value.Bin.Size * 2 + 1;
	default:
		if (bufLen >= 1)
			buf[0] = 0;
		return 1;
	}

	// These cases all use fixed and fixedLen. "bufLen != 0" is here to satisfy clang-analyze [LLVM 3.8]
	if (fixedLen <= bufLen && bufLen != 0 && fixed != buf)
		memcpy(buf, fixed, fixedLen);
	return fixedLen;
}

Guid Attrib::ToGuid() const {
	switch (Type) {
	case Type::Guid:
		return *Value.Guid;
	case Type::Bin:
		if (Value.Bin.Size == 16)
			return Guid::FromBytes(Value.Bin.Data);
		break;
	case Type::Text:
		return Guid::FromString(Value.Text.Data);
	default:
		break;
	}
	return Guid();
}

time::Time Attrib::ToDate() const {
	time::Time t;
	switch (Type) {
	case Type::Date:
		return Date();
	case Type::Text:
		t.Parse8601(Value.Text.Data, Value.Text.Size);
		break;
	default:
		break;
	}
	return t;
}

void Attrib::ToJson(rapidjson::Value& out, rapidjson::Document::AllocatorType& allocator) const {
	auto pushPart = [&](rapidjson::Value& vertexOut, uint8_t* vertices, int* indices, int count, bool closed) {
		bool   isFloat       = !!(Value.Geom.Flags & GeomFlags::Float);
		size_t dimensions    = GeomDimensions();
		size_t dimSize       = (isFloat ? 4 : 8);
		size_t bpv           = GeomBytesPerVertex(Value.Geom.Flags);
		size_t numIterations = count + (closed ? 1 : 0);
		for (size_t idx = 0; idx < numIterations; idx++) {
			size_t index = idx;
			if (closed && index == count) // repeat first vertex
				index = 0;

			uint8_t* vertex = vertices + (bpv * indices[index]);
			for (size_t xyz = 0; xyz < dimensions; xyz++, vertex += dimSize) {
				rapidjson::Value a(rapidjson::kNumberType);
				if (isFloat)
					a.SetFloat(*((float*) vertex));
				else
					a.SetDouble(*((double*) vertex));
				vertexOut.PushBack(a, allocator);
			}
		}
	};

	switch (Type) {
	case Type::Bool: out.SetBool(Value.Bool); break;
	case Type::Int16: out.SetInt(Value.Int16); break;
	case Type::Int32: out.SetInt(Value.Int32); break;
	case Type::Int64: out.SetInt64(Value.Int64); break;
	case Type::Float: out.SetDouble(Value.Float); break;
	case Type::Double: out.SetDouble(Value.Double); break;
	case Type::Text: out.SetString(Value.Text.Data, allocator); break;
	case Type::Null: out.SetNull(); break;
	case Type::Guid:
	case Type::Date:
	case Type::Bin:
		out.SetString(ToString().c_str(), allocator);
		break;
	//case Type::Time:
	//	// TODO
	case Type::GeomPoint: {
		out.SetObject();
		char gtype[4] = "ptN";
		gtype[2]      = '0' + (char) GeomDimensions();
		out.AddMember("t", rapidjson::Value(gtype, allocator), allocator);
		rapidjson::Value vertex(rapidjson::kArrayType);
		int              index[1] = {0};
		pushPart(vertex, (uint8_t*) GeomVertices(), index, 1, false);
		out.AddMember("v", vertex, allocator);
		break;
	}
	case Type::GeomMultiPoint:
	case Type::GeomAny:
		// not supported
		out.SetObject();
		break;
	case Type::GeomPolyline:
	case Type::GeomPolygon: {
		out.SetObject();
		auto dim      = GeomDimensions();
		char gtype[4] = "pxN";
		gtype[1]      = (Type == Type::GeomPolyline) ? 'l' : 'g';
		gtype[2]      = '0' + (char) dim;
		out.AddMember("t", rapidjson::Value(gtype, allocator), allocator);
		auto             vertices = (uint8_t*) GeomVertices();
		bool             isFloat  = !!(Value.Geom.Flags & GeomFlags::Float);
		size_t           dimSize  = isFloat ? 4 : 8;
		rapidjson::Value parts(rapidjson::kArrayType);
		for (size_t partIdx = 0; partIdx < Value.Geom.Head->NumParts; partIdx++) {
			int              start, count;
			auto             closed = !!(GeomPart(partIdx, start, count) & GeomPartFlag_Closed);
			rapidjson::Value vertexOut(rapidjson::kArrayType);
			int*             indices = (int*) malloc(count * sizeof(int));
			for (auto vIdx = 0; vIdx < count; vIdx++)
				indices[vIdx] = start + vIdx;
			pushPart(vertexOut, vertices, indices, count, closed);
			free(indices);
			parts.PushBack(vertexOut, allocator);
		}
		out.AddMember("v", parts, allocator);
		break;
	}
	default:
		break;
	}
}

std::string Attrib::ToSQLString() const {
	switch (Type) {
	case Type::Null:
		return "NULL";
		break;
	case Type::GeomPoint:
	case Type::GeomPolygon:
	case Type::GeomPolyline:
	case Type::GeomMultiPoint:
	case Type::GeomAny:
		return tsf::fmt("dba_ST_GeomFromText('%v', %d),", ToWKTString(), geom::SRID_WGS84LatLonDegrees);
		break;
	default:
		return tsf::fmt("'%v'", ToString());
	}
}

ohash::hashkey_t Attrib::gethashcode(const dba::Attrib& a) {
	return a.GetHashCode();
}

ohash::hashkey_t Attrib::GetHashCode() const {
	// See http://aras-p.info/blog/2016/08/09/More-Hash-Function-Tests/
	// FNV is fastest for small sizes. XXH32 better for larger sizes.

	switch (Type) {
	case Type::Null: return 0;
	case Type::Bool: return Value.Bool;
	case Type::Int16: return Value.Int16;
	case Type::Int32: return Value.Int32;
	case Type::Int64: return (ohash::hashkey_t)(Value.Int64 ^ ((uint64_t) Value.Int64 >> 32));
	case Type::Float: {
		uint32_t i = *((uint32_t*) &Value.Float);
		return (ohash::hashkey_t) i;
	}
	case Type::Double: {
		uint64_t i = *((uint64_t*) &Value.Double);
		return (ohash::hashkey_t) i ^ (i >> 32);
	}
	case Type::Guid: return Value.Guid->Hash32();
	case Type::Text:
	case Type::Bin:
		static_assert(offsetof(Attrib, Value.Bin.Data) == offsetof(Attrib, Value.Text.Data), "Bin and Text don't have same storage");
		static_assert(offsetof(Attrib, Value.Bin.Size) == offsetof(Attrib, Value.Text.Size), "Bin and Text don't have same storage");
		static_assert(sizeof(Value.Bin) == sizeof(Value.Text), "Bin and Text don't have same storage");
		if (Value.Bin.Size <= 8)
			return fnv_32a_buf(Value.Bin.Data, Value.Bin.Size);
		else
			return XXH32(Value.Bin.Data, Value.Bin.Size, 0);
	default:
		IMQS_ASSERT(false);
		return 0;
	}
}

static StaticError ErrInvalidGeom("Invalid geometry inside JSON");
static StaticError ErrJsonPolygonOpen("Invalid geometry inside JSON: Polygon is not closed");

/*
Geometry format:
{
	"t": "pt3" | "pg2" | "pl4",			pt = point, pg = polygon, pl = polyline. numeric digit number of dimensions, which is 2, 3, or 4.

	-- vertices of a pt3:
	"v": [
		33.1, 22.1, 5
	]

	-- vertices of a pg2:
	"v": [
		[1,2,1,3,2,4,1,2], [4,5,6,7,1,2,5,6,4,5]  -- 2 parts. First part has 3 vertices. Second part has 4 vertices. First vertex is repeated to close object.
	]
}
*/
Error Attrib::FromJson(const rapidjson::Value& in, dba::Type fieldType, Allocator* alloc) {
	Error err;
	Reset();
	if (in.IsNull())
		return Error();
	switch (fieldType) {
	case Type::GeomAny:
	case Type::GeomPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon: {
		if (!in.IsObject())
			return ErrInvalidGeom;
		auto objT = in.FindMember("t");
		auto objV = in.FindMember("v");
		if (objT == in.MemberEnd() || objV == in.MemberEnd() || !objT->value.IsString() || !objV->value.IsArray())
			return ErrInvalidGeom;
		if (objT->value.GetStringLength() != 3)
			return Error("Invalid JSON geometry: type must be 3 characters long");
		const char* t = objT->value.GetString();
		if (t[0] != 'p')
			return Error("Invalid JSON geometry: first character of type must be 'p'");

		dba::Type jtype;
		switch (t[1]) {
		case 't': jtype = dba::Type::GeomPoint; break;
		case 'l': jtype = dba::Type::GeomPolyline; break;
		case 'g': jtype = dba::Type::GeomPolygon; break;
		default:
			return Error("Invalid JSON geometry: must be one of pt, pl, pg");
		}
		if (fieldType != Type::GeomAny && fieldType != jtype)
			return Error::Fmt("JSON geometry is type %v, but field is type %v", dba::FieldTypeToString(jtype), dba::FieldTypeToString(fieldType));

		GeomFlags flags  = GeomFlags::Double;
		int       vxSize = 2;
		switch (t[2]) {
		case '2':
			break;
		case '3':
			vxSize = 3;
			flags |= GeomFlags::HasZ;
			break;
		case '4':
			vxSize = 4;
			flags |= GeomFlags::HasZ | GeomFlags::HasM;
			break;
		default:
			return Error("Invalid JSON geometry: dimensions must be 2,3,4");
		}

		// Our general approach here is to allocate the memory first, and then copy the vertices in from the JSON
		const auto& jv = objV->value.GetArray();
		if (jtype == dba::Type::GeomPoint) {
			if (jv.Size() != vxSize)
				return ErrInvalidGeom;
			for (int i = 0; i < vxSize; i++) {
				if (!jv[i].IsNumber())
					return Error("Invalid JSON geometry: coordinates must be numbers");
			}
			SetPoint(flags, (double*) nullptr, geom::SRID_WGS84LatLonDegrees, alloc);
			double* vx = (double*) GeomVertices();
			for (int i = 0; i < vxSize; i++)
				vx[i] = jv[i].GetDouble();
		} else {
			// polyline or polygon. first validate correctness of JSON
			int numParts = jv.Size();
			for (int i = 0; i < numParts; i++) {
				if (!jv[i].IsArray()) {
					return Error("Invalid JSON geometry: part is not an array");
				}
				const auto& jvx = jv[i].GetArray();
				if (jtype == dba::Type::GeomPolyline && (int) jvx.Size() < vxSize * 2) {
					return Error("Invalid JSON geometry: too few vertices");
				}
				if (jtype == dba::Type::GeomPolygon && (int) jvx.Size() < vxSize * 4) { // actually 3, but our first vertex is repeated to make a closed loop. polygons must be closed.
					return Error("Invalid JSON geometry: too few vertices for polygon");
				}
				if (jvx.Size() % vxSize != 0) {
					return Error("Invalid JSON geometry: coordinate array size is not a multiple of vertex size");
				}
				for (unsigned j = 0; j < jvx.Size(); j++) {
					if (!jvx[i].IsNumber())
						return ErrInvalidGeom;
				}
			}
			// measure parts, and allocate storage
			uint32_t  static_parts[16];
			uint32_t* parts   = (numParts + 1 < arraysize(static_parts)) ? static_parts : (uint32_t*) imqs_malloc_or_die(sizeof(uint32_t) * (numParts + 1));
			int       vxcount = 0;
			for (int i = 0; i < numParts; i++) {
				const auto& jvx    = jv[i].GetArray();
				int         nverts = jvx.Size() / vxSize;
				bool        closed = true;
				for (int j = 0; j < vxSize; j++) {
					if (jvx[j].GetDouble() != jvx[(nverts - 1) * vxSize + j].GetDouble())
						closed = false;
				}
				parts[i] = vxcount;
				if (closed)
					parts[i] |= GeomPartFlag_Closed;

				if (!closed && jtype == Type::GeomPolygon)
					return ErrJsonPolygonOpen;

				if (closed)
					vxcount += nverts - 1;
				else
					vxcount += nverts;
			}
			parts[numParts] = vxcount;
			// We want to prepare all of the vertices before calling SetPoly(), because SetPoly() takes care of
			// figuring out the rings of multipolygon objects.
			double  static_vx[64];
			size_t  dblCount = vxcount * vxSize;
			double* vx       = (dblCount < arraysize(static_vx)) ? static_vx : (double*) imqs_malloc_or_die(sizeof(double) * dblCount);
			double* vxOut    = vx;
			for (int i = 0; i < numParts; i++) {
				const auto& jPart     = jv[i].GetArray();
				int         partVerts = (parts[i + 1] & GeomPartFlag_Mask) - (parts[i] & GeomPartFlag_Mask);
				int         vxIn      = 0;
				for (int j = 0; j < partVerts; j++) {
					for (int q = 0; q < vxSize; q++) {
						*vxOut++ = jPart[vxIn++].GetDouble();
					}
				}
			}
			SetPoly(jtype, flags, numParts, parts, vx, geom::SRID_WGS84LatLonDegrees, alloc);
			if (parts != static_parts)
				free(parts);
			if (vx != static_vx)
				free(vx);
		}
		break;
	}
	case Type::Int16:
		if (!in.IsNumber())
			return Error("Expected JSON number for INT16 value");
		SetInt16((int16_t) in.GetInt());
		break;
	case Type::Int32:
		if (!in.IsNumber())
			return Error("Expected JSON number for INT32 value");
		SetInt32(in.GetInt());
		break;
	case Type::Int64:
		if (!in.IsNumber())
			return Error("Expected JSON number for INT64 value");
		SetInt64(in.GetInt64());
		break;
	case Type::Float:
		if (!in.IsNumber())
			return Error("Expected JSON number for Float value");
		SetFloat(in.GetFloat());
		break;
	case Type::Double:
		if (!in.IsNumber())
			return Error("Expected JSON number for Double value");
		SetDouble(in.GetDouble());
		break;
	case Type::Guid:
		if (!in.IsString())
			return Error("Expected JSON string for UUID value");
		SetGuid(Guid::FromString(in.GetString()), alloc);
		break;
	case Type::Text:
		if (!in.IsString())
			return Error("Expected JSON string");
		SetText(in.GetString(), alloc);
		break;
	case Type::Bool:
		if (!in.IsBool())
			return Error("Expected JSON boolean");
		SetBool(in.GetBool());
		break;
	case Type::Date:
	case Type::Time: {
		if (!in.IsString())
			return Error("Expected JSON string for date/time value");
		time::Time  t;
		std::string s = in.GetString();
		if (s == "")
			break;
		err = t.Parse8601(s.c_str(), s.length());
		if (!err.OK())
			return Error::Fmt("Expected ISO 8601 date from JSON. %v", err.Message());
		SetDate(t);
		break;
	}
	case Type::Null:
		// Unspecified field type, so just guess
		if (in.IsString())
			SetText(in.GetString(), alloc);
		else if (in.IsInt())
			SetInt64(in.GetInt64());
		else if (in.IsNumber())
			SetDouble(in.GetDouble());
		else if (in.IsBool())
			SetBool(in.IsTrue());
		else if (in.IsNull())
			SetNull();
		else
			return Error::Fmt("Don't know how to decode JSON value");
		break;
	default:
		return Error::Fmt("JSON value not convertible to dba::Attrib (field type %v)", dba::FieldTypeToString(fieldType));
	}
	return err;
}

// ORMJS-TODO: Give this function a mode where it writes to a preallocated buffer
std::string Attrib::ToWKTString() const {
	auto pushPart = [&](std::string& out, uint8_t* vertices, int* indices, int count, bool closed) {
		out                  = "(";
		bool   isFloat       = !!(Value.Geom.Flags & GeomFlags::Float);
		size_t dimensions    = GeomDimensions();
		size_t dimSize       = (isFloat ? 4 : 8);
		size_t bpv           = GeomBytesPerVertex(Value.Geom.Flags);
		size_t numIterations = count + (closed ? 1 : 0);
		for (size_t idx = 0; idx < numIterations; idx++) {
			size_t index = idx;
			if (closed && index == count) // repeat first vertex
				index = 0;

			uint8_t* vertex = vertices + (bpv * indices[index]);
			for (size_t xyz = 0; xyz < dimensions; xyz++, vertex += dimSize) {
				if (isFloat)
					out += std::to_string(*((float*) vertex)) + " ";
				else
					out += std::to_string(*((double*) vertex)) + " ";
			}
			if (idx < numIterations - 1)
				out += ", ";
		}
		out += ")";
	};

	std::string wkt;

	bool   isFloat    = !!(Value.Geom.Flags & GeomFlags::Float);
	auto   dimensions = GeomDimensions();
	size_t dimSize    = (isFloat ? 4 : 8);
	size_t bpv        = GeomBytesPerVertex(Value.Geom.Flags);
	auto   vertices   = (uint8_t*) GeomVertices();
	switch (Type) {
	case Type::GeomPoint: {
		wkt                  = "POINT";
		int         index[1] = {0};
		std::string part;
		pushPart(part, (uint8_t*) GeomVertices(), index, 1, false);
		wkt += part;
		break;
	}
	case Type::GeomPolygon:
	case Type::GeomPolyline:
		wkt = (Type == Type::GeomPolygon) ? "POLYGON (" : "LINESTRING (";
		for (size_t partIdx = 0; partIdx < Value.Geom.Head->NumParts; partIdx++) {
			int              start, count;
			auto             closed = !!(GeomPart(partIdx, start, count) & GeomPartFlag_Closed);
			rapidjson::Value vertexOut(rapidjson::kArrayType);
			int*             indices = (int*) malloc(count * sizeof(int));
			for (auto vIdx = 0; vIdx < count; vIdx++)
				indices[vIdx] = start + vIdx;
			std::string partStr;
			pushPart(partStr, vertices, indices, count, closed);
			free(indices);
			wkt += partStr;
			if (partIdx < Value.Geom.Head->NumParts)
				wkt += ", ";
		}
		wkt += ")";
		break;
	default:
		break;
	}

	return wkt;
}

std::string Attrib::ToString() const {
	size_t size = ToText(nullptr, 0);
	if (size == 1)
		return "";
	std::string s;
	s.resize(size - 1);
	// This is not strictly legal, but I cannot imagine a string representation where this wouldn't work.
	ToText(&s.at(0), size);
	return s;
}

const char* Attrib::RawString() const {
	IMQS_ASSERT(Type == dba::Type::Text);
	return Value.Text.Data;
}

size_t Attrib::TextLen() const {
	IMQS_ASSERT(Type == dba::Type::Text);
	return Value.Text.Size;
}

Attrib Attrib::ConvertTo(dba::Type dstType) const {
	Attrib tmp;
	CopyTo(dstType, tmp);
	return tmp;
}

Error Attrib::AssignTo(varargs::OutArg& arg) const {
	// GeomAny in OutArg means "Attrib"
	if (arg.Type == Type::GeomAny) {
		*arg.Attrib = *this;
		return Error();
	}

	char buf[128];

	switch (Type) {
	case Type::Null:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = false; break;
		case Type::Int16: *arg.I16 = 0; break;
		case Type::Int32: *arg.I32 = 0; break;
		case Type::Int64: *arg.I64 = 0; break;
		case Type::Float: *arg.Flt = 0; break;
		case Type::Double: *arg.Dbl = 0; break;
		case Type::Text: *arg.Txt = ""; break;
		case Type::Guid: *arg.Guid = Guid::Null(); break;
		case Type::Date: *arg.Date = time::Time(); break;
		default:
			return Error("Unable to convert null into target type");
		}
		break;
	case Type::Bool:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = Value.Bool; break;
		case Type::Int16: *arg.I16 = Value.Bool ? 1 : 0; break;
		case Type::Int32: *arg.I32 = Value.Bool ? 1 : 0; break;
		case Type::Int64: *arg.I64 = Value.Bool ? 1 : 0; break;
		case Type::Float: *arg.Flt = Value.Bool ? 1.f : 0.f; break;
		case Type::Double: *arg.Dbl = Value.Bool ? 1. : 0.; break;
		case Type::Text: *arg.Txt = Value.Bool ? "1" : "0"; break;
		default:
			return Error("Unable to convert bool into target type");
		}
		break;
	case Type::Int32:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = Value.Int32 != 0; break;
		case Type::Int16: *arg.I16 = Value.Int32; break;
		case Type::Int32: *arg.I32 = Value.Int32; break;
		case Type::Int64: *arg.I64 = (int32_t) Value.Int64; break;
		case Type::Float: *arg.Flt = (float) Value.Int32; break;
		case Type::Double: *arg.Dbl = Value.Int32; break;
		case Type::Text:
			ItoA(Value.Int32, buf, 10);
			*arg.Txt = buf;
			break;
		default:
			return Error("Unable to convert int32 into target type");
		}
		break;
	case Type::Int64:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = Value.Int64 != 0; break;
		case Type::Int16: *arg.I16 = (int16_t) Value.Int64; break;
		case Type::Int32: *arg.I32 = (int32_t) Value.Int64; break;
		case Type::Int64: *arg.I64 = Value.Int64; break;
		case Type::Float: *arg.Flt = (float) Value.Int64; break;
		case Type::Double: *arg.Dbl = (double) Value.Int64; break;
		case Type::Text:
			I64toA(Value.Int64, buf, 10);
			*arg.Txt = buf;
			break;
		default:
			return Error("Unable to convert int64 into target type");
		}
		break;
	case Type::Double:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = Value.Double != 0; break;
		case Type::Int16: *arg.I16 = (int16_t) Value.Double; break;
		case Type::Int32: *arg.I32 = (int32_t) Value.Double; break;
		case Type::Int64: *arg.I64 = (int64_t) Value.Double; break;
		case Type::Float: *arg.Flt = (float) Value.Double; break;
		case Type::Double: *arg.Dbl = Value.Double; break;
		case Type::Text:
			sprintf(buf, "%f", Value.Double);
			*arg.Txt = buf;
			break;
		default:
			return Error("Unable to convert double into target type");
		}
		break;
	case Type::Text:
		switch (arg.Type) {
		case Type::Bool: *arg.Bool = atoi(Value.Text.Data) != 0; break;
		case Type::Int16: *arg.I16 = (int16_t) atoi(Value.Text.Data); break;
		case Type::Int32: *arg.I32 = atoi(Value.Text.Data); break;
		case Type::Int64: *arg.I64 = AtoI64(Value.Text.Data); break;
		case Type::Float: *arg.Flt = (float) std::strtod(Value.Text.Data, nullptr); break;
		case Type::Double: *arg.Dbl = std::strtod(Value.Text.Data, nullptr); break;
		case Type::Text: *arg.Txt = Value.Text.Data; break;
		case Type::Guid: {
			Guid g;
			if (g.ParseString(Value.Text.Data))
				return Error();
			return Error("Unable to convert text into guid");
		}
		default:
			return Error("Unable to convert text into target type");
		}
		break;
	case Type::Bin:
		switch (arg.Type) {
		case Type::Guid:
			if (Value.Bin.Size == 16) {
				memcpy(arg.Guid, Value.Bin.Data, 16);
				return Error();
			}
		default:
			break;
		}
		return Error("Unable to convert blob into target type");
	case Type::Guid:
		switch (arg.Type) {
		case Type::Guid: *arg.Guid = *Value.Guid; break;
		default:
			return Error("Unable to convert guid into target type");
		}
		break;
	case Type::Date:
		switch (arg.Type) {
		case Type::Date: *arg.Date = Date(); break;
		default:
			return Error("Unable to convert date into target type");
		}
		break;
	default:
		return Error("Type not supported for conversion");
	}
	return Error();
}

int Attrib::Compare(const Attrib& b) const {
	if (IsNull() && !b.IsNull())
		return -1;
	if (!IsNull() && b.IsNull())
		return 1;
	if (IsNull())
		return 0; // both null

	if (Type != b.Type) {
		uint8_t    buf[128];
		StackAlloc stalloc(buf);
		Attrib     copy;
		b.CopyTo(Type, copy, &stalloc);
		return Compare(copy);
	}

	switch (Type) {
	case Type::Bool: return (Value.Bool ? 1 : -1) - (b.Value.Bool ? 1 : -1);
	case Type::Int16: return math::Compare(Value.Int16, b.Value.Int16);
	case Type::Int32: return math::Compare(Value.Int32, b.Value.Int32);
	case Type::Int64: return math::Compare(Value.Int64, b.Value.Int64);
	case Type::Float: return math::Compare(Value.Float, b.Value.Float);
	case Type::Double: return math::Compare(Value.Double, b.Value.Double);
	case Type::Text: return math::SignOrZero(strcmp(Value.Text.Data, b.Value.Text.Data));
	case Type::Guid: return math::SignOrZero(memcmp(Value.Guid->Bytes, b.Value.Guid->Bytes, 16));
	case Type::Date:
		return math::Compare(Date(), b.Date());
	//case Type::Time: return 0;
	case Type::Bin: {
		auto minSize = std::min(Value.Bin.Size, b.Value.Bin.Size);
		int  mc      = memcmp(Value.Bin.Data, b.Value.Bin.Data, minSize);
		if (mc < 0)
			return -1;
		else if (mc > 0)
			return 1;
		else if (Value.Bin.Size == b.Value.Bin.Size)
			return 0;
		return Value.Bin.Size < b.Value.Bin.Size ? -1 : 1;
	}
	case Type::GeomPoint: return 0;
	case Type::GeomMultiPoint: return 0;
	case Type::GeomPolyline: return 0;
	case Type::GeomPolygon: return 0;
	case Type::GeomAny: return 0;
	default:
		IMQS_DIE_MSG("Unimplemented type in Attrib.Compare");
		return 0;
	}
}

int Attrib::CompareAsNum(const Attrib& b) const {
	if (Type == b.Type) {
		switch (Type) {
		case Type::Int16: return math::Compare(Value.Int16, b.Value.Int16);
		case Type::Int32: return math::Compare(Value.Int32, b.Value.Int32);
		case Type::Int64: return math::Compare(Value.Int64, b.Value.Int64);
		case Type::Float: return math::Compare(Value.Float, b.Value.Float);
		case Type::Double: return math::Compare(Value.Double, b.Value.Double);
		default:
			break;
		}
	}
	return math::Compare(ToDouble(), b.ToDouble());
}

void Attrib::CopyTo(dba::Type dstType, Attrib& dst, Allocator* alloc) const {
	if (dstType == dba::Type::Null)
		dstType = Type;

	if (dstType == Type) {
		dst.CopyFrom(*this, alloc);
	} else {
		switch (dstType) {
		case Type::Bool: dst.SetBool(ToBool()); break;
		case Type::Int16: dst.SetInt16(ToInt16()); break;
		case Type::Int32: dst.SetInt32(ToInt32()); break;
		case Type::Int64: dst.SetInt64(ToInt64()); break;
		case Type::Float: dst.SetFloat(ToFloat()); break;
		case Type::Double: dst.SetDouble(ToDouble()); break;
		case Type::Text: {
			size_t len = ToText(nullptr, 0);
			dst.PrepareText(len - 1, alloc);
			ToText(dst.Value.Text.Data, len);
			break;
		}
		case Type::Guid: dst.SetGuid(ToGuid()); break;
		case Type::Date: dst.SetDate(ToDate()); break;
		case Type::GeomPoint:
		case Type::GeomMultiPoint:
		case Type::GeomPolyline:
		case Type::GeomPolygon:
		case Type::GeomAny:
			if (Type == Type::Bin)
				WKB::Decode(Value.Bin.Data, Value.Bin.Size, dst, alloc);
			else if (Type == Type::Text)
				WKT::Parse(Value.Text.Data, Value.Text.Size, dst, alloc, nullptr, nullptr, false);
			break;
		case Type::Time:
		case Type::Bin:
		default:
			// TODO
			dst.SetNull();
			break;
		}
	}
}

void* Attrib::DynData() const {
	switch (Type) {
	case Type::Null:
	case Type::Bool:
	case Type::Int16:
	case Type::Int32:
	case Type::Int64:
	case Type::Float:
	case Type::Double:
	case Type::Date:
	case Type::Time:
		return nullptr;
	case Type::Text:
		return Value.Text.Data;
	case Type::Guid:
		return Value.Guid;
	case Type::Bin:
		return Value.Bin.Data;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
		return Value.Geom.Head;
	default:
		IMQS_DIE();
	}
	return nullptr;
}

int32_t Attrib::UnixSeconds32() const {
	if (Type != Type::Date)
		return 0;
	auto d = time::Time::FromInternal(Value.Date.Sec, Value.Date.Nsec);
	return (int32_t) d.Unix();
}

int64_t Attrib::UnixSeconds64() const {
	if (Type != Type::Date)
		return 0;
	auto d = time::Time::FromInternal(Value.Date.Sec, Value.Date.Nsec);
	return d.Unix();
}

double Attrib::UnixSecondsDbl() const {
	if (Type != Type::Date)
		return 0;
	auto d = time::Time::FromInternal(Value.Date.Sec, Value.Date.Nsec);
	return d.UnixNano() / 1e9;
}

time::Time Attrib::Date() const {
	if (Type != Type::Date)
		return time::Time();
	return time::Time::FromInternal(Value.Date.Sec, Value.Date.Nsec);
}

uint32_t Attrib::GeomNumParts() const {
	return Value.Geom.Head->NumParts;
}

GeomPartFlags Attrib::GeomPart(size_t part, int& start, int& count) const {
	IMQS_ASSERT(IsTypeGeomPoly(Type));
	auto parts = GeomParts();
	start      = parts[part] & GeomPartFlag_Mask;
	count      = (parts[part + 1] & GeomPartFlag_Mask) - (parts[part] & GeomPartFlag_Mask);
	return (GeomPartFlags)(parts[part] & ~GeomPartFlag_Mask);
}

bool Attrib::GeomPartIsClosed(size_t part) const {
	int start, count;
	return !!(GeomPart(part, start, count) & GeomPartFlags::GeomPartFlag_Closed);
}

void* Attrib::GeomVertices() {
	return const_cast<void*>(((const Attrib*) this)->GeomVertices());
}

const void* Attrib::GeomVertices() const {
	if (Type == Type::GeomPoint || Type == Type::GeomMultiPoint) {
		return Value.Geom.Head + 1;
	} else if (Type == Type::GeomPolyline || Type == Type::GeomPolygon) {
		uint8_t* p = (uint8_t*) (Value.Geom.Head + 1);
		p += PolyPartArraySize(Value.Geom.Head->NumParts) * sizeof(uint32_t);
		return p;
	} else {
		IMQS_DIE();
		return nullptr;
	}
}

double* Attrib::GeomVerticesDbl() {
	IMQS_ASSERT(!!(Value.Geom.Flags & GeomFlags::Double));
	return (double*) GeomVertices();
}

const double* Attrib::GeomVerticesDbl() const {
	IMQS_ASSERT(!!(Value.Geom.Flags & GeomFlags::Double));
	return (const double*) GeomVertices();
}

size_t Attrib::GeomTotalVertexCount() const {
	if (Type == Type::GeomPoint) {
		return 1;
	} else if (Type == Type::GeomMultiPoint) {
		return Value.Geom.Head->NumParts;
	} else if (Type == Type::GeomPolygon || Type == Type::GeomPolyline) {
		return GeomParts()[Value.Geom.Head->NumParts] & GeomPartFlag_Mask;
	} else {
		IMQS_DIE();
		return 0;
	}
}

bool Attrib::GeomHasZ() const {
	return !!(Value.Geom.Flags & GeomFlags::HasZ);
}

bool Attrib::GeomHasM() const {
	return !!(Value.Geom.Flags & GeomFlags::HasM);
}

size_t Attrib::GeomDimensions() const {
	return dba::GeomDimensions(Value.Geom.Flags);
}

bool Attrib::GeomIsDoubles() const {
	return !!(Value.Geom.Flags & GeomFlags::Double);
}

template <typename OrgV, typename NewV>
void ConvertVertices(size_t n, const OrgV* oldV, NewV* newV, bool oldZ, bool newZ, bool oldM, bool newM) {
	size_t inC  = 0;
	size_t outC = 0;
	for (size_t i = 0; i < n; i++) {
		OrgV x, y, z = 0, m = 0;
		x = oldV[inC++];
		y = oldV[inC++];
		if (oldZ)
			z = oldV[inC++];
		if (oldM)
			m = oldV[inC++];

		newV[outC++] = (NewV) x;
		newV[outC++] = (NewV) y;
		if (newZ)
			newV[outC++] = (NewV) z;
		if (newM)
			newV[outC++] = (NewV) m;
	}
}

void Attrib::GeomAlterStorage(GeomFlags newFlags, Allocator* alloc) {
	bool oldHasZ   = GeomHasZ();
	bool oldHasM   = GeomHasM();
	bool oldDouble = GeomIsDoubles();
	bool newHasZ   = !!(newFlags & GeomFlags::HasZ);
	bool newHasM   = !!(newFlags & GeomFlags::HasM);
	bool newDouble = !!(newFlags & GeomFlags::Double);
	if (oldHasM == newHasM && oldHasZ == newHasZ && oldDouble == newDouble)
		return;

	// prepare new flags by preserving our old flags, and overriding the relevant pieces
	unsigned nf = (unsigned) Value.Geom.Flags;
	nf          = (nf & ~((unsigned) GeomFlags::HasZ)) | (newHasZ ? (unsigned) GeomFlags::HasZ : 0);
	nf          = (nf & ~((unsigned) GeomFlags::HasM)) | (newHasM ? (unsigned) GeomFlags::HasM : 0);
	nf          = (nf & ~((unsigned) GeomFlags::Double | (unsigned) GeomFlags::Float)) | (newDouble ? (unsigned) GeomFlags::Double : (unsigned) GeomFlags::Float);
	newFlags    = (GeomFlags) nf;

	auto        oldGeomFlags = Value.Geom.Flags;
	void*       oldVx        = GeomVertices();
	int         nparts       = GeomNumParts();
	size_t      numV         = GeomTotalVertexCount();
	GeomHeader* oldHead      = Value.Geom.Head;
	Value.Geom.Head          = nullptr;

	auto oldAttribFlags = Flags;
	Flags |= Flags::CustomHeap; // Ensure that the Reset() inside SetPointsInternal, SetPolyInternal, don't free our old memory. We'll free it after everything.

	if (Type == dba::Type::GeomPoint || Type == dba::Type::GeomMultiPoint)
		SetPointsInternal(Type, oldHead->NumParts, newFlags, (double*) nullptr, oldHead->SRID, alloc);
	else
		SetPolyInternal(Type, newFlags, oldHead->NumParts, (uint32_t*) (oldHead + 1), nullptr, oldHead->SRID, alloc);

	void* newVx = GeomVertices();
	if (oldDouble && newDouble)
		ConvertVertices(numV, (double*) oldVx, (double*) GeomVertices(), oldHasZ, newHasZ, oldHasM, newHasM);
	else if (oldDouble && !newDouble)
		ConvertVertices(numV, (double*) oldVx, (float*) GeomVertices(), oldHasZ, newHasZ, oldHasM, newHasM);
	else if (!oldDouble && newDouble)
		ConvertVertices(numV, (float*) oldVx, (double*) GeomVertices(), oldHasZ, newHasZ, oldHasM, newHasM);
	else if (!oldDouble && !newDouble)
		ConvertVertices(numV, (float*) oldVx, (float*) GeomVertices(), oldHasZ, newHasZ, oldHasM, newHasM);

	// If old mem was on the heap, then free it by making a dummy attrib that goes out of scope
	if (!(oldAttribFlags & Flags::CustomHeap)) {
		Attrib dummy;
		dummy.Type             = Type;
		dummy.Flags            = oldAttribFlags;
		dummy.Value.Geom.Flags = oldGeomFlags;
		dummy.Value.Geom.Head  = oldHead; // This is the key pointer that's going to be freed. Everything else is just book keeping.
	}
}

bool Attrib::GeomConvertSRID(int newSRID) {
	if (Value.Geom.Head->SRID == newSRID)
		return true;
	IMQS_ASSERT(Value.Geom.Head->SRID != 0);
	IMQS_ASSERT(GeomIsDoubles());
	int ndim = 2;
	if (GeomHasZ())
		ndim++;
	if (GeomHasM())
		ndim++;
	double* x = GeomVerticesDbl();
	double* y = GeomVerticesDbl() + 1;
	double* z = GeomHasZ() ? GeomVerticesDbl() + 2 : nullptr;
	if (projwrap::Convert(Value.Geom.Head->SRID, newSRID, GeomTotalVertexCount(), ndim, x, y, z)) {
		Value.Geom.Head->SRID = newSRID;
		return true;
	}
	return false;
}

size_t Attrib::GeomRawSize() const {
	size_t size = 4; // Value.Geom.Flags
	size += sizeof(GeomHeader);
	size += PolyPartArraySize(Value.Geom.Head->NumParts) * sizeof(uint32_t);
	size += GeomBytesPerVertex(Value.Geom.Flags) * GeomTotalVertexCount();
	return size;
}

static_assert(sizeof(GeomFlags) == 4, "Geom.Flags size expected to be 4");

void Attrib::GeomCopyRawOut(void* dst) const {
	uint8_t*  p     = (uint8_t*) dst;
	GeomFlags flags = Value.Geom.Flags;
	flags |= GeomMakePackedTypeFlag(Type);
	memcpy(p, &flags, sizeof(flags));
	p += sizeof(flags);
	size_t dynSize = sizeof(GeomHeader) + sizeof(uint32_t) * PolyPartArraySize(Value.Geom.Head->NumParts) + GeomBytesPerVertex(Value.Geom.Flags) * GeomTotalVertexCount();
	memcpy(p, Value.Geom.Head, dynSize);
}

void Attrib::GeomCopyRawIn(const void* src, size_t len, Allocator* alloc) {
	Reset();
	const uint8_t* p     = (const uint8_t*) src;
	GeomFlags      flags = (GeomFlags) * ((uint32_t*) p);
	p += sizeof(flags);
	Type             = GeomTypeFromPackedFlags(flags);
	flags            = (GeomFlags)((uint32_t) flags & ~(uint32_t) GeomFlags::PackedTypeMask);
	Value.Geom.Flags = flags;
	size_t dynSize   = len - sizeof(flags);
	if (alloc) {
		Value.Geom.Head = (GeomHeader*) alloc->Alloc(dynSize);
		Flags |= Flags::CustomHeap;
	} else {
		switch (Type) {
		case Type::GeomPoint: Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocPoint(dynSize); break;
		case Type::GeomMultiPoint: Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocGeom(dynSize); break;
		case Type::GeomPolyline: Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocGeom(dynSize); break;
		case Type::GeomPolygon: Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocGeom(dynSize); break;
		default: IMQS_DIE();
		}
	}
	memcpy(Value.Geom.Head, p, dynSize);
}

void Attrib::SetTempGeomRaw(const void* src, size_t len) {
	Reset();
	const uint8_t* p     = (const uint8_t*) src;
	GeomFlags      flags = (GeomFlags) * ((uint32_t*) p);
	p += sizeof(flags);
	Type             = GeomTypeFromPackedFlags(flags);
	flags            = (GeomFlags)((uint32_t) flags & ~(uint32_t) GeomFlags::PackedTypeMask);
	Value.Geom.Flags = flags;
	Value.Geom.Head  = (GeomHeader*) p;
	Flags |= Flags::CustomHeap;
}

const double* Attrib::GeomFirstVertex() const {
	return GeomVerticesDbl();
}

const double* Attrib::GeomLastVertex() const {
	return GeomVerticesDbl() + GeomDimensions() * (GeomTotalVertexCount() - 1);
}

uint32_t Attrib::GeomNumExternalRings() const {
	uint32_t np = GeomNumParts();
	if (Type != Type::GeomPolygon)
		return np;

	uint32_t ext = 0;
	for (size_t i = 0; i < np; i++) {
		int start, count;
		if (!!(GeomPart(i, start, count) & GeomPartFlag_ExteriorRing))
			ext++;
	}
	return ext;
}

Attrib& Attrib::operator=(const Attrib& b) {
	CopyFrom(b);
	return *this;
}

Attrib& Attrib::operator=(Attrib&& b) {
	if (this != &b) {
		Reset();
		memcpy(this, &b, sizeof(b));
		b.Type  = dba::Type::Null;
		b.Flags = None;
	}
	return *this;
}

bool Attrib::operator==(const Attrib& b) const {
	if (Type != b.Type)
		return false;
	switch (Type) {
	case Type::Null: return true;
	case Type::Bool: return Value.Bool == b.Value.Bool;
	case Type::Int16: return Value.Int16 == b.Value.Int16;
	case Type::Int32: return Value.Int32 == b.Value.Int32;
	case Type::Int64: return Value.Int64 == b.Value.Int64;
	case Type::Float: return Value.Float == b.Value.Float;
	case Type::Double: return Value.Double == b.Value.Double;
	case Type::Text: return Value.Text.Size == b.Value.Text.Size && memcmp(Value.Text.Data, b.Value.Text.Data, Value.Text.Size) == 0;
	case Type::Guid: return *Value.Guid == *b.Value.Guid;
	case Type::Date: return Value.Date.Sec == b.Value.Date.Sec && Value.Date.Nsec == b.Value.Date.Nsec;
	case Type::Time:
		return false; // TODO
	case Type::Bin: return Value.Bin.Size == b.Value.Bin.Size && memcmp(Value.Bin.Data, b.Value.Bin.Data, Value.Bin.Size) == 0;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
		return GeomEquals(b);
	default:
		break;
	}
	IMQS_DIE_MSG("Unimplemented type in Attrib::operator==");
	return false;
}

bool Attrib::operator!=(const Attrib& b) const {
	return !(*this == b);
}

bool Attrib::operator<(const Attrib& b) const {
	return Compare(b) < 0;
}

bool Attrib::operator<=(const Attrib& b) const {
	return Compare(b) <= 0;
}

bool Attrib::operator>(const Attrib& b) const {
	return Compare(b) > 0;
}

bool Attrib::operator>=(const Attrib& b) const {
	return Compare(b) >= 0;
}

void Attrib::CopyFrom(const Attrib& b, Allocator* alloc) {
	Reset();
	switch (b.Type) {
	case Type::Null: break;
	case Type::Bool: Value.Bool = b.Value.Bool; break;
	case Type::Int16: Value.Int16 = b.Value.Int16; break;
	case Type::Int32: Value.Int32 = b.Value.Int32; break;
	case Type::Int64: Value.Int64 = b.Value.Int64; break;
	case Type::Float: Value.Float = b.Value.Float; break;
	case Type::Double: Value.Double = b.Value.Double; break;
	case Type::Text: SetTextWithLen(b.Value.Text.Data, b.Value.Text.Size, alloc); break;
	case Type::Guid: SetGuid(*b.Value.Guid, alloc); break;
	case Type::Date: SetDate(b.Date()); break;
	case Type::Time: break;
	case Type::Bin: SetBin(b.Value.Bin.Data, b.Value.Bin.Size, alloc); break;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
		SetPointsInternal(b.Type, b.Value.Geom.Head->NumParts, b.Value.Geom.Flags, b.GeomVertices(), b.Value.Geom.Head->SRID, alloc);
		break;
	case Type::GeomPolyline:
	case Type::GeomPolygon:
		SetPolyInternal(b.Type, b.Value.Geom.Flags, b.GeomNumParts(), b.GeomParts(), b.GeomVertices(), b.Value.Geom.Head->SRID, alloc);
		break;
	default:
		IMQS_DIE();
	}
	// We can only set Type AFTER copying data, because functions such as SetTextWithLen will
	// call Free(), and if our Type is already "Text", then Free will try and free bogus memory.
	Type = b.Type;
}

void Attrib::Reset() {
	Free();
	Type  = Type::Null;
	Flags = 0;
}

void Attrib::Free() {
	if (!!(Flags & Flags::CustomHeap))
		return;

	switch (Type) {
	case Type::Text:
		MemPool::FreeText(Value.Text.Data);
		break;
	case Type::Guid:
		MemPool::FreeGuid(Value.Guid);
		break;
	case Type::Bin:
		MemPool::FreeBin(Value.Bin.Data);
		break;
	case Type::GeomPoint:
		MemPool::FreePoint(Value.Geom.Head);
		break;
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
		MemPool::FreeGeom(Value.Geom.Head);
		break;
	default:
		break;
	}
}

void Attrib::PrepareText(size_t len, Allocator* alloc) {
	IMQS_ASSERT(len < MaxDataLen - 1);
	Reset();
	Type            = Type::Text;
	Value.Text.Size = (int) len;
	if (alloc) {
		Value.Text.Data = (char*) alloc->Alloc(len + 1);
		Flags |= Flags::CustomHeap;
	} else {
		Value.Text.Data = MemPool::AllocText(len + 1);
	}
}

void Attrib::CopyVertexIn(void* dst, const void* src, GeomFlags flags) {
	CopyVertexIn(dst, src, flags, GeomBytesPerVertex(flags));
}

void Attrib::CopyVertexIn(void* dst, const void* src, GeomFlags flags, size_t vertexSize) {
	memcpy(dst, src, vertexSize);
}

void Attrib::CopyVerticesIn(int numVerts, const void* vx, GeomFlags flags) {
	auto   dst = (uint8_t*) GeomVertices();
	auto   src = (const uint8_t*) vx;
	size_t bpv = GeomBytesPerVertex(flags);
	for (uint32_t i = 0; i < (uint32_t) numVerts; i++, src += bpv, dst += bpv)
		CopyVertexIn(dst, src, flags, bpv);
}

void Attrib::SetPointsInternal(dba::Type type, int numParts, GeomFlags flags, const void* vx, int srid, Allocator* alloc) {
	Reset();
	Type             = type;
	Value.Geom.Flags = flags;
	size_t bytes     = sizeof(GeomHeader) + (numParts * GeomBytesPerVertex(flags));
	if (alloc) {
		Flags |= Flags::CustomHeap;
		Value.Geom.Head = (GeomHeader*) alloc->Alloc(bytes);
	} else {
		if (type == Type::GeomPoint)
			Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocPoint(bytes);
		else
			Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocGeom(bytes);
	}
	Value.Geom.Head->NumParts = numParts;
	Value.Geom.Head->SRID     = srid;
	if (vx != nullptr)
		CopyVerticesIn(numParts, vx, flags);
}

void Attrib::SetPolyInternal(dba::Type type, GeomFlags flags, int numParts, const uint32_t* parts, const void* vx, int srid, Allocator* alloc) {
	IMQS_ASSERT(type == Type::GeomPolyline || type == Type::GeomPolygon);
	Reset();

	int prev = 0;
	for (int i = 1; i < numParts; i++) {
		int pp = parts[i] & GeomPartFlag_Mask;
		IMQS_ASSERT(pp - prev != 0);
	}

// If polygon rings are not ordered the way we need them for WKB serialization, then reorder them now
#if defined(_DEBUG)
	// Allocate much less memory during debug builds. The MSVC CRT will fill these static buffers
	// up with 0xCC, which ends up just making the code slower, on average.
	uint32_t static_newParts[16];
	uint8_t  static_newVx[64];
#else
	uint32_t static_newParts[32];
	uint8_t  static_newVx[4096];
#endif
	uint32_t* newParts   = static_newParts;
	uint8_t*  newVx      = static_newVx;
	size_t    bytesPerVx = GeomBytesPerVertex(flags);
	if (type == Type::GeomPolygon && vx != nullptr) {
		if (numParts > 1 && !(flags & GeomFlags::RingsInWKBOrder)) {
			uint32_t numVx = parts[numParts] & GeomPartFlag_Mask;

			if (numParts + 1 > arraysize(static_newParts))
				newParts = (uint32_t*) imqs_malloc_or_die((numParts + 1) * sizeof(uint32_t));

			if (numVx * bytesPerVx > arraysize(static_newVx))
				newVx = (uint8_t*) imqs_malloc_or_die(numVx * bytesPerVx);

			geom::FixRingOrderWKB(GeomIsDouble(flags), dba::GeomDimensions(flags), numParts, parts, vx, newParts, newVx);
			parts = newParts;
			vx    = newVx;
		} else if (numParts == 1) {
			// Make sure that the only part is marked as exterior
			memcpy(newParts, parts, sizeof(parts[0]) * 2);
			newParts[0] |= GeomPartFlag_ExteriorRing;
			parts = newParts;
		}
		flags |= GeomFlags::RingsInWKBOrder;
	}

	Type              = type;
	Value.Geom.Flags  = flags;
	uint32_t numVerts = parts[numParts] & GeomPartFlag_Mask;
	size_t   bytes    = sizeof(GeomHeader) + PolyPartArraySize(numParts) * sizeof(uint32_t) + (size_t) numVerts * GeomBytesPerVertex(flags);
	if (alloc) {
		Flags |= Flags::CustomHeap;
		Value.Geom.Head = (GeomHeader*) alloc->Alloc(bytes);
	} else {
		Value.Geom.Head = (GeomHeader*) MemPool::GetPoolForThread()->AllocGeom(bytes);
	}

	Value.Geom.Head->NumParts = numParts;
	Value.Geom.Head->SRID     = srid;

	memcpy(GeomParts(), parts, (1 + numParts) * sizeof(uint32_t));

	if (vx != nullptr)
		CopyVerticesIn(numVerts, vx, flags);

	if (newParts != static_newParts)
		free(newParts);

	if (newVx != static_newVx)
		free(newVx);
}

uint32_t* Attrib::GeomParts() {
	return const_cast<uint32_t*>(((const Attrib*) this)->GeomParts());
}

const uint32_t* Attrib::GeomParts() const {
	IMQS_ASSERT(Type == Type::GeomPolygon || Type == Type::GeomPolyline);
	return (uint32_t*) (Value.Geom.Head + 1);
}

bool Attrib::GeomEquals(const Attrib& b) const {
	if (Type != b.Type)
		return false;

	if (Value.Geom.Head->NumParts != b.Value.Geom.Head->NumParts)
		return false;

	if (Value.Geom.Head->SRID != b.Value.Geom.Head->SRID)
		return false;

	if (Value.Geom.Flags != b.Value.Geom.Flags)
		return false;

	switch (Type) {
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
		break;
	case Type::GeomPolygon:
	case Type::GeomPolyline:
		if (memcmp(GeomParts(), b.GeomParts(), Value.Geom.Head->NumParts * sizeof(uint32_t)) != 0)
			return false;
		break;
	default:
		IMQS_DIE_MSG("Not a geometry type");
	}

	if (GeomTotalVertexCount() != b.GeomTotalVertexCount())
		return false;

	return memcmp(GeomVertices(), b.GeomVertices(), GeomTotalVertexCount() * GeomBytesPerVertex(Value.Geom.Flags)) == 0;
}

bool Attrib::TextEq(const char* s) const {
	auto cpA = utfz::cp(Value.Text.Data, Value.Text.Size);
	auto cpB = utfz::cp(s);
	auto ia  = cpA.begin();
	auto ib  = cpB.begin();
	for (; ia != cpA.end() && ib != cpB.end(); ++ia, ++ib) {
		if (*ia != *ib)
			return false;
	}
	return ia == cpA.end() && ib == cpB.end();
}

bool Attrib::TextEqNoCase(const char* s) const {
	auto cpA = utfz::cp(Value.Text.Data, Value.Text.Size);
	auto cpB = utfz::cp(s);
	auto ia  = cpA.begin();
	auto ib  = cpB.begin();
	for (; ia != cpA.end() && ib != cpB.end(); ++ia, ++ib) {
		if (Lower(*ia) != Lower(*ib))
			return false;
	}
	return ia == cpA.end() && ib == cpB.end();
}

size_t Attrib::PolyPartArraySize(int numParts) {
	int sizeWithSentinal = numParts + 1;
	int roundedUpToEven  = (sizeWithSentinal + 1) & ~1;
	return roundedUpToEven;
}
} // namespace dba
} // namespace imqs
