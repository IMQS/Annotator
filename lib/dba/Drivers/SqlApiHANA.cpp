#include "pch.h"
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "SqlApiHANA.h"
#include "HANASchema.h"
#include "../Attrib.h"
#include "../AttribGeom.h"

namespace imqs {
namespace dba {

Error ToErrorHANA(const SAException& e) {
	std::string msg = (const char*) e.ErrText();

	// 42S02 [SAP AG][LIBODBCHDB DLL][HDBODBC] Base table or view not found;259 invalid table name: one: line 1 col 12 (at pos 11)
	// S1000 [SAP AG][LIBODBCHDB DLL][HDBODBC] General error;260 invalid column name: X
	// 23000 [SAP AG][LIBODBCHDB DLL][HDBODBC] Integrity constraint violation;301 unique constraint violated: Table(one), Index(one_unik)

	auto posTableOrViewNotFound = msg.find("Base table or view not found");
	auto posInvalidColumnName   = msg.find(";260 invalid column name:");
	auto posUniqueViolated      = msg.find(";301 unique constraint violated:");
	auto pos259                 = msg.find(";259 ");
	if (posTableOrViewNotFound != -1) {
		if (pos259 != -1)
			return Error::Fmt("%v: %v", ErrStubTableNotFound, msg.substr(pos259 + 5));
		else
			return Error::Fmt("%v: %v", ErrStubTableNotFound, msg);
	} else if (posInvalidColumnName != -1) {
		return Error::Fmt("%v: %v", ErrStubFieldNotFound, msg.substr(posInvalidColumnName + 5));
	} else if (posUniqueViolated != -1) {
		return Error::Fmt("%v: %v", ErrStubKeyViolation, msg.substr(posUniqueViolated + 5));
	}

	return Error(msg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialectFlags HANADialect::Flags() {
	return SqlDialectFlags::AlterSchemaInsideTransaction | SqlDialectFlags::MultiRowDummyUnionInsert;
}

dba::Syntax HANADialect::Syntax() {
	return dba::Syntax::ANSI;
}

void HANADialect::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	switch (type) {
	case Type::Int32:
		if (!!(flags & TypeFlags::AutoIncrement))
			s += "INTEGER PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY";
		else
			s += "INTEGER";
		break;
	case Type::Int64:
		if (!!(flags & TypeFlags::AutoIncrement))
			s += "BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY";
		else
			s += "BIGINT";
		break;
	case Type::Double: s += "DOUBLE"; break;
	case Type::Text:
		if (width_or_srid != 0)
			s += tsf::fmt("NVARCHAR(%v)", width_or_srid);
		else
			s += tsf::fmt("NVARCHAR(%v)", HANASchemaWriter::DefaultTextFieldWidth);
		break;
	case Type::Guid: s += "VARBINARY(16)"; break;
	case Type::Bin:
		if (width_or_srid != 0)
			s += tsf::fmt("VARBINARY(%v)", width_or_srid);
		else
			s += tsf::fmt("VARBINARY(%v)", HANASchemaWriter::DefaultBinFieldWidth);
		break;
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
	case Type::GeomAny:
		s += "ST_GEOMETRY";
		if (width_or_srid != 0)
			s.Fmt("(%v)", width_or_srid);
		break;
	default:
		SqlDialect::FormatType(s, type, width_or_srid, flags);
	}
}

void HANADialect::NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) {
	auto funcName = ast.FuncName.c_str();
	switch (hash::crc32(funcName, strlen(funcName))) {
	case "dba_ST_GeomFrom"_crc32:
		s += "ST_GeomFromEWKB"; // This must match the output produced by ST_GeomFrom
		break;
	case "dba_ST_GeomFromText"_crc32:
		s += "ST_GeomFromText";
		break;
	case "dba_ST_AsGeom"_crc32:
		s += ".ST_AsEWKB";
		break;
	default:
		s += funcName;
	}
}

void HANADialect::NativeHexLiteral(const char* hexLiteral, SqlStr& s) {
	s += tsf::fmt("x'%v'", hexLiteral);
}

bool HANADialect::UseThisCall(const char* funcName) {
	return false;
}

void HANADialect::ST_GeomFrom(SqlStr& s, const char* insertElement) {
	s += "ST_GeomFromEWKB(";
	s += insertElement;
	s += ")";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlApiHANARows::SqlApiHANARows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con) : SqlApiRows(sql, type, stmtCmd, con) {
}

Error SqlApiHANARows::Get(size_t col, Attrib& val, Allocator* alloc) {
	try {
		SAField& v = Cmd->Field((int) col + 1);
		if (v.isNull()) {
			val.SetNull();
			return Error();
		}
		switch (v.FieldType()) {
		case SA_dtLongBinary:
			if (v.Name().Find(".ST_ASEWKB()") != -1) {
				return WKB::Decode(v.asBLob(), v.asBLob().GetBinaryLength(), val, alloc);
			}
			break;
		default:
			break;
		}
	} catch (SAException& e) {
		return MyConn()->ToError(e);
	}

	return SqlApiRows::Get(col, val, alloc);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialect* SqlApiHANAConn::Dialect() {
	return new HANADialect();
}

Error SqlApiHANAConn::NewRows(const char* sql, SACommand* cmd, SqlApiRows*& rowsOut) {
	rowsOut = new SqlApiHANARows(sql, SA_CmdSQLStmt, cmd, this);
	return Error();
}

Error SqlApiHANAConn::ToError(const SAException& e) {
	return ToErrorHANA(e);
}

Error SqlApiHANAConn::Begin() {
	auto err = SqlApiConn::Begin();
	if (!err.OK())
		return err;

	DriverRows* rows;
	err = Exec("SET TRANSACTION AUTOCOMMIT DDL OFF", 0, nullptr, rows);
	delete rows;

	return err;
}

Error SqlApiHANAConn::Commit() {
	try {
		SACon.Commit();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

Error SqlApiHANAConn::Rollback() {
	try {
		SACon.Rollback();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlApiHANADriver::SqlApiHANADriver() {
}

SqlApiHANADriver::~SqlApiHANADriver() {
}

Error SqlApiHANADriver::Open(const ConnDesc& desc, DriverConn*& con) {
// Other things from the internet:
// Our tests do have one very small unicode test (labeled "utf-8"), so I'm ignoring these for now.
// CHAR_AS_UTF8=1           Might be HANA specific
// DriverUnicodeType=1      Looks like a generic ODBC thing
#ifdef _WIN32
	std::string dbStr = tsf::fmt("Driver=HDBODBC;SERVERNODE=%v:%v;DATABASENAME=%v", desc.Host, desc.Port, desc.Database);
#else
	std::string dbStr = tsf::fmt("Driver=/usr/sap/hdbclient/libodbcHDB.so;SERVERNODE=%v:%v;DATABASENAME=%v", desc.Host, desc.Port, desc.Database);
#endif
	auto hcon = new SqlApiHANAConn();
	auto err  = OpenInternal(SA_ODBC_Client, dbStr, desc, hcon);
	if (!err.OK()) {
		delete hcon;
		return err;
	}
	con = hcon;
	return Error();
}

imqs::dba::SchemaReader* SqlApiHANADriver::SchemaReader() {
	return &HSchemaReader;
}

imqs::dba::SchemaWriter* SqlApiHANADriver::SchemaWriter() {
	return &HSchemaWriter;
}

SqlDialect* SqlApiHANADriver::DefaultDialect() {
	return &StaticDialect;
}

} // namespace dba
} // namespace imqs
#endif