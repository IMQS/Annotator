#pragma once

#include "Attrib.h"
#include "SqlStr.h"

namespace imqs {
namespace dba {
class Rows;
class Stmt;

// Interface that allows one to execute SQL statements and queries
// This is implemented by Tx and Conn, so that one can write functions
// that act either through implicit or explicit transactions.
// For documentation on how to query, see Conn
class IMQS_DBA_API Executor {
public:
	virtual SqlStr     Sql()     = 0;
	virtual dba::Conn* GetConn() = 0;

	virtual Error Exec(const char* sql, size_t nParams, const Attrib** params) = 0;
	virtual Error Exec(const char* sql, std::initializer_list<Attrib> params = {}); // Avoid this for performance sensitive code, due to Attrib copies into initializer_list

	virtual Rows Query(const char* sql, size_t nParams, const Attrib** params) = 0;
	virtual Rows Query(const char* sql, std::initializer_list<Attrib> params = {}); // Avoid this for performance sensitive code, due to Attrib copies into initializer_list

	virtual Error Prepare(const char* sql, size_t nParams, const Type* paramTypes, Stmt& stmt) = 0; // Create a new prepared statement
	virtual Error Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt);     // Create a new prepared statement
};
} // namespace dba
} // namespace imqs