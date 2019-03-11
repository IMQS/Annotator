#pragma once

#include "../Attrib.h"

namespace imqs {
namespace dba {

//class Attrib;

// Attribute variable argument function helpers
namespace varargs {

// A temporary stack argument that holds a reference to the actual value.
// This is built for creating functions that allow us to do things like:
//
//   container.Add(123, "string", 5.5);
//
class Arg {
public:
	union {
		bool          Bool;
		int32_t       I16;
		int32_t       I32;
		int64_t       I64;
		const char*   Txt;
		float         Flt;
		double        Dbl;
		const Guid*   Guid;
		const Attrib* Attrib;
	};
	Type Type;

	Arg() {}
	Arg(bool v) : Type(Type::Bool), Bool(v) {}
	Arg(int16_t v) : Type(Type::Int16), I16(v) {}
	Arg(int32_t v) : Type(Type::Int32), I32(v) {}
	Arg(int64_t v) : Type(Type::Int64), I64(v) {}
	Arg(const char* v) : Type(Type::Text), Txt(v) {}
	Arg(const std::string& v) : Type(Type::Text), Txt(v.c_str()) {}
	Arg(float v) : Type(Type::Float), Flt(v) {}
	Arg(double v) : Type(Type::Double), Dbl(v) {}
	Arg(const imqs::Guid& v) : Type(Type::Guid), Guid(&v) {}
	Arg(const dba::Attrib& v) : Type(Type::GeomAny), Attrib(&v) {} // GeomAny is a special value in this context. We typically use this to pass geometry through, so somewhat appropriate.

	// Fill 'v' with the deep contents of 'this'. The lifetime of 'v' must not exceed the lifetime of Arg
	void ToAttribDeep(dba::Attrib& v) const {
		switch (Type) {
		case dba::Type::Bool: v.SetBool(Bool); break;
		case dba::Type::Int16: v.SetInt16(I16); break;
		case dba::Type::Int32: v.SetInt32(I32); break;
		case dba::Type::Int64: v.SetInt64(I64); break;
		case dba::Type::Text: v.SetTempText(Txt, strlen(Txt)); break;
		case dba::Type::Float: v.SetFloat(Flt); break;
		case dba::Type::Double: v.SetDouble(Dbl); break;
		case dba::Type::Guid: v.SetTempGuid(Guid); break;
		case dba::Type::GeomAny:
			v.Type  = Attrib->Type;
			v.Flags = Attrib->Flags | dba::Attrib::Flags::CustomHeap;
			memcpy(&v.Value, &Attrib->Value, sizeof(dba::Attrib::Value));
			break;
		default:
			break;
		}
	}
};

// Recursive template functions to build up an array of Arg objects, from a ...list
inline void PackArgs(Arg* pack) {
}

inline void PackArgs(Arg* pack, const Arg& arg) {
	*pack = arg;
}

template <typename... Args>
void PackArgs(Arg* pack, const Arg& arg, const Args&... args) {
	*pack = arg;
	PackArgs(pack + 1, args...);
}

// A temporary stack argument that holds a reference to the actual value.
// This is built for creating functions that allow us to do things like:
//
//   int64_t     i64;
//   std::string str;
//   tx->Query("SELECT rowid, text FROM table", i64, str);
//
class OutArg {
public:
	union {
		bool*        Bool;
		int16_t*     I16;
		int32_t*     I32;
		int64_t*     I64;
		std::string* Txt;
		float*       Flt;
		double*      Dbl;
		Guid*        Guid;
		time::Time*  Date;
		Attrib*      Attrib;
		// If you add more representations, then be sure to add support inside Attrib::AssignTo
	};
	Type Type;

	OutArg() {}
	OutArg(bool& v) : Type(Type::Bool), Bool(&v) {}
	OutArg(int16_t& v) : Type(Type::Int16), I16(&v) {}
	OutArg(int32_t& v) : Type(Type::Int32), I32(&v) {}
	OutArg(int64_t& v) : Type(Type::Int64), I64(&v) {}
	OutArg(std::string& v) : Type(Type::Text), Txt(&v) {}
	OutArg(float& v) : Type(Type::Float), Flt(&v) {}
	OutArg(double& v) : Type(Type::Double), Dbl(&v) {}
	OutArg(imqs::Guid& v) : Type(Type::Guid), Guid(&v) {}
	OutArg(time::Time& v) : Type(Type::Date), Date(&v) {}
	OutArg(dba::Attrib& v) : Type(Type::GeomAny), Attrib(&v) {} // GeomAny is a special value in this context. We do typically use this to pass geometry through, so somewhat appropriate.
};

// Recursive template functions to build up an array of Arg objects, from a ...list
inline void PackOutArgs(OutArg* pack) {
}

inline void PackOutArgs(OutArg* pack, OutArg arg) {
	*pack = arg;
}

template <typename... Args>
void PackOutArgs(OutArg* pack, OutArg arg, Args&... args) {
	*pack = arg;
	PackOutArgs(pack + 1, args...);
}
} // namespace varargs
} // namespace dba
} // namespace imqs
