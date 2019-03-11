#pragma once
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "SqlApiBase.h"
#include "HANASchema.h"

/*
HANA notes:

HANA's row-oriented tables don't support AUTOINCREMENT (IDENTITY) fields,
which is why we always create columnar tables.

HANA geometry types are split into two types: ST_POINT and ST_GEOMETRY.
ST_GEOMETRY holds everything that is not a point. For this reason, we
don't bother to try and classify HANA geometry fields into things like
linestring or polygon. We just always treat all geometry fields as our
GeomAny type.

*/

namespace imqs {
namespace dba {

class HANADialect : public SqlDialect {
public:
	SqlDialectFlags Flags() override;
	dba::Syntax     Syntax() override;
	void            FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) override;
	void            NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) override;
	void            NativeHexLiteral(const char* hexLiteral, SqlStr& s) override;
	bool            UseThisCall(const char* funcName) override;
	void            ST_GeomFrom(SqlStr& s, const char* insertElement) override;
};

class SqlApiHANARows : public SqlApiRows {
public:
	SqlApiHANARows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con);

	Error Get(size_t col, Attrib& val, Allocator* alloc) override;
};

class SqlApiHANAConn : public SqlApiConn {
public:
	SqlDialect* Dialect() override;
	Error       Begin() override;
	Error       Commit() override;
	Error       Rollback() override;

	Error NewRows(const char* sql, SACommand* cmd, SqlApiRows*& rowsOut) override;
	Error ToError(const SAException& e) override;
};

class SqlApiHANADriver : public SqlApiDriver {
public:
	//SAConnection     Con;
	HANADialect      StaticDialect;
	HANASchemaReader HSchemaReader;
	HANASchemaWriter HSchemaWriter;

	SqlApiHANADriver();
	~SqlApiHANADriver();

	Error                    Open(const ConnDesc& desc, DriverConn*& con) override;
	imqs::dba::SchemaReader* SchemaReader() override;
	imqs::dba::SchemaWriter* SchemaWriter() override;
	SqlDialect*              DefaultDialect() override;
};

} // namespace dba
} // namespace imqs
#endif