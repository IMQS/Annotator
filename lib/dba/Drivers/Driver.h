#pragma once

#include "../SqlStr.h"
#include "SchemaReader.h"
#include "SchemaWriter.h"

namespace imqs {
namespace dba {

namespace sqlparser {
class SqlAST;
}

class Attrib;
class DriverConn;
class Driver;
class DriverStmt;
class Conn;
class ResultSink;
class Allocator;

// Information about a column that is returned from a SELECT statement
struct ColumnInfo {
	std::string Name;
	dba::Type   Type;
};

// Low level iterator over the resulting rows of a query
class IMQS_DBA_API DriverRows {
public:
	DriverRows(DriverConn* dcon);
	virtual ~DriverRows();

	virtual Error  NextRow()                                      = 0; // Returns ErrEOF when there are no more rows.
	virtual Error  Get(size_t col, Attrib& val, Allocator* alloc) = 0;
	virtual Error  Columns(std::vector<ColumnInfo>& cols)         = 0; // Consider getting rid of the error here, since neither Postgres nor Sqlite can return an error here.
	virtual size_t ColumnCount()                                  = 0; // I held out long before adding this. It is necessary for a lot of sanity checking code paths, where asking for the columns is too expensive.

protected:
	DriverConn* DCon;
};

// Low level prepared statement
class IMQS_DBA_API DriverStmt {
public:
	std::string              SQL;           // The SQL string of this statement
	const sqlparser::SqlAST* AST = nullptr; // Parsed AST of SQL (if our parser was able to parse the statement)

	DriverStmt(DriverConn* dcon);
	virtual ~DriverStmt();
	virtual Error Exec(size_t nParams, const Attrib** params, DriverRows*& rowsOut) = 0;

	void Initialize(const char* sql);

protected:
	DriverConn* DCon;
};

// Low level connection.
// One DriverConn connection must represent a low-level, actual connection to a database. For example,
// it might be a libpq DB handle, or an sqlite DB handle. The dba library will only allow a single thread to use
// one DriverConn at a time, so there is no need to worry about multithreaded access to a DriverConn object.
class IMQS_DBA_API DriverConn {
public:
	// Every new DriverConn gets a unique ID. This was added to make debugging easier, so that we
	// don't need to identify DriverConn objects by their pointers.
	uint64_t InternalID = 0;

	// Number of agents that are using this. For example, a Rows constructor will increment this,
	// and decrement it when it is destroyed.
	// This needs to be atomic, because the code that scans the cache of DriverConn objects inside
	// Conn can be running on a different thread to the code that is running, for example, the destructor
	// of a Rows or a Stmt object.
	// Do not alter this directly. Instead, use IncrementRefCount() and DecrementRefCount(). These functions
	// allow us to trace reference counting behaviour, in order to find bugs that arise from time
	// to time.
	std::atomic<int> RefCount;

	// The following two variables are used to artificially inject an error code after the given
	// number of calls to the DB.
	// This is used to stress failure paths inside unit tests.
	// The 'numGoodCallsBeforeFailure' number must be decremented on every call to the actual
	// DB protocol which might reasonably fail if the database or the network was to go down.
	// Examples of such calls are PQprepare and PQexec. When numGoodCallsBeforeFailure hits one,
	// that function must return 'failWithError'.
	// If FailAfter is zero, then behave as usual.
	size_t FailAfter = 0;
	Error  FailAfterWith;

	// This is used to implement assertions that verify that you're using transactions correctly.
	bool IsTxBusy = false;

	DriverConn();
	virtual ~DriverConn();
	virtual Error Prepare(const char* sql, size_t nParams, const Type* paramTypes, DriverStmt*& stmt) = 0;
	virtual Error Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut); // This is optional. If you don't provide it, dba will use Prepare to execute all statements.

	// Retrieve the SQL dialect used by this database.
	// One of these objects will be fetched the first time a new connection is made to this driver.
	// That object will remain in use until the last connection is closed. That one object will be
	// used on all subsequent connections in the same pool.
	virtual SqlDialect* Dialect() = 0;

	virtual Error Begin()    = 0; // Start a transaction.
	virtual Error Commit()   = 0; // Commit a transaction.
	virtual Error Rollback() = 0; // Rollback a transaction

	// Used to alter FailAfter
	void DecrementFailAfter();

	void IncrementRefCount(void* caller, const char* callerName);
	void DecrementRefCount(void* caller, const char* callerName);

private:
	// The following two are only used when IMQS_DEBUG_REF_COUNT is defined inside Driver.cpp
	std::mutex               RefCountLogLock;
	std::vector<std::string> RefCountLog;
};

/* A database driver
All functions on Driver must be thread safe.

Different databases support different feature sets (eg a GUID field, or M coordinates in geometry).
These flags are exposed through the SqlDialect interface, specifically through SqlDialectFlags.
*/
class IMQS_DBA_API Driver {
public:
	virtual ~Driver();
	virtual Error                    Open(const ConnDesc& desc, DriverConn*& con) = 0; // Open a new connection
	virtual imqs::dba::SchemaReader* SchemaReader();                                   // Provide a schema reader. Do not delete the reader. Default implementation returns null.
	virtual imqs::dba::SchemaWriter* SchemaWriter();                                   // Provide a schema writer. Do not delete the writer. Default implementation returns null.

	// Provide a default dialect.
	// Note that in many cases this will be just as useful as the Dialect() returned by a DriverConn object.
	// The only reason we make this distinction, is because sometimes there are specific details that
	// one can only discover once you've made a connection to a database. Historic examples of this are
	// the SQL escaping syntax for Postgres, or the internal storage representation of a TIMESTAMP object.
	// In practice, those are no longer issues, because we do not support connecting to such old databases,
	// but we encourage the use of the DriverConn::Dialect() rather than this one, should such issues
	// arise in future. This interface was added so that the IMQS CrudServer could expose an SQL translator
	// as an API, without needing a dummy Postgres server to connect to.
	virtual SqlDialect* DefaultDialect() = 0;
};
} // namespace dba
} // namespace imqs
