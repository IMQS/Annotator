#pragma once

#include "Executor.h"

namespace imqs {
namespace dba {

class Attrib;
class DriverRows;
class Stmt;
class Rows;
class TxAutoCloser;

// Transaction
//
// Start a transaction by calling Conn::Begin(). End a transaction by calling either
// Commit() or Rollback(). Those functions will delete the transaction object.
//
// For a lexical scope transaction helper, see TxAutoCloser
//
// Tx shares the same API as Conn. For documentation on how to issues queries etc, see Conn.
class IMQS_DBA_API Tx : public Executor {
public:
	friend class Conn;
	friend class TxAutoCloser;

	Error Commit();
	Error Rollback();

	SqlStr     Sql() override;
	dba::Conn* GetConn() override { return Con; }

	Error Exec(const char* sql, size_t nParams, const Attrib** params) override;
	Error Exec(const char* sql, std::initializer_list<Attrib> params = {}) override;
	Rows  Query(const char* sql, size_t nParams, const Attrib** params) override;
	Rows  Query(const char* sql, std::initializer_list<Attrib> params = {}) override;
	Error Prepare(const char* sql, size_t nParams, const Type* paramTypes, Stmt& stmt) override;
	Error Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt) override;

private:
	dba::Conn*    Con        = nullptr;
	DriverConn*   DCon       = nullptr;
	TxAutoCloser* AutoCloser = nullptr;

	Tx(dba::Conn* con, DriverConn* dcon);
	~Tx();
};

// A block-scoped auto transaction closer.
// If the destructor runs and neither Commit nor Rollback have been called, then Rollback is called.
// It is fine to pass a null Tx object to the constructor, in which case this class does nothing.
class IMQS_DBA_API TxAutoCloser {
public:
	friend class Tx;

	TxAutoCloser(Tx* tx);
	~TxAutoCloser();

	void Bind(Tx* tx);                             // For cases where you can't initialize the object with it's constructor (ie inside an 'if' statement)
	bool IsBound() const { return TX != nullptr; } // This will return false if the transaction has already been committed or rolled back

private:
	Tx* TX = nullptr;
};
} // namespace dba
} // namespace imqs
