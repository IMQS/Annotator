#pragma once
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "Driver.h"
// I have given up on using SA_UNICODE for two reasons:
// 1. I couldn't get Unicode text to work in MSSQL from Linux, regardless of what modes I tried
// 2. Definining SA_UNICODE causes our Windows compilation to fail, because we don't use _UNICODE.
//#define SA_UNICODE
#include <third_party/SQLAPI/include/SQLAPI.h>

/*
SqlApiBase contains common code that can be shared between different drivers
that are all based on SQLAPI. SQLAPI is, in itself, a database abstraction
library, so we're layering turtles on top of each other here, which is not
always ideal, but it allows us to get something out the door quickly,
which is why we're using it for our HANA driver.

The design of SqlApi is such as that every SACommand is actually a prepared
statement. This is contrary to dba's design, which allows you to execute
a statement directly against the DB, without creating a 'command' object.
Because of this, there is some code duplication inside SqliApiBase.cpp
*/

namespace imqs {
namespace dba {

class SqlApiRows;
class SqlApiConn;
class SqlApiDriver;

class SqlApiRows : public DriverRows {
public:
	SACommand* Cmd    = nullptr;
	bool       IsStmt = false;

	// If sql is not null, then this is a once-off statement, where SACommand is created and owned by SqlApiRows
	// If stmtCmd is not null, then this is a prepared statement, where SACommand is created outside of the SqlApiRows
	// Only one of 'sql' or 'stmtCmd' will ever be populated. The other one will always be null.
	SqlApiRows(const char* sql, SACommandType_t type, SACommand* stmtCmd, SqlApiConn* con);
	~SqlApiRows() override;

	Error  NextRow() override;
	Error  Get(size_t col, Attrib& val, Allocator* alloc) override;
	Error  Columns(std::vector<ColumnInfo>& cols) override;
	size_t ColumnCount() override;

	// You can override this to provide special functionality
	virtual void FromAttrib(const Attrib& s, SAParam& d);

	SqlApiConn* MyConn();
};

class SqlApiStmt : public DriverStmt {
public:
	SACommand* Cmd = nullptr;

	SqlApiStmt(DriverConn* dcon);
	~SqlApiStmt() override;

	Error Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) override;

	SqlApiConn* MyConn();
};

class SqlApiConn : public DriverConn {
public:
	SAConnection SACon;

	Error Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut) override;
	Error Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) override;
	Error Begin() override;
	Error Commit() override;
	Error Rollback() override;

	// Either sql or cmd is specified, but never both.
	// For a prepared statement, cmd is populated, and sql is null.
	// For a directly executed statement, cmd is null, and sql is populated.
	virtual Error NewRows(const char* sql, SACommand* cmd, SqlApiRows*& rowsOut) = 0;
	virtual Error ToError(const SAException& e)                                  = 0;
};

class SqlApiDriver : public Driver {
public:
	SqlApiDriver();
	~SqlApiDriver();
	Error OpenInternal(SAClient_t client, std::string dbString, const ConnDesc& desc, SqlApiConn* con);
};

} // namespace dba
} // namespace imqs
#endif