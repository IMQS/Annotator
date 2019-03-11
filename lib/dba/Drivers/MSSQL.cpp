#include "pch.h"
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "MSSQL.h"
#include "../SqlParser/Generated/Parser.h"
#include "../Attrib.h"
#include "../AttribGeom.h"

/*
apt install libiodbc2

# https://docs.microsoft.com/en-us/sql/connect/odbc/linux-mac/installing-the-microsoft-odbc-driver-for-sql-server?view=sql-server-2017

# Ubuntu (16.04)
curl -fsSL https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
sudo curl -s -o /etc/apt/sources.list.d/mssql-release.list https://packages.microsoft.com/config/ubuntu/16.04/prod.list
sudo apt-get update
sudo ACCEPT_EULA=Y apt-get install -y msodbcsql17

# Setting up SQL Server Express for unit tests
See http://web.synametrics.com/sqlexpressremote.htm
Basically, you need to enable TCP/IP as a protocol, and you need to disable dynamic ports.
You also need to lock down the port on which the server listens. We use 1433 in the unit tests.

# Unicode
I am giving up on Unicode. I don't know what's going on. If you look inside IssNCliCursor::ConvertString, you'll see
that sometimes the resulting bytes are UTF8, and sometimes they are UTF16. I don't understand what's going on.

# Geometry
* We assume that all data is in SRID 4326 (latlon WGS84 degrees).
* We do not support Z or M, because we use WKB, and MSSQL doesn't support EWKB

*/

using namespace std;

namespace imqs {
namespace dba {

// When we select geometry out of an MSSQL database, we need to SELECT [field].STAsBinary().
// That will result in us getting out an attribute with type SA_dtBLob. At the point
// where we're doing the "read attribute", we don't know that this SA_dtBlob is in fact
// Well Known Binary. So, in order to work around this, we always rebuild our SELECT
// statement from above, as:
// SELECT [field].STAsBinary() AS _dbaWKB_
// This DOES imply that we're only able to select a single geometry field in any query
// (otherwise _dbaWKB_ would appear twice in the result set, which is illegal)
// So far, this constraint has been acceptable.
static const char* SpecialWKBFieldName = "_dbaWKB_";

Error ToErrorMSSQL(const SAException& e) {
	std::string msg = (const char*) e.ErrText();

	/*
	42S02 [Microsoft][ODBC Driver 17 for SQL Server][SQL Server]Cannot drop the table 'one', because it does not exist or you do not have permission.
	"01000 [Microsoft][ODBC Driver 17 for SQL Server][SQL Server]The statement has been terminated.\n23000 [Microsoft][ODBC Driver 17 for SQL Server][SQL Server]Cannot insert duplicate key row in object 'db"...

	*/

	auto posCannotDropTheTable                      = msg.find("Cannot drop the table");
	auto posBecauseItDoesNotExistOrYouDoNotHavePerm = msg.find("because it does not exist or you do not have permission");
	auto posInvalidColumnName                       = msg.find("Invalid column name");
	auto posInvalidObjectName                       = msg.find("Invalid object name");
	auto duplicateKey                               = msg.find("Cannot insert duplicate key row in object");

	if (posInvalidColumnName != -1) {
		return Error::Fmt("%v: %v", ErrStubFieldNotFound, msg);
	} else if (posCannotDropTheTable != -1 && posBecauseItDoesNotExistOrYouDoNotHavePerm != -1) {
		// I don't know how to distinguish between "do not have permission" and "table does not exist"
		return Error::Fmt("%v: %v", ErrStubTableNotFound, msg);
	} else if (posInvalidObjectName != -1) {
		return Error::Fmt("%v: %v", ErrStubTableNotFound, msg);
	} else if (duplicateKey != -1) {
		return Error::Fmt("%v: %v", ErrStubKeyViolation, msg);
	}

	/*
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
	*/

	return Error(msg);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialectFlags MSSQLDialect::Flags() {
	return SqlDialectFlags::UUID;
}

imqs::dba::Syntax MSSQLDialect::Syntax() {
	return Syntax::Microsoft;
}

void MSSQLDialect::NativeHexLiteral(const char* hexLiteral, SqlStr& s) {
	s += tsf::fmt("0x%v", hexLiteral);
}

void MSSQLDialect::NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) {
	auto funcName = ast.FuncName.c_str();
	switch (hash::crc32(funcName, strlen(funcName))) {
	case "dba_ST_GeomFrom"_crc32:
		s += "geometry::STGeomFromWKB";
		printFlags = sqlparser::SqlAST::PrintFlags::PrintAdd4326;
		break;
	case "dba_ST_GeomFromText"_crc32:
		s += "geometry::STGeomFromText";
		break;
	case "dba_ST_AsGeom"_crc32:
		s += string(".STAsBinary() AS ") + SpecialWKBFieldName;
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "dba_ST_Intersects"_crc32:
		// MSSQL requires the '= 1' after the STIntersects and STContains methods for the WHERE clause to work
		ast.Params[0]->Print(s);
		s += ".STIntersects(";
		ast.Params[1]->Print(s);
		s += ") = 1";
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "dba_ST_Contains"_crc32:
		// MSSQL requires the '= 1' after the STIntersects and STContains methods for the WHERE clause to work
		ast.Params[0]->Print(s);
		s += ".STContains(";
		ast.Params[1]->Print(s);
		s += ") = 1";
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "dba_Unix_Timestamp"_crc32:
		s += "DATEDIFF(second,'1970-01-01',";
		s.Identifier(ast.Params[0]->Variable);
		s += ")";
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	case "dba_ST_CoarseIntersect"_crc32:
		s += "(";
		s.Identifier(ast.Params[0]->Variable);
		s += ".Filter(geometry::STGeomFromText('";
		// expecting X1, Y1, X2, Y2
		s.Fmt("POLYGON ((%v %v, ", ast.Params[1]->Value.NumberVal, ast.Params[2]->Value.NumberVal);
		s.Fmt("%v %v, ", ast.Params[3]->Value.NumberVal, ast.Params[2]->Value.NumberVal);
		s.Fmt("%v %v, ", ast.Params[3]->Value.NumberVal, ast.Params[4]->Value.NumberVal);
		s.Fmt("%v %v, ", ast.Params[1]->Value.NumberVal, ast.Params[4]->Value.NumberVal);
		s.Fmt("%v %v))', 4326) ) = 1)", ast.Params[1]->Value.NumberVal, ast.Params[2]->Value.NumberVal);
		printFlags = sqlparser::SqlAST::PrintFlags::PrintExcludeParameters;
		break;
	default:
		s += funcName;
	}
}

bool MSSQLDialect::UseThisCall(const char* funcName) {
	// This is commented out for team phoenix in order to add the '= 1' in the dba_ST_Intersects case above
	/*if (strcmp(funcName, "dba_ST_Intersects") == 0)
		return true;*/
	return false;
}

void MSSQLDialect::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	if (flags & TypeFlags::AutoIncrement) {
		if (type == Type::Int32)
			s += "int";
		else if (type == Type::Int64)
			s += "bigint";
		else
			s += "'invalid - autoincrement must be int32 or int64'";
		s += " identity(1,1)";
		return;
	}
	switch (type) {
	case Type::Bool: s += "bit"; return;
	case Type::Int32: s += "int"; return;
	case Type::Int64: s += "bigint"; return;
	case Type::Double: s += "float(53)"; return;
	case Type::Text:
		if (width_or_srid == -1) {
			s += "nvarchar(max)";
		} else if (width_or_srid != 0) {
			s += tsf::fmt("nvarchar(%v)", width_or_srid);
		} else {
			// I get strange unicode string failures when using nvarchar(max). Not trying too hard to make this work, since
			// the MSSQL driver is a fringe thing right now for one or two special cases at IMQS.
			s += "nvarchar(4000)";
		}
		break;
	case Type::Date: s += "datetime2"; return;
	case Type::Guid: s += "uniqueidentifier"; return;
	case Type::Bin:
		if (width_or_srid == -1)
			s += "varbinary(max)";
		else if (width_or_srid != 0)
			s += tsf::fmt("varbinary(%v)", width_or_srid);
		else
			s += "varbinary(8000)";
		return;
	default:
		return SqlDialect::FormatType(s, type, width_or_srid, flags);
	}
}

void MSSQLDialect::AddLimit(SqlStr& s, const std::vector<std::string>& fields, int64_t limitCount, int64_t limitOffset, const std::vector<std::string>& orderBy) {
	// This only works for MS SQL Server 2012+
	if (orderBy.size() != 0) {
		s += "\nORDER BY ";
		for (const auto& order : orderBy)
			s.Fmt("%Q,", order);
		s.Chop();
		s.Fmt(" ASC");
	}
	s.Fmt("\n OFFSET %v ROWS FETCH NEXT %v ROWS ONLY", limitOffset, limitCount);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

MSSQLRows::MSSQLRows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con) : SqlApiRows(sql, type, stmtCmd, con) {
}

Error MSSQLRows::Columns(std::vector<ColumnInfo>& cols) {
	auto err = SqlApiRows::Columns(cols);
	if (!err.OK())
		return err;
	for (auto& c : cols) {
		if (c.Name == SpecialWKBFieldName)
			c.Type = Type::GeomAny;
	}

	return Error();
}

Error MSSQLRows::Get(size_t col, Attrib& val, Allocator* alloc) {
	try {
		SAField& v = Cmd->Field((int) col + 1);
		if (v.isNull()) {
			val.SetNull();
			return Error();
		}
		switch (v.FieldType()) {
		case SA_dtBLob:
			if (v.Name() == SpecialWKBFieldName) {
				auto err = WKB::Decode(v.asBLob(), v.asBLob().GetBinaryLength(), val, alloc);
				if (!err.OK())
					return err;
				val.Value.Geom.Head->SRID = 4326; // we assume 4326 in various places inside this driver (eg PrintAdd4326)
				return err;
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

void MSSQLRows::FromAttrib(const Attrib& s, SAParam& d) {
	switch (s.Type) {
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
	case Type::GeomAny: {
		auto       wflags = WKB::WriterFlags::None;
		io::Buffer buf;
		size_t     len = WKB::Encode(wflags, s, buf);
		memcpy(d.setAsBLob().GetBinaryBuffer(len), buf.Buf, len);
		d.setAsBLob().ReleaseBinaryBuffer(len);
		break;
	}
	default:
		SqlApiRows::FromAttrib(s, d);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialect* MSSQLConn::Dialect() {
	return new MSSQLDialect();
}

Error MSSQLConn::NewRows(const char* sql, SACommand* cmd, SqlApiRows*& rowsOut) {
	rowsOut = new MSSQLRows(sql, SA_CmdSQLStmt, cmd, this);
	return Error();
}

Error MSSQLConn::ToError(const SAException& e) {
	return ToErrorMSSQL(e);
}

Error MSSQLConn::Begin() {
	auto err = SqlApiConn::Begin();
	if (!err.OK())
		return err;

	//DriverRows* rows;
	//err = Exec("SET TRANSACTION AUTOCOMMIT DDL OFF", 0, nullptr, rows);
	//delete rows;

	return err;
}

Error MSSQLConn::Commit() {
	try {
		SACon.Commit();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

Error MSSQLConn::Rollback() {
	try {
		SACon.Rollback();
		SACon.setAutoCommit(SA_AutoCommitOn);
		return Error();
	} catch (SAException& e) {
		return ToError(e);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

MSSQLDriver::MSSQLDriver() {
}

MSSQLDriver::~MSSQLDriver() {
}

Error MSSQLDriver::Open(const ConnDesc& desc, DriverConn*& con) {
	auto mcon = new MSSQLConn();
	//string dbStr = tsf::fmt("Driver=/usr/lib/libmsodbcsql-17.so;Server=legendqa.imqs.co.za");
	//string dbStr = tsf::fmt("Driver=/usr/lib/libmsodbcsql-17.so");
	//string dbStr = "DSN=dev005;DATABASE=unit_test_dba;UID=unit_test_user;PWD=unit_test_password"; // WORKS!!!!!!
	//string dbStr = "Server=dev005,1433;DATABASE=unit_test_dba;UID=unit_test_user;PWD=unit_test_password"; // WORKS!!!!!!

	// This works mostly, but it fails when there is an @ in the password, because ssNcliClient.cpp mistakes that for server@database.
	//string dbStr = tsf::fmt("Server=%v,%v;DATABASE=%v;UID=%v;PWD=%v", desc.Host, desc.Port, desc.Database, desc.Username, desc.Password);
	// So, instead we force the server@database to occur early on.
	string dbStr = tsf::fmt("%v,%v@%v;UID=%v;PWD=%v", desc.Host, desc.Port, desc.Database, desc.Username, desc.Password);

	auto err = OpenInternal(SA_SQLServer_Client, dbStr, desc, mcon);
	if (!err.OK()) {
		delete mcon;
		return err;
	}
	con = mcon;
	return Error();
}

SqlDialect* MSSQLDriver::DefaultDialect() {
	return &StaticDialect;
}
} // namespace dba
} // namespace imqs
#endif