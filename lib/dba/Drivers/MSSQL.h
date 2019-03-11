#pragma once
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "Driver.h"
#include "SqlApiBase.h"
#include "../Allocators.h"

namespace imqs {
namespace dba {

class MSSQLDialect : public SqlDialect {
public:
	SqlDialectFlags   Flags() override;
	imqs::dba::Syntax Syntax() override;
	void              NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) override;
	void              NativeHexLiteral(const char* hexLiteral, SqlStr& s) override;
	bool              UseThisCall(const char* funcName) override;
	void              FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) override;
	void              AddLimit(SqlStr& s, const std::vector<std::string>& fields, int64_t limitCount, int64_t limitOffset, const std::vector<std::string>& orderBy) override;
};

class MSSQLRows : public SqlApiRows {
public:
	MSSQLRows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con);

	Error Columns(std::vector<ColumnInfo>& cols) override;
	Error Get(size_t col, Attrib& val, Allocator* alloc) override;
	void  FromAttrib(const Attrib& s, SAParam& d) override;
};

class MSSQLConn : public SqlApiConn {
public:
	SqlDialect* Dialect() override;
	Error       Begin() override;
	Error       Commit() override;
	Error       Rollback() override;

	Error NewRows(const char* sql, SACommand* cmd, SqlApiRows*& rowsOut) override;
	Error ToError(const SAException& e) override;
};

class MSSQLDriver : public SqlApiDriver {
public:
	MSSQLDialect StaticDialect;

	MSSQLDriver();
	~MSSQLDriver();
	Error       Open(const ConnDesc& desc, DriverConn*& con) override;
	SqlDialect* DefaultDialect() override;
};
} // namespace dba
} // namespace imqs
#endif