#pragma once
#include "Driver.h"
#include <sqlite/sqlite3.h>
#include "SqliteSchema.h"

/*
SQLite driver

Issues:

* The SRID of a geometry columns is read out of geometry_columns (and spatial_ref_sys) by the SqliteSchemaReader,
 but if you're reading geometry with SELECT dba_ST_AsGeom(Geometry), then the Attrib you get out will likely have
 a SRID = 0, because at this phase, the query system doesn't know the association with that field.
 The burden is unfortunately pushed out to the caller to merge up the SRID from the schema with the SRID from
 the geometry columns.

*/

namespace imqs {
namespace dba {

class SqliteConn;

class SqliteRows : public DriverRows {
public:
	SqliteRows(DriverConn* dcon, sqlite3_stmt* statement, bool isDone);
	~SqliteRows() override;

	Error  NextRow() override;
	Error  Get(size_t col, Attrib& val, Allocator* alloc) override;
	Error  Columns(std::vector<ColumnInfo>& cols) override;
	size_t ColumnCount() override;

	Type        FromSqliteType(int t);
	SqliteConn* DBConn();

private:
	sqlite3_stmt* Stmt            = nullptr;
	bool          FirstRowStepped = false;
	bool          IsDone          = false; // We got SQLITE_DONE on first exec: No rows available.
};

class SqliteStmt : public DriverStmt {
public:
	sqlite3_stmt* Stmt = nullptr;

	SqliteStmt(SqliteConn* dcon, sqlite3_stmt* pStatement);
	~SqliteStmt() override;

	Error Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) override;

	SqliteConn* DBConn();

private:
	bool   ValuesBound = false;
	size_t BoundParams = 0;
};

class SqliteDialect : public SqlDialect {
public:
	SqlDialectFlags Flags() override;
	dba::Syntax     Syntax() override;
	void            NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) override;
	void            NativeHexLiteral(const char* hexLiteral, SqlStr& s) override;
	bool            UseThisCall(const char* funcName) override;
	void            FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) override;
	void            WriteValue(const Attrib& val, SqlStr& s) override;
	void            TruncateTable(SqlStr& s, const std::string& table, bool resetSequences) override;
};

class SqliteConn : public DriverConn {
public:
	sqlite3* HDB = nullptr;

	~SqliteConn() override;

	Error       Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) override;
	Error       Begin() override;
	Error       Commit() override;
	Error       Rollback() override;
	SqlDialect* Dialect() override;

	Error Connect(const ConnDesc& desc);
	void  Close();
	Error Exec(const char* sql);
};

class SqliteDriver : public Driver {
public:
	SqliteSchemaReader SqSchemaReader;
	SqliteSchemaWriter SqSchemaWriter;
	SqliteDialect      StaticDialect;

	SqliteDriver();
	~SqliteDriver();

	Error                    Open(const ConnDesc& desc, DriverConn*& con) override;
	imqs::dba::SchemaReader* SchemaReader() override;
	imqs::dba::SchemaWriter* SchemaWriter() override;
	SqlDialect*              DefaultDialect() override;
};
} // namespace dba
} // namespace imqs
