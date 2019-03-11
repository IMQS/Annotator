#pragma once

#include "ConnDesc.h"
#include "Executor.h"

namespace imqs {
namespace dbutil {
// This is a necessary forward declaration, because we make this a friend of Conn, so that it can dig inside.
class TestHelper;
} // namespace dbutil
} // namespace imqs

namespace imqs {
namespace dba {

class Rows;
class Attrib;
class Driver;
class Stmt;
class Tx;
class SchemaReader;
class SchemaWriter;
class ConnAutoCloser;

/* High level connection. Shared by many agents, on multiple threads.

How to Query:

	auto rows = con->Query(sql);
	for (auto row : rows) {
		int val1, val2;
		row.Scan(val1, val2);        // consume first two columns

		// or...

		int val1 = row[0].ToInt32(); // consume first result column
		int val2 = row[1].ToInt32(); // consume second result column
	}

	// handle errors
	if (!rows.OK())
		return rows.Err();

*/
class IMQS_DBA_API Conn : public Executor {
public:
	friend class Global;
	friend class Tx;
	friend class imqs::dbutil::TestHelper; // TestHelper lives inside dbutil
	friend class ConnAutoCloser;

	void            Close();
	const ConnDesc& Connection() const { return ConnDesc; }

	SqlStr     Sql() override;
	dba::Conn* GetConn() override { return this; }

	Error Exec(const char* sql, size_t nParams, const Attrib** params) override;
	Error Exec(const char* sql, std::initializer_list<Attrib> params = {}) override;
	Rows  Query(const char* sql, size_t nParams, const Attrib** params) override;
	Rows  Query(const char* sql, std::initializer_list<Attrib> params = {}) override;
	Error Prepare(const char* sql, size_t nParams, const Type* paramTypes, Stmt& stmt) override;
	Error Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt) override;

	// Start a transaction.
	Error Begin(Tx*& tx);

	// Return a schema reader, if the driver supports that. Returns null if the driver doesn't support it. Do not delete this object - it is owned by the connection.
	SchemaReader* SchemaReader();

	// Return a schema writer, if the driver supports that. Returns null if the driver doesn't support it. Do not delete this object - it is owned by the connection.
	SchemaWriter* SchemaWriter();

private:
	int                      RefCount = 0; // How many times this Conn object has been 'opened' via Global.Open()
	ConnDesc                 ConnDesc;
	Driver*                  Driver  = nullptr;
	SqlDialect*              Dialect = nullptr;
	std::mutex               DriverConnsLock; // Guards access to DriverConns
	std::vector<DriverConn*> DriverConns;     // Underlying, actual connections to a DB

	// Constructor and destructor are private, so that only Global can create/destroy a Conn object
	Conn();
	~Conn();
	Error GetDriverConn(DriverConn*& dcon);
	void  ReleaseDriverConn(DriverConn* dcon);
	void  ReleaseAndCloseDriverConn(DriverConn* dcon);
	Rows  QueryWithStmt(const char* sql, size_t nParams, const Attrib** params);
	Error TryRestartableOperation(std::function<Error(DriverConn* dcon)> restartable, DriverConn*& _dcon);

	void InjectError(size_t failAfter, Error failAfterWith); // Exposed for unit tests. Affects only the first driver in the pool. See DriverConn for details.
};

// A block-scoped auto connection closer.
class IMQS_DBA_API ConnAutoCloser {
public:
	friend class Conn;

	ConnAutoCloser(Conn* con);
	~ConnAutoCloser();

private:
	Conn* Con;
};

} // namespace dba
} // namespace imqs
