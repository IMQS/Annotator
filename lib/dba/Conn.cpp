#include "pch.h"
#include "Conn.h"
#include "Stmt.h"
#include "Rows.h"
#include "Tx.h"
#include "Drivers/Driver.h"
#include "Attrib.h"
#include "SqlStr.h"
#include "SqlParser/Tools/InternalTranslator.h"

using namespace std;

namespace imqs {
namespace dba {

Conn::Conn() {
}

Conn::~Conn() {
	for (auto dcon : DriverConns) {
		// Somebody is leaking DB connections.
		// If you need to do a "fast exit", and threads are not responding, then just TerminateProcess() or the equivalent
		IMQS_ASSERT(dcon->RefCount == 0);
		delete dcon;
	}
	delete Dialect;
}

void Conn::Close() {
	Glob.ConnClose(this);
}

SqlStr Conn::Sql() {
	return SqlStr(Dialect);
}

Error Conn::Exec(const char* sql, size_t nParams, const Attrib** params) {
	return Query(sql, nParams, params).Err();
}

Rows Conn::Query(const char* sql, size_t nParams, const Attrib** params) {
	DriverConn* dcon  = nullptr;
	DriverRows* drows = nullptr;
	auto        err   = TryRestartableOperation([&](DriverConn* dcon) -> Error { return dcon->Exec(sql, nParams, params, drows); }, dcon);
	if (err == ErrUnsupported)
		return QueryWithStmt(sql, nParams, params);

	Rows rows;
	rows.DeadWithError = err;
	if (!err.OK())
		return rows;

	IMQS_ASSERT(dcon != nullptr); // hint for clang-analyze

	rows.Reset(drows, nullptr);
	ReleaseDriverConn(dcon);
	return rows;
}

// Fall back to querying via a prepared statement, if the driver doesn't support plain Exec()
Rows Conn::QueryWithStmt(const char* sql, size_t nParams, const Attrib** params) {
	Rows                rows;
	smallvec<dba::Type> pTypes;
	pTypes.resize(nParams);
	for (size_t i = 0; i < nParams; i++)
		pTypes[i] = params[i] ? params[i]->Type : Type::Null;

	DriverConn* dcon   = nullptr;
	DriverStmt* stmt   = nullptr;
	rows.DeadWithError = TryRestartableOperation([&](DriverConn* dcon) -> Error { return dcon->Prepare(sql, nParams, &pTypes[0], stmt); }, dcon);
	if (!rows.DeadWithError.OK())
		return rows;
	IMQS_ASSERT(dcon != nullptr); // hint for clang-analyze
	stmt->Initialize(sql);

	DriverRows* drows  = nullptr;
	rows.DeadWithError = stmt->Exec(nParams, params, drows);
	if (!rows.DeadWithError.OK()) {
		IMQS_ASSERT(drows == nullptr); // driver must not return a 'rows' if it returns an error.
		delete stmt;
		// Here we don't try to re-execute the command on a new connection, because we can't be
		// sure if the connection died before or after the statement was executed.
		if (rows.DeadWithError == ErrBadCon)
			ReleaseAndCloseDriverConn(dcon);
		else
			ReleaseDriverConn(dcon);
		return rows;
	}

	rows.Reset(drows, stmt);
	ReleaseDriverConn(dcon);
	return rows;
}

Error Conn::Prepare(const char* sql, size_t nParams, const Type* paramTypes, Stmt& stmt) {
	stmt.Close();
	DriverConn* dcon  = nullptr;
	DriverStmt* dstmt = nullptr;
	auto        err   = TryRestartableOperation([&](DriverConn* dcon) -> Error { return dcon->Prepare(sql, nParams, paramTypes, dstmt); }, dcon);
	if (!err.OK())
		return err;
	IMQS_ASSERT(dcon != nullptr); // hint for clang-analyze
	dstmt->Initialize(sql);
	stmt.DStmt = dstmt;
	ReleaseDriverConn(dcon);
	return Error();
}

Error Conn::Exec(const char* sql, std::initializer_list<Attrib> params) {
	return Executor::Exec(sql, params);
}

Rows Conn::Query(const char* sql, std::initializer_list<Attrib> params) {
	return Executor::Query(sql, params);
}

Error Conn::Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt) {
	return Executor::Prepare(sql, paramTypes, stmt);
}

Error Conn::Begin(Tx*& tx) {
	DriverConn* dcon = nullptr;
	auto        err  = TryRestartableOperation([&](DriverConn* dcon) -> Error { return dcon->Begin(); }, dcon);
	if (!err.OK())
		return err;
	IMQS_ASSERT(dcon != nullptr); // hint for clang-analyze
	IMQS_ASSERT(!dcon->IsTxBusy);
	dcon->IsTxBusy = true;
	tx             = new Tx(this, dcon);
	ReleaseDriverConn(dcon);
	return Error();
}

SchemaReader* Conn::SchemaReader() {
	return Driver->SchemaReader();
}

SchemaWriter* Conn::SchemaWriter() {
	return Driver->SchemaWriter();
}

void Conn::InjectError(size_t failAfter, Error failAfterWith) {
	if (DriverConns.size() == 0) {
		// ensure there is a dcon available
		DriverConn* dcon = nullptr;
		auto        err  = GetDriverConn(dcon);
		IMQS_ASSERT(err.OK());
		ReleaseDriverConn(dcon);
	}
	DriverConns[0]->FailAfter     = failAfter;
	DriverConns[0]->FailAfterWith = failAfterWith;
}

Error Conn::GetDriverConn(DriverConn*& dcon) {
	lock_guard<mutex> lock(DriverConnsLock);
	for (DriverConn* d : DriverConns) {
		// RefCount cannot progress from zero to any other state, while we have DriverConnsLock,
		// so we don't need to bother with a lock-free style acquisition here. RefCount CAN
		// progress from non-zero to zero on a different thread, but that doesn't interfere
		// with us here.
		if (d->RefCount == 0) {
			dcon = d;
			d->IncrementRefCount(this, "Conn::GetDriverConn");
			IMQS_ASSERT(d->RefCount == 1);
			return Error();
		}
	}
	DriverConn* d   = nullptr;
	auto        err = Driver->Open(ConnDesc, d);
	if (!err.OK())
		return err;
	DriverConns.push_back(d);
	dcon = d;
	d->IncrementRefCount(this, "Conn::GetDriverConn");
	IMQS_ASSERT(d->RefCount == 1);
	return Error();
}

void Conn::ReleaseDriverConn(DriverConn* dcon) {
	dcon->DecrementRefCount(this, "Conn::GetDriverConn");
}

void Conn::ReleaseAndCloseDriverConn(DriverConn* dcon) {
	dcon->DecrementRefCount(this, "Conn::GetDriverConn");

	// I'm not sure about this assumption. I have the feeling that there may be situations where one could end
	// up with Rows or Stmt objects still existing, which are holding a reference to this dcon. If you can
	// legitimately create such a scenario, then we need to remove this assertion, and rather create
	// a purgatory, where DriverConns can go while we wait for another thread to finish up with it's
	// Rows or Stmt objects. At present, I can't think of such a scenario, because all of the operations
	// that would cause ReleaseAndCloseDriverConn to occur, should be synchronous with whatever agent
	// is holding a Rows or Stmt object on that dcon.
	IMQS_ASSERT(dcon->RefCount == 0);

	lock_guard<mutex> lock(DriverConnsLock);
	auto              pos = find(DriverConns.begin(), DriverConns.end(), dcon);
	IMQS_ASSERT(pos != DriverConns.end());
	delete dcon;
	DriverConns.erase(pos);
}

// Many operations can be retried, if the first error that we get back is ErrBadCon.
// This utility function handles the boilerplate of the retry operation.
// If the operation succeeds, then dcon holds the relevant DriverConn.
Error Conn::TryRestartableOperation(std::function<Error(DriverConn* dcon)> restartable, DriverConn*& _dcon) {
	_dcon            = nullptr;
	DriverConn* dcon = nullptr;
	for (int pass = 0; pass < 2; pass++) {
		auto err = GetDriverConn(dcon);
		if (!err.OK())
			return err;
		IMQS_ASSERT(dcon != nullptr); // hint for clang-analyze
		err = restartable(dcon);
		if (err.OK()) {
			_dcon = dcon;
			return Error();
		}
		if (pass == 0 && err == ErrBadCon) {
			// Try again on a new connection
			ReleaseAndCloseDriverConn(dcon);
			continue;
		}
		ReleaseDriverConn(dcon);
		return err;
	}
	IMQS_DIE_MSG("TryRestartableOperation: this code is not supposed to be reachable");
	return Error();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

ConnAutoCloser::ConnAutoCloser(Conn* con) : Con(con) {
}

ConnAutoCloser::~ConnAutoCloser() {
	if (Con)
		Con->Close();
}

} // namespace dba
} // namespace imqs