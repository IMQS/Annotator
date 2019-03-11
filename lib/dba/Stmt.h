#pragma once

namespace imqs {
namespace dba {

class DriverStmt;
class Rows;
class Attrib;

// A prepared statement.
// Create a prepared statement by calling Conn::Prepare().
// Prepared statements may only be used by a single thread.
// To close a statement early (ie before destruction), call Close()
class IMQS_DBA_API Stmt {
public:
	friend class Conn;
	friend class Tx;

	static const int MaxParams = 999; // Maximum number of prepared statement parameters. This is $1 ... $999

	Stmt();
	~Stmt();
	void Close(); // The destructor will call Close() if you haven't already done so

	Error Exec(std::initializer_list<Attrib> params = {}); // Avoid this for performance sensitive code, due to Attrib copies into initializer_list
	Error Exec(size_t nParams, const Attrib** params);
	Rows  Query(size_t nParams, const Attrib** params);
	Rows  Query(std::initializer_list<Attrib> params = {}); // Avoid this for performance sensitive code, due to Attrib copies into initializer_list

private:
	DriverStmt* DStmt = nullptr;
	void        Bind(DriverStmt* dstmt);
};
} // namespace dba
} // namespace imqs