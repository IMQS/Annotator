#include "pch.h"
#include "PackedColumn.h"

namespace imqs {
namespace dba {

PackedColumn::PackedColumn() {
	Index = {0}; // add initial sentinel
}

PackedColumn::PackedColumn(const PackedColumn& b) {
	*this = b;
}

PackedColumn::PackedColumn(PackedColumn&& b) {
	*this = std::move(b);
}

PackedColumn::PackedColumn(dba::Type type) {
	SetType(type);
}

PackedColumn::~PackedColumn() {
	Clear();
}

PackedColumn& PackedColumn::operator=(const PackedColumn& b) {
	Clear();
	_Type            = b._Type;
	IsTypeStaticSize = b.IsTypeStaticSize;
	_IsNull          = b._IsNull;

	Index   = b.Index;
	VarSize = b.VarSize;
	DataCap = b.DataCap;
	DataLen = b.DataLen;
	Data    = (uint8_t*) imqs_malloc_or_die(DataCap);
	memcpy(Data, b.Data, DataLen);

	return *this;
}

PackedColumn& PackedColumn::operator=(PackedColumn&& b) {
	Clear();
	_Type            = b._Type;
	IsTypeStaticSize = b.IsTypeStaticSize;
	_IsNull          = std::move(b._IsNull);

	Index   = std::move(b.Index);
	VarSize = std::move(b.VarSize);
	DataCap = b.DataCap;
	DataLen = b.DataLen;
	Data    = b.Data;
	b.Data  = nullptr;

	b.Clear();
	return *this;
}

void PackedColumn::Reset() {
	Clear();
	_Type            = dba::Type::Null;
	IsTypeStaticSize = false;
}

void PackedColumn::Clear() {
	Index   = {0}; // add initial sentinel
	VarSize = {};
	free(Data);
	Data    = nullptr;
	DataLen = 0;
	DataCap = 0;
	_IsNull.Clear();
}

bool PackedColumn::Add(const dba::Attrib& val) {
	size_t i = _IsNull.Size();

	if (_Type == dba::Type::Null && !val.IsNull()) {
		// This is the first non-null object that we've seen, so we can nail down the type of this column.
		SetType(val.Type);
	}

	_IsNull.Add(val.IsNull());

	// If _Type is null, then it implies that val is null too, and so we have nothing further to do here
	if (_Type == dba::Type::Null)
		return true;

	if (!IsTypeStaticSize) {
		if (i == 0) {
			// On the very first addition, add an extra entry into Index. This entry sits at the end
			// and it is the sentinel (see comments at Index definition in class header).
			Index.push_back(0);
		}
		Index.push_back(0);
		if (VarSize.size() != 0)
			VarSize.push_back(0);
	}

	return Store(i, val);
}

bool PackedColumn::Store(size_t i, const Attrib& val) {
	_IsNull.Set(i, val.IsNull());

	// A possible exception that might make sense here is to detect the condition where you're storing multiple
	// types of geometry in the column. For example, your first observed geometry might be a Point, and then
	// later on you see a Polyline. That would be valid for a GeomAny field type, but the assertion below
	// would not allow that.
	IMQS_ASSERT(val.Type == Type::Null || val.Type == _Type || (_Type == Type::GeomAny && IsTypeGeom(val.Type)));

	size_t needBytes = 0;
	void*  valPtr    = nullptr;
	switch (_Type) {
	case dba::Type::Bool: needBytes = 1; break;
	case dba::Type::Int16: needBytes = 2; break;
	case dba::Type::Int32: needBytes = 4; break;
	case dba::Type::Int64: needBytes = 8; break;
	case dba::Type::Float: needBytes = 4; break;
	case dba::Type::Double: needBytes = 8; break;
	case dba::Type::Text:
	case dba::Type::JSONB:
		if (val.Type != Type::Null) {
			needBytes = val.Value.Text.Size + 1; // +1 for null terminator
			valPtr    = val.Value.Text.Data;
		}
		break;
	case dba::Type::Guid:
		needBytes = 16;
		valPtr    = val.Value.Guid;
		break;
	case dba::Type::Date: needBytes = sizeof(time::Time); break;
	case dba::Type::Time: IMQS_DIE(); break;
	case dba::Type::Bin:
		if (val.Type != Type::Null) {
			needBytes = val.Value.Bin.Size;
			valPtr    = val.Value.Bin.Data;
		}
		break;
	case dba::Type::GeomAny:
	case dba::Type::GeomPoint:
	case dba::Type::GeomMultiPoint:
	case dba::Type::GeomPolyline:
	case dba::Type::GeomPolygon:
		if (val.Type != Type::Null) {
			needBytes = val.GeomRawSize();
			valPtr    = val.Value.Geom.Head;
		}
		break;
	default: IMQS_DIE();
	}

	//IMQS_ASSERT(needBytes < 100 * 1024 * 1024); // This assertion was useful during development, but I don't think it's necessary to keep it live all the time anymore

	// Ensure that the user has not done a [GetDeep,Modify,Set] cycle, which is illegal, because
	// by the time we've grown Data, 'val' is pointing at garbage. This will only be a problem
	// if we actually need to grow our data, but if we only ran this assertion inside the
	// data growth path, then developers would be likely to miss a bug during testing.
	IMQS_ASSERT(valPtr == nullptr || (size_t) valPtr - (size_t) Data >= DataCap);

	if (DataLen + needBytes > DataCap) {
		size_t newCap = DataCap;
		newCap        = std::max(newCap * 2, (size_t) 256);
		while (newCap < DataLen + needBytes)
			newCap *= 2;
		void* b = realloc(Data, newCap);
		if (!b)
			return false;
		Data    = (uint8_t*) b;
		DataCap = newCap;
	}

	if (IsTypeStaticSize) {
		if (!val.IsNull()) {
			//IMQS_ASSERT(i * needBytes < DataCap); // This assertion was useful during development, but I don't think it's necessary to keep it live all the time anymore
			IMQS_ANALYSIS_ASSUME(val.Value.Guid != nullptr);
			switch (_Type) {
			case dba::Type::Bool: ((bool*) Data)[i] = val.Value.Bool; break;
			case dba::Type::Int16: ((int16_t*) Data)[i] = val.Value.Int16; break;
			case dba::Type::Int32: ((int32_t*) Data)[i] = val.Value.Int32; break;
			case dba::Type::Int64: ((int64_t*) Data)[i] = val.Value.Int64; break;
			case dba::Type::Float: ((float*) Data)[i] = val.Value.Float; break;
			case dba::Type::Double: ((double*) Data)[i] = val.Value.Double; break;
			case dba::Type::Guid: ((Guid*) Data)[i] = *val.Value.Guid; break;
			case dba::Type::Date: ((time::Time*) Data)[i] = val.Date(); break;
			case dba::Type::Time: IMQS_DIE(); break;
			default: IMQS_DIE();
			}
		}
	} else {
		// Variable sized object
		if (VarSize.size() != 0) {
			Index[i]   = DataLen;
			VarSize[i] = (uint32_t) needBytes;
		} else {
			// initial adding. update the sentinel, which is also the start of the next object
			Index[i + 1] = DataLen + needBytes;
		}

		if (val.IsNull()) {
			// do not increment DataLen
			return true;
		}
		// Data[DataLen + needBytes + 1] = 0xcc;
		switch (_Type) {
		case dba::Type::GeomAny:
		case dba::Type::GeomPoint:
		case dba::Type::GeomMultiPoint:
		case dba::Type::GeomPolyline:
		case dba::Type::GeomPolygon:
			val.GeomCopyRawOut(Data + DataLen);
			break;
		case dba::Type::JSONB:
		case dba::Type::Text:
			memcpy(Data + DataLen, val.Value.Text.Data, needBytes); // val.Value.Text.Data is null terminated, and needBytes includes that terminator
			break;
		case dba::Type::Bin:
			memcpy(Data + DataLen, val.Value.Bin.Data, needBytes);
			break;
		default:
			IMQS_DIE();
		}
		//IMQS_ASSERT(Data[DataLen + needBytes + 1] == 0xcc);
	}
	DataLen += needBytes;
	return true;
}

void PackedColumn::Set(size_t i, const Attrib& val) {
	IMQS_ASSERT(i < _IsNull.Size());

	if (_Type == Type::Null && val.Type != Type::Null)
		SetType(val.Type);

	if (val.Type == Type::Null) {
		_IsNull.Set(i, true);
		return;
	}

	if (!IsTypeStaticSize && VarSize.size() == 0) {
		size_t n = _IsNull.Size();
		VarSize.resize(n);
		for (size_t j = 0; j < n; j++)
			VarSize[j] = (uint32_t)(Index[j + 1] - Index[j]);
	}
	bool ok = Store(i, val);
	IMQS_ASSERT(ok);
}

void PackedColumn::SetType(dba::Type type) {
	if (_Type != dba::Type::Null) {
		if (Size() == 0) {
			// allow re-initialization to any other type, because PackedColumn is empty
			Clear();
		} else if (IsTypeGeom(_Type) && type == Type::GeomAny) {
			// Once the type has been set, and data has been added,
			// the only legal change of type is from Geom____ to GeomAny.
			_Type = type;
			return;
		} else {
			IMQS_DIE();
		}
	}
	_Type = type;

	size_t staticSize = 0;
	switch (_Type) {
	case dba::Type::Bool: staticSize = 1; break;
	case dba::Type::Int16: staticSize = 2; break;
	case dba::Type::Int32: staticSize = 4; break;
	case dba::Type::Int64: staticSize = 8; break;
	case dba::Type::Float: staticSize = 4; break;
	case dba::Type::Double: staticSize = 8; break;
	case dba::Type::Guid: staticSize = 16; break;
	case dba::Type::Date: staticSize = sizeof(time::Time); break;
	case dba::Type::Time: IMQS_DIE(); break;
	default:
		break;
	}

	IsTypeStaticSize = staticSize != 0;

	if (Size() != 0) {
		if (IsTypeStaticSize) {
			// All of the objects inside this newly allocated Data is null, so we don't bother filling it
			size_t bytes = staticSize * Size();
			Data         = (uint8_t*) imqs_malloc_or_die(bytes);
			DataLen      = bytes;
			DataCap      = bytes;
		} else {
			Index.resize(Size() + 1);
			IMQS_ASSERT(Index[0] == 0);
			IMQS_ASSERT(Index[Index.size() - 1] == 0);
		}
	}
}

bool PackedColumn::IsAllNull() const {
	return _IsNull.Size() == 0 || _IsNull.ScanAll() == BitVector::Mixture::AllOne;
}

bool PackedColumn::GrowTo(size_t size) {
	if (Size() >= size)
		return true;
	size_t n = size - Size();
	Attrib val;
	for (size_t i = 0; i < n; i++) {
		if (!Add(val))
			return false;
	}
	return true;
}

} // namespace dba
} // namespace imqs
