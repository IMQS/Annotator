#pragma once

#include "../Types.h"
#include "../Attrib.h"

namespace imqs {
namespace dba {

/* PackedColumn holds a tightly packed column of dba::Attrib objects, that have been
encoded in a memory efficient manner. All attributes inside a PackedColumn must
be of the same type (or null).
*/
class IMQS_DBA_API PackedColumn {
public:
	PackedColumn();
	PackedColumn(const PackedColumn& b);
	PackedColumn(PackedColumn&& b);
	PackedColumn(dba::Type type);
	~PackedColumn();

	PackedColumn& operator=(const PackedColumn& b);
	PackedColumn& operator=(PackedColumn&& b);

	void   Reset();                // Clear and reset type to null
	void   Clear();                // Discard all data, but preserve type
	bool   Add(const Attrib& val); // Add an item to the end. Add() will automatically set the column type, the first time it sees a non-null object. Returns false if out of memory.
	void   SetType(Type type);
	Attrib Get(size_t i, dba::Allocator* alloc = nullptr) const; // Retrieve a value
	void   Set(size_t i, const Attrib& val);                     // Change an existing value
	void   GetDeep(size_t i, Attrib& val) const;                 // Populate Attrib with a deep pointer. Use with caution. Pointers become invalid the moment this RecordSet is changed (eg with Add())
	bool   IsAllNull() const;                                    // Returns true if all entries are null
	bool   GrowTo(size_t size);                                  // Add null values until size is greater than or equal to specified size. Returns false if out of memory.

	dba::Type Type() const { return _Type; }
	size_t    Size() const { return _IsNull.Size(); }
	bool      IsNull(size_t i) const { return _IsNull[i]; }

private:
	dba::Type _Type            = Type::Null;
	bool      IsTypeStaticSize = false;
	BitVector _IsNull;

	// For variable length records (Text and Bin), points into Data.
	// There is always a sentinel at the end of Index, so that we know the length of the final record (initially, Index = [0])
	// Index is not used for fixed-length records.
	// Index has a slightly
	std::vector<size_t> Index;

	// This is only populated the first time Set() is called. Once Set() is called, we can no longer rely on
	// record-to-record differences in Index to indicate variable object size, because the objects will no longer
	// be contiguous.
	std::vector<uint32_t> VarSize;

	size_t   DataCap = 0;       // Size in bytes, of Data
	size_t   DataLen = 0;       // Length in bytes, of Data
	uint8_t* Data    = nullptr; // Storage for dynamic and static data

	size_t GetSize(size_t i) const;
	bool   Store(size_t i, const Attrib& val);
	void   GetInternal(size_t i, bool deep, Attrib& val, dba::Allocator* alloc) const;
};

inline Attrib PackedColumn::Get(size_t i, dba::Allocator* alloc) const {
	Attrib val;
	GetInternal(i, false, val, alloc);
	return val;
}

inline void PackedColumn::GetDeep(size_t i, Attrib& val) const {
	GetInternal(i, true, val, nullptr);
}

inline void PackedColumn::GetInternal(size_t i, bool deep, Attrib& val, dba::Allocator* alloc) const {
	using namespace dba;
	if (_IsNull[i]) {
		val.SetNull();
		return;
	}

	switch (_Type) {
	case dba::Type::Bool: val.SetBool(((bool*) Data)[i]); return;
	case dba::Type::Int16: val.SetInt16(((int16_t*) Data)[i]); return;
	case dba::Type::Int32: val.SetInt32(((int32_t*) Data)[i]); return;
	case dba::Type::Int64: val.SetInt64(((int64_t*) Data)[i]); return;
	case dba::Type::Float: val.SetFloat(((float*) Data)[i]); return;
	case dba::Type::Double: val.SetDouble(((double*) Data)[i]); return;
	case dba::Type::Text:
		if (deep)
			val.SetTempText((const char*) (Data + Index[i]), GetSize(i) - 1);
		else
			val.SetText((const char*) (Data + Index[i]), GetSize(i) - 1, alloc);
		return;
	case dba::Type::Guid:
		if (deep)
			val.SetTempGuid(&((Guid*) Data)[i]);
		else
			val.SetGuid(((Guid*) Data)[i], alloc);
		return;
	case dba::Type::Date: val.SetDate(((time::Time*) Data)[i]); return;
	case dba::Type::Time: IMQS_DIE(); return;
	case dba::Type::Bin:
		if (deep)
			val.SetTempBin(Data + Index[i], GetSize(i));
		else
			val.SetBin(Data + Index[i], GetSize(i), alloc);
		return;
	case dba::Type::JSONB:
		if (deep)
			val.SetTempJSONB((const char*) (Data + Index[i]), GetSize(i) - 1);
		else
			val.SetJSONB((const char*) (Data + Index[i]), GetSize(i) - 1, alloc);
		return;
	case dba::Type::GeomAny:
	case dba::Type::GeomPoint:
	case dba::Type::GeomMultiPoint:
	case dba::Type::GeomPolyline:
	case dba::Type::GeomPolygon:
		if (deep)
			val.SetTempGeomRaw(Data + Index[i], GetSize(i));
		else
			val.GeomCopyRawIn(Data + Index[i], GetSize(i), alloc);
		return;
	default:
		IMQS_DIE();
	}

	// should be unreachable
	IMQS_DIE();
}

inline size_t PackedColumn::GetSize(size_t i) const {
	if (VarSize.size() == 0)
		return Index[i + 1] - Index[i];
	else
		return VarSize[i];
}

} // namespace dba
} // namespace imqs
