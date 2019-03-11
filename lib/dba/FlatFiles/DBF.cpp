#include "pch.h"
#include "DBF.h"

namespace imqs {
namespace dba {

// If numeric fields have a width greater than or equal to 10, and their type is ambiguous, then treat them as Int64 (otherwise Int32).
static const int Int3264Cutoff = 10;

Error DBF::Open(const std::string& filename, bool create) {
	int flags = create ? xbase::OpenFlagCreate : 0;
	flags |= xbase::OpenFlagTolerateMissingMemoFile;
	auto err = DB.Open(filename, flags);
	if (!err.OK())
		return err;
	int n = DB.FieldCount();
	for (int i = 0; i < n; i++) {
		auto          sf = DB.GetField(i);
		schema::Field df;
		df.Name  = sf->Name;
		df.Width = sf->Length;
		switch (sf->PublicType) {
		case xbase::PublicFieldNull: df.Type = Type::Null; break;
		case xbase::PublicFieldText: df.Type = Type::Text; break;
		case xbase::PublicFieldInteger: df.Type = sf->Length >= Int3264Cutoff ? Type::Int64 : Type::Int32; break;
		case xbase::PublicFieldLogical: df.Type = Type::Bool; break;
		case xbase::PublicFieldDate: df.Type = Type::Date; break;
		case xbase::PublicFieldDateTime: df.Type = Type::Date; break;
		case xbase::PublicFieldReal: df.Type = Type::Double; break;
		}
		if (df.Type == Type::Null)
			continue;
		FieldsCacheIndex.push_back(i);
		FieldsCache.push_back(df);
	}
	return Error();
}

int64_t DBF::RecordCount() {
	return DB.RecordCount();
}

std::vector<schema::Field> DBF::Fields() {
	return FieldsCache;
}

// See https://gis.stackexchange.com/questions/3529/which-character-encoding-is-used-by-the-dbf-file-in-shapefiles about encoding

static void Latin1ToUtf8(const char* src, Attrib& val, Allocator* alloc) {
	// measure output size and allocate storage
	size_t outSize = 0;
	for (size_t i = 0; src[i]; i++) {
		if (src[i] > 0)
			outSize += 1;
		else
			outSize += 2;
	}
	val.SetText(nullptr, outSize, alloc);

	// transcribe from Latin1 to utf8
	char* dst = val.Value.Text.Data;
	for (size_t i = 0; src[i]; i++) {
		if (src[i] > 0) {
			*dst++ = src[i];
		} else {
			char   buf[5];
			size_t n = utfz::encode(buf, (unsigned char) src[i]);
			// If you look at the UTF8 table, you can be sure that a value between 0x80 and 0xff will encode to exactly 2 bytes.
			dst[0] = buf[0];
			dst[1] = buf[1];
			dst += 2;
		}
	}
	*dst = 0;
}

Error DBF::Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) {
	int         f = FieldsCacheIndex[field];
	char        buf[128];
	int64_t     i64 = 0;
	time::Time  tval;
	double      dval = 0;
	bool        bval = false;
	std::string str;

	val.SetNull();

	Error err = DB.SetRecord((int) record);
	if (!err.OK())
		return err;

	switch (FieldsCache[field].Type) {
	case Type::Text:
		err = DB.ReadTextA_Fast(f, buf, sizeof(buf));
		if (err == xbase::DB::ErrNull)
			return Error();

		if (err == xbase::DB::ErrNeedMoreSpace) {
			err = DB.ReadTextA(f, str);
			if (err == xbase::DB::ErrNull)
				return Error();
			if (err.OK())
				Latin1ToUtf8(str.c_str(), val, alloc);
			//val.SetText(str, alloc);
		} else if (err.OK()) {
			Latin1ToUtf8(buf, val, alloc);
			//val.SetText(buf, -1, alloc);
		}
		break;
	case Type::Int16:
	case Type::Int32:
	case Type::Int64:
		err = DB.ReadInteger(f, i64);
		if (err == xbase::DB::ErrNull)
			return Error();
		if (err.OK()) {
			if (FieldsCache[field].Type == Type::Int16)
				val.SetInt16((int16_t) i64);
			else if (FieldsCache[field].Type == Type::Int32)
				val.SetInt32((int32_t) i64);
			else
				val.SetInt64(i64);
		}
		break;
	case Type::Bool:
		err = DB.ReadLogical(f, bval);
		if (err == xbase::DB::ErrNull)
			return Error();
		if (err.OK())
			val.SetBool(bval);
		break;
	case Type::Date:
		err = DB.ReadDate(f, tval);
		if (err == xbase::DB::ErrNull)
			return Error();
		if (err.OK())
			val.SetDate(tval);
		break;
	case Type::Float:
	case Type::Double:
		err = DB.ReadReal(f, dval);
		if (err == xbase::DB::ErrNull)
			return Error();
		if (err.OK()) {
			if (FieldsCache[field].Type == Type::Float)
				val.SetFloat((float) dval);
			else
				val.SetDouble(dval);
		}
		break;
	default:
		IMQS_DIE();
	}

	return err;
}

} // namespace dba
} // namespace imqs