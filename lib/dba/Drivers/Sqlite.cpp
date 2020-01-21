#include "pch.h"
#include "Sqlite.h"
#include "SqliteSchema.h"
#include "../Attrib.h"
#include "../AttribGeom.h"

namespace imqs {
namespace dba {

Error GetSqliteErr(sqlite3* s3) {
	const char* msg = sqlite3_errmsg(s3);
	if (strstr(msg, "no such table:")) {
		return Error(std::string(ErrStubTableNotFound) + (msg + 13));
	} else if (strstr(msg, "has no column named")) {
		return Error(std::string(ErrStubFieldNotFound) + ": " + msg);
	} else if (strstr(msg, "UNIQUE constraint failed")) {
		return Error(std::string(ErrStubKeyViolation) + (msg + 24));
	} else if (strstr(msg, "database is locked")) {
		return Error(std::string(ErrStubDatabaseBusy) + ": " + msg);
	}
	return Error(msg);
}

enum Subtypes {
	SubtypeAsGeom  = 1,
	SubtypeAsGUID  = 2,
	SubtypeAsInt32 = 3,
	SubtypeMask    = 15, // reserve 15 slots, out of 255. Sqlite only preserves the lower 8 bits for you.
};

// This custom function allows us to inform our query API that the resulting BLOB is actually WKB,
// so that we can return the Attrib as a geometry object instead of a BLOB
static void CustomFunc_dba_ST_AsGeom(sqlite3_context* s3, int argc, sqlite3_value** argv) {
	sqlite3_result_value(s3, argv[0]);
	sqlite3_result_subtype(s3, SubtypeAsGeom);
}
static void CustomFunc_dba_AsGUID(sqlite3_context* s3, int argc, sqlite3_value** argv) {
	// This doesn't work, and I have no idea why not. The method works for CustomFunc_dba_ST_AsGeom.
	sqlite3_result_value(s3, argv[0]);
	sqlite3_result_subtype(s3, SubtypeAsGUID);
}
static void CustomFunc_dba_AsInt32(sqlite3_context* s3, int argc, sqlite3_value** argv) {
	sqlite3_result_value(s3, argv[0]);
	sqlite3_result_subtype(s3, SubtypeAsInt32);
}

SqliteRows::SqliteRows(DriverConn* dcon, sqlite3_stmt* statement, bool isDone) : DriverRows(dcon), Stmt(statement), IsDone(isDone) {
}

SqliteRows::~SqliteRows() {
}

Error SqliteRows::NextRow() {
	// We have already stepped the first row in SqliteStmt::Exec
	if (!FirstRowStepped && !IsDone) {
		FirstRowStepped = true;
		return Error();
	}

	auto res = sqlite3_step(Stmt);
	switch (res) {
	case SQLITE_ROW: return Error();
	case SQLITE_DONE: return ErrEOF;
	default: return GetSqliteErr(DBConn()->HDB);
	}
}

Error SqliteRows::Get(size_t col, Attrib& val, Allocator* alloc) {
	int ct = sqlite3_column_type(Stmt, (int) col);

	switch (ct) {
	case SQLITE_INTEGER: {
		sqlite3_value* sval    = sqlite3_column_value(Stmt, (int) col);
		int            subtype = sqlite3_value_subtype(sval) & SubtypeMask;
		if (subtype == SubtypeAsInt32)
			val.SetInt32(sqlite3_column_int(Stmt, (int) col));
		else
			val.SetInt64((int64_t) sqlite3_column_int64(Stmt, (int) col));
		break;
	}
	case SQLITE_FLOAT:
		val.SetDouble(sqlite3_column_double(Stmt, (int) col));
		break;
	case SQLITE_BLOB: {
		sqlite3_value* sval     = sqlite3_column_value(Stmt, (int) col);
		int            subtype  = sqlite3_value_subtype(sval) & SubtypeMask;
		const void*    blob     = sqlite3_column_blob(Stmt, (int) col);
		size_t         blobSize = sqlite3_column_bytes(Stmt, (int) col);
		if (subtype == SubtypeAsGeom) {
			auto err = WKB::Decode(blob, blobSize, val, alloc);
			if (!err.OK())
				val.SetNull();
		} else if (subtype == SubtypeAsGUID) {
			if (blobSize == 16)
				val.SetGuid(Guid::FromBytes(blob), alloc);
			else
				val.SetNull();
		} else {
			val.SetBin(blob, blobSize, alloc);
		}
		break;
	}
	case SQLITE_NULL:
		val.SetNull();
		break;
	case SQLITE_TEXT: {
		sqlite3_value* sval    = sqlite3_column_value(Stmt, (int) col);
		int            subtype = sqlite3_value_subtype(sval) & SubtypeMask;
		const char*    txt     = (const char*) sqlite3_column_text(Stmt, (int) col);
		size_t         size    = (size_t) sqlite3_column_bytes(Stmt, (int) col);
		if (subtype == SubtypeAsGUID) {
			Guid g = Guid::Null();
			if (size == 36)
				g = Guid::FromString(txt);

			if (g.IsNull())
				val.SetNull();
			else
				val = g;
		} else {
			val.SetText(txt, size, alloc);
		}
		break;
	}
	default:
		IMQS_DIE_MSG("Unrecognized native type in SqliteRows::Get");
	}
	return Error();
}

Error SqliteRows::Columns(std::vector<ColumnInfo>& cols) {
	int n = sqlite3_column_count(Stmt);
	for (int i = 0; i < n; i++) {
		ColumnInfo inf;
		inf.Name = sqlite3_column_name(Stmt, i);
		inf.Type = FromSqliteType(sqlite3_column_type(Stmt, i));
		cols.push_back(std::move(inf));
	}
	return Error();
}

size_t SqliteRows::ColumnCount() {
	return sqlite3_column_count(Stmt);
}

Type SqliteRows::FromSqliteType(int t) {
	switch (t) {
	case SQLITE_INTEGER: return Type::Int64;
	case SQLITE_FLOAT: return Type::Double;
	case SQLITE_BLOB: return Type::Bin;
	case SQLITE_NULL: return Type::Null;
	case SQLITE_TEXT: return Type::Text;
	}
	return Type::Null;
}

SqliteConn* SqliteRows::DBConn() {
	return static_cast<SqliteConn*>(DCon);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqliteStmt::SqliteStmt(SqliteConn* dcon, sqlite3_stmt* pStatement) : DriverStmt(dcon), Stmt(pStatement) {
}

SqliteStmt::~SqliteStmt() {
	sqlite3_finalize(Stmt);
}

Error SqliteStmt::Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	int err;

	if (ValuesBound) {
		if (nParams != BoundParams)
			return Error("Number of attributes to bind is invalid");

		err = sqlite3_reset(Stmt);
		if (err != SQLITE_OK)
			return GetSqliteErr(DBConn()->HDB);

		ValuesBound = false;
	} else {
		BoundParams = nParams;
	}

	for (int i = 0; i < nParams; i++) {
		const Attrib* p = params[i];

		switch (p->Type) {
		case Type::Null:
			err = sqlite3_bind_null(Stmt, i + 1);
			break;
		case Type::Bool:
			err = sqlite3_bind_int(Stmt, i + 1, p->Value.Bool ? 1 : 0);
			break;
		case Type::Int16:
			err = sqlite3_bind_int(Stmt, i + 1, p->Value.Int16);
			break;
		case Type::Int32:
			err = sqlite3_bind_int(Stmt, i + 1, p->Value.Int32);
			break;
		case Type::Int64:
			err = sqlite3_bind_int64(Stmt, i + 1, (sqlite3_int64) p->Value.Int64);
			break;
		case Type::Float:
			err = sqlite3_bind_double(Stmt, i + 1, p->Value.Float);
			break;
		case Type::Double:
			err = sqlite3_bind_double(Stmt, i + 1, p->Value.Double);
			break;
		case Type::Text:
			err = sqlite3_bind_text64(Stmt, i + 1, (const char*) p->Value.Text.Data, (sqlite3_uint64) p->Value.Text.Size, SQLITE_STATIC, SQLITE_UTF8);
			break;
		case Type::Guid:
			err = sqlite3_bind_blob(Stmt, i + 1, p->Value.Guid, 16, SQLITE_STATIC);
			break;
		case Type::Date: {
			char   dateStr[64]; // better way?
			size_t len = p->Date().Format8601(dateStr);
			err        = sqlite3_bind_text64(Stmt, i + 1, dateStr, (sqlite3_uint64) len, SQLITE_TRANSIENT, SQLITE_UTF8);
			break;
		}
		// case Type::Time:
		case Type::Bin:
			// sqlite3_bind_blob will behave like sqlite3_bind_null when 3rd argument is null, which we don't want.
			err = sqlite3_bind_blob(Stmt, i + 1, p->Value.Bin.Data == nullptr ? (const void*) (1) : p->Value.Bin.Data, p->Value.Bin.Size, SQLITE_STATIC);
			break;
		case Type::GeomPoint:
		case Type::GeomMultiPoint:
		case Type::GeomPolyline:
		case Type::GeomPolygon: {
			auto wflags = WKB::WriterFlags::EWKB | WKB::WriterFlags::Force_Multi;
			if (p->Value.Geom.Head->SRID != 0)
				wflags |= WKB::WriterFlags::SRID;
			if (p->GeomHasZ())
				wflags |= WKB::WriterFlags::Z;
			if (p->GeomHasM())
				wflags |= WKB::WriterFlags::M;
			io::Buffer buf;
			size_t     len = WKB::Encode(wflags, *p, buf);
			err            = sqlite3_bind_blob(Stmt, i + 1, buf.Buf, (int) len, SQLITE_TRANSIENT);
			break;
		}
		default:
			IMQS_DIE_MSG("Unhandled parameter type");
			break;
		}

		if (err != SQLITE_OK)
			return GetSqliteErr(DBConn()->HDB);
	}

	err = sqlite3_step(Stmt);

	switch (err) {
	case SQLITE_ROW:
	case SQLITE_DONE:
		ValuesBound = true;
		rowsOut     = new SqliteRows(DBConn(), Stmt, err == SQLITE_DONE);
		return Error();
	default:
		return GetSqliteErr(DBConn()->HDB);
	}
}

SqliteConn* SqliteStmt::DBConn() {
	return static_cast<SqliteConn*>(DCon);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialectFlags SqliteDialect::Flags() {
	return SqlDialectFlags::AlterSchemaInsideTransaction;
}

dba::Syntax SqliteDialect::Syntax() {
	return Syntax::ANSI;
}

void SqliteDialect::NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) {
	// We support dba_ST_AsGeom, dba_AsGUID, dba_AsINT32, so just pass them through here
	s += ast.FuncName;
}

void SqliteDialect::NativeHexLiteral(const char* hexLiteral, SqlStr& s) {
	s += tsf::fmt("X'%v'", hexLiteral);
}

bool SqliteDialect::UseThisCall(const char* funcName) {
	return false;
}

void SqliteDialect::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	if (!!(flags & TypeFlags::AutoIncrement)) {
		s += "INTEGER PRIMARY KEY AUTOINCREMENT";
	} else {
		SqlDialect::FormatType(s, type, width_or_srid, flags);
	}
}

void SqliteDialect::WriteValue(const Attrib& val, SqlStr& s) {
	switch (val.Type) {
	case Type::Bool:
		s += val.Value.Bool ? "1" : "0";
		break;
	default:
		SqlDialect::WriteValue(val, s);
	}
}

void SqliteDialect::TruncateTable(SqlStr& s, const std::string& table, bool resetSequences) {
	s.Fmt("DELETE FROM %Q", table);
	if (resetSequences)
		s.Fmt(";\nDELETE FROM sqlite_sequence WHERE name=%Q", table);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqliteConn::~SqliteConn() {
	Close();
}

Error SqliteConn::Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) {
	sqlite3_stmt* s;
	auto          res = sqlite3_prepare_v2(HDB, sql, (int) strlen(sql), &s, nullptr);

	if (res != SQLITE_OK)
		return GetSqliteErr(HDB);

	SqliteStmt* sstmt = new SqliteStmt(this, s);
	stmt              = sstmt;

	return Error();
}

Error SqliteConn::Begin() {
	return Exec("BEGIN");
}

Error SqliteConn::Commit() {
	return Exec("COMMIT");
}

Error SqliteConn::Rollback() {
	return Exec("ROLLBACK");
}

SqlDialect* SqliteConn::Dialect() {
	return new SqliteDialect();
}

Error SqliteConn::Connect(const ConnDesc& desc) {
	int  flags  = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
	auto status = sqlite3_open_v2(desc.Database.c_str(), &HDB, flags, nullptr);
	if (status != SQLITE_OK) {
		Close();
		return GetSqliteErr(HDB);
	}
	sqlite3_create_function(HDB, "dba_ST_AsGeom", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, CustomFunc_dba_ST_AsGeom, nullptr, nullptr);
	sqlite3_create_function(HDB, "dba_AsGUID", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, CustomFunc_dba_AsGUID, nullptr, nullptr);
	sqlite3_create_function(HDB, "dba_AsInt32", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, nullptr, CustomFunc_dba_AsInt32, nullptr, nullptr);
	return Error();
}

void SqliteConn::Close() {
	if (HDB != nullptr) {
		sqlite3_close(HDB);
		HDB = nullptr;
	}
}

Error SqliteConn::Exec(const char* sql) {
	char* err = nullptr;
	if (sqlite3_exec(HDB, sql, nullptr, nullptr, &err) != SQLITE_OK)
		return GetSqliteErr(HDB);

	auto res = Error();
	if (err != nullptr) {
		res = Error(err);
		sqlite3_free(err);
	}
	return res;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

SqliteDriver::SqliteDriver() {
}

SqliteDriver::~SqliteDriver() {
}

Error SqliteDriver::Open(const ConnDesc& desc, DriverConn*& con) {
	auto mycon = new SqliteConn();
	auto err   = mycon->Connect(desc);
	if (!err.OK()) {
		delete mycon;
		return err;
	}
	con = mycon;
	return Error();
}

SchemaReader* SqliteDriver::SchemaReader() {
	return &SqSchemaReader;
}

SchemaWriter* SqliteDriver::SchemaWriter() {
	return &SqSchemaWriter;
}

SqlDialect* SqliteDriver::DefaultDialect() {
	return &StaticDialect;
}
} // namespace dba
} // namespace imqs
