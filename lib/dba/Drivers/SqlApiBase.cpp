#include "pch.h"
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "SqlApiBase.h"
#include "../Attrib.h"
#include "../AttribGeom.h"

namespace imqs {
namespace dba {

/*
#define SQL_UNKNOWN_TYPE    0
#define SQL_CHAR            1
#define SQL_NUMERIC         2
#define SQL_DECIMAL         3
#define SQL_INTEGER         4
#define SQL_SMALLINT        5
#define SQL_FLOAT           6
#define SQL_REAL            7
#define SQL_DOUBLE          8
#if (ODBCVER >= 0x0300)
#define SQL_DATETIME        9
#endif
#define SQL_VARCHAR         12
*/

// These are observed values from HANA over ODBC. I have no idea to what degree these constants generalize to other DBs or transports.
// So.. I've now seen some of the same values coming out for the MSSQL ODBC driver. At the very least.. -6 was Bool for both HANA an MSSQL over ODBC.
enum OdbcNative {
	OdbcNativeBool           = -6,
	OdbcNativeNumeric        = -5,
	OdbcNativeInt32          = 4,
	OdbcNativeDouble         = 8,
	OdbcNativeStr            = 12,
	OdbcNative_SqlServerUUID = -11, // NO IDEA if this is reliable
};

static Type ConvertType(const SAField& f) {
	int nt = f.FieldNativeType();
	switch (nt) {
	case OdbcNativeBool:
		return Type::Bool;
	case OdbcNative_SqlServerUUID:
		return Type::Guid;
	}

	SADataType_t t = f.FieldType();
	switch (t) {
	case SA_dtBool: return Type::Bool;
	case SA_dtShort: return Type::Int32;
	case SA_dtUShort: return Type::Int32;
	case SA_dtLong: return Type::Int32;
	case SA_dtULong: return Type::Int32;
	case SA_dtDouble: return Type::Double;
	case SA_dtNumeric: return Type::Double;
	case SA_dtDateTime: return Type::Date;
	case SA_dtString: return Type::Text;
	case SA_dtBytes: return Type::Bin;
	case SA_dtLongBinary: return Type::Bin;
	case SA_dtLongChar: return Type::Text;
	case SA_dtBLob: return Type::Bin;
	case SA_dtCLob: return Type::Bin;
	default:
		return Type::Null;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlApiRows::SqlApiRows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con) : DriverRows(con) {
	if (stmtCmd) {
		IsStmt = true;
		Cmd    = stmtCmd;
	} else if (sql) {
		IsStmt = false;
		Cmd    = new SACommand(&con->SACon, strings::utf::ConvertUTF8ToWide(sql).c_str(), type);
	} else {
		IMQS_DIE_MSG("Either 'sql' or 'stmdCmd' must be non-null");
	}
}

SqlApiRows::~SqlApiRows() {
	if (!IsStmt) {
		try {
			delete Cmd;
		} catch (SAException&) {
		}
	}
}

void SqlApiRows::FromAttrib(const Attrib& s, SAParam& d) {
	switch (s.Type) {
	case Type::Null: d.setAsNull(); break;
	case Type::Bool: d.setAsBool() = s.Value.Bool; break;
	case Type::Int32: d.setAsLong() = (long) s.Value.Int32; break;
	case Type::Int64: d.setAsNumeric() = (sa_int64_t) s.Value.Int64; break;
	case Type::Double: d.setAsDouble() = s.Value.Double; break;
	case Type::Text: {
		// All the commented out code here is attempts to get unicode text inserted, but none of these
		// variations work. This is from Linux.
		if (strings::utf::IsLowAscii(s.Value.Text.Data, s.Value.Text.Size)) {
			//d.setAsString().SetUTF8Chars(s.Value.Text.Data, s.Value.Text.Size);
			d.setAsString() = s.Value.Text.Data;
		} else {
			//d.setAsString().SetUTF8Chars(s.Value.Text.Data, s.Value.Text.Size);
			wchar_t  dstStatic[200];
			wchar_t* dst    = dstStatic;
			size_t   dstLen = arraysize(dstStatic);
			size_t   maxLen = strings::utf::MaximumWideFromUtf8(s.Value.Text.Size);
			if (maxLen > dstLen) {
				dstLen = maxLen + 1;
				dst    = (wchar_t*) imqs_malloc_or_die(dstLen * sizeof(wchar_t));
			}
			strings::utf::ConvertUTF8ToWide(s.Value.Text.Data, s.Value.Text.Size, dst, dstLen);
			d.setAsString() = dst;
			if (dst != dstStatic)
				free(dst);
		}
		//d.setAsString().SetUTF8Chars(s.Value.Text.Data, s.Value.Text.Size);
		//d.setAsString().SetUTF8Chars(s.Value.Text.Data);
		//if (s.Value.Text.Size > 1) {
		//	auto&       ss  = d.setAsString();
		//	const char* foo = ss.GetUTF8Chars();
		//	size_t      len = ss.GetUTF8CharsLength();
		//	int         abc = 123;
		//}
		break;
	}
	case Type::Guid: {
		SAString& blob = d.setAsBLob();
		memcpy(blob.GetBinaryBuffer(16), s.Value.Guid, 16);
		blob.ReleaseBinaryBuffer(16);
		break;
	}
	case Type::Date: {
		int         year, mday, yday;
		int         hour, min, sec;
		time::Month mon;
		auto        sd = s.Date();
		sd.DateComponents(year, mon, mday, yday);
		sd.TimeComponents(hour, min, sec);
		d.setAsDateTime() = SADateTime(year, (int) mon, mday, hour, min, sec, (int) (sd.Nanoseconds() * 1000000000));
		break;
	}
	case Type::Bin: {
		SAString& blob = d.setAsBLob();
		memcpy(blob.GetBinaryBuffer(s.Value.Bin.Size), s.Value.Bin.Data, s.Value.Bin.Size);
		blob.ReleaseBinaryBuffer(s.Value.Bin.Size);
		break;
	}
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
	case Type::GeomAny: {
		auto wflags = WKB::WriterFlags::EWKB;
		if (s.Value.Geom.Head->SRID != 0)
			wflags |= WKB::WriterFlags::SRID;
		// The following line is true for the way in which we always create PostGIS schema. ie.. we always
		// set the field types to the "multi" variety. We never use linestring or polygon for the field type.
		// In order to support databases that we haven't created ourselves, we'll need better knowledge here,
		// informing whether we should toggle the following flag or not.
		// See detailed explanation in main docs under "MultiPolygon / MultiLineString Issue"
		//wflags |= WKB::WriterFlags::Force_Multi;
		if (s.GeomHasZ())
			wflags |= WKB::WriterFlags::Z;
		if (s.GeomHasM())
			wflags |= WKB::WriterFlags::M;
		// TODO: Make this buffer re-usable, by embedded it inside Conn or Stmt
		io::Buffer buf;
		size_t     len = WKB::Encode(wflags, s, buf);
		memcpy(d.setAsBLob().GetBinaryBuffer(len), buf.Buf, len);
		d.setAsBLob().ReleaseBinaryBuffer(len);
		break;
	}
	default:
		break;
	}
}

Error SqlApiRows::NextRow() {
	try {
		if (!Cmd->FetchNext())
			return ErrEOF;
		return Error();
	} catch (SAException& e) {
		return MyConn()->ToError(e);
	}
}

Error SqlApiRows::Get(size_t col, Attrib& val, Allocator* alloc) {
	try {
		SAField& v = Cmd->Field((int) col + 1);
		if (v.isNull()) {
			val.SetNull();
			return Error();
		}
		// These native types might be DB specific. I've only developed this for HANA, so I don't know.
		// I *think* they're ODBC-specific.
		int  nt       = v.FieldNativeType();
		bool fallback = false;
		switch (nt) {
		case OdbcNativeBool:
			val.SetBool((short) v != 0);
			break;
		case OdbcNativeInt32:
			val.SetInt32((long) v);
			break;
		case OdbcNativeDouble:
			val.SetDouble((double) v);
			break;
		case OdbcNativeNumeric: {
			SANumeric num = v.asNumeric();
			if (num.scale > 0)
				val.SetDouble((double) num);
			else
				val.SetInt64((sa_int64_t) num);
			break;
		}
		case OdbcNativeStr:
			val.SetText((const char*) v.asString(), alloc);
			break;
		default:
			fallback = true;
		}
		if (!fallback)
			return Error();

		// fall back to using the generic SqlApi type

		switch (v.FieldType()) {
		case SA_dtBool:
			val.SetBool((bool) v);
			break;
		case SA_dtShort:
			val.SetInt32((short) v);
			break;
		case SA_dtUShort:
			val.SetInt32((unsigned short) v);
			break;
		case SA_dtLong:
			val.SetInt64((long) v);
			break;
		case SA_dtULong:
			val.SetInt64((unsigned long) v);
			break;
		case SA_dtDouble:
			val.SetDouble((double) v);
			break;
		case SA_dtNumeric: {
			SANumeric num = v.asNumeric();
			if (num.scale > 0)
				val.SetDouble((double) num);
			else
				val.SetInt64((sa_int64_t) num);
			break;
		}
		case SA_dtString:
		case SA_dtCLob: {
			const auto& vs = v.asString();
			//const wchar_t* wide    = vs.GetWideChars();
			//auto           wideLen = vs.GetWideCharsLength();
			//auto           mcbs    = vs.GetMultiByteChars();
			//auto           mcbsLen = vs.GetMultiByteCharsLength();
			//val.SetText(vs.GetUTF8Chars(), vs.GetUTF8CharsLength(), alloc);
			val.SetText((const char*) vs, vs.GetLength(), alloc);
			//val.SetText(vs, -1, alloc);
			break;
		}
		case SA_dtDateTime: {
			auto st = v.asDateTime();
			val.SetDate(time::Time(st.GetYear(), (time::Month) st.GetMonth(), st.GetDay(), st.GetHour(), st.GetMinute(), st.GetSecond(), st.Fraction()));
			break;
		}
		case SA_dtBytes:
		case SA_dtLongBinary: {
			const auto& blob = v.asBLob();
			if (nt == OdbcNative_SqlServerUUID && blob.GetBinaryLength() == 16) {
				Guid g;
				memcpy(&g, (const void*) blob, 16);
				val.SetGuid(g, alloc);
			} else {
				val.SetBin((const void*) blob, blob.GetBinaryLength(), alloc);
			}
			break;
		}
		default:
			return Error("Unsupported field type");
		}
		return Error();
	} catch (SAException& e) {
		return MyConn()->ToError(e);
	}
}

Error SqlApiRows::Columns(std::vector<ColumnInfo>& cols) {
	try {
		cols.resize(Cmd->FieldCount());
		for (size_t i = 0; i < cols.size(); i++) {
			SAField& f   = Cmd->Field((int) i + 1);
			cols[i].Name = f.Name();
			cols[i].Type = ConvertType(f);
		}
		return Error();
	} catch (SAException& e) {
		return MyConn()->ToError(e);
	}
}

size_t SqlApiRows::ColumnCount() {
	return Cmd->FieldCount();
}

SqlApiConn* SqlApiRows::MyConn() {
	return (SqlApiConn*) DCon;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlApiStmt::SqlApiStmt(DriverConn* dcon) : DriverStmt(dcon) {
	Cmd = nullptr;
}

SqlApiStmt::~SqlApiStmt() {
	try {
		delete Cmd;
	} catch (SAException&) {
	}
}

Error SqlApiStmt::Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	SqlApiRows* rows = nullptr;
	auto        err  = MyConn()->NewRows(nullptr, Cmd, rows);
	if (!err.OK())
		return err;

	try {
		for (size_t i = 0; i < nParams; i++)
			rows->FromAttrib(*params[i], rows->Cmd->ParamByIndex((int) i));
		rows->Cmd->Execute();
		rowsOut = rows;
		return Error();
	} catch (SAException& e) {
		delete rows;
		return MyConn()->ToError(e);
	}
}

SqlApiConn* SqlApiStmt::MyConn() {
	return (SqlApiConn*) DCon;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Error SqlApiConn::Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	std::string translated;
	if (TranslateParamString(sql, ':', translated))
		sql = translated.c_str();

	SqlApiRows* rows = nullptr;
	auto        err  = NewRows(sql, nullptr, rows);
	if (!err.OK())
		return err;

	try {
		for (size_t i = 0; i < nParams; i++)
			rows->FromAttrib(*params[i], rows->Cmd->ParamByIndex((int) i));
		rows->Cmd->Execute();
		rowsOut = rows;
		return Error();
	} catch (SAException& e) {
		delete rows;
		return ToError(e);
	}
}

Error SqlApiConn::Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) {
	std::string translated;
	if (TranslateParamString(sql, ':', translated))
		sql = translated.c_str();

	SqlApiStmt* ps = new SqlApiStmt(this);
	ps->Cmd        = new SACommand(&SACon, strings::utf::ConvertUTF8ToWide(sql).c_str(), SA_CmdSQLStmt);
	stmt           = ps;
	return Error();
}

Error SqlApiConn::Begin() {
	try {
		SACon.setAutoCommit(SA_AutoCommitOff);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

Error SqlApiConn::Commit() {
	try {
		SACon.Commit();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

Error SqlApiConn::Rollback() {
	try {
		SACon.Rollback();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlApiDriver::SqlApiDriver() {
}

SqlApiDriver::~SqlApiDriver() {
}

Error SqlApiDriver::OpenInternal(SAClient_t client, std::string dbString, const ConnDesc& desc, SqlApiConn* con) {
	try {
		con->SACon.setOption(L"SQL_ATTR_CONNECTION_TIMEOUT") = L"5";
		con->SACon.setOption(L"SQL_ATTR_LOGIN_TIMEOUT")      = L"5";
		con->SACon.setClient(client);
		con->SACon.Connect(dbString.c_str(), desc.Username.c_str(), desc.Password.c_str(), client);
		return Error();
	} catch (SAException& e) {
		return con->ToError(e);
	}
}

} // namespace dba
} // namespace imqs
#endif