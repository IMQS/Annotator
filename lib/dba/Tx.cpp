#include "pch.h"
#include "Tx.h"
#include "Drivers/Driver.h"
#include "Attrib.h"
#include "Rows.h"
#include "Stmt.h"
#include "Conn.h"

namespace imqs {
namespace dba {

Tx::Tx(dba::Conn* con, DriverConn* dcon) : Con(con), DCon(dcon) {
	dcon->IncrementRefCount(this, "Tx");
}

Tx::~Tx() {
	DCon->DecrementRefCount(this, "Tx");
}

Error Tx::Commit() {
	if (AutoCloser != nullptr)
		AutoCloser->TX = nullptr;
	IMQS_ASSERT(DCon->IsTxBusy);
	DCon->IsTxBusy = false;
	auto err       = DCon->Commit();
	delete this;
	return err;
}

Error Tx::Rollback() {
	if (AutoCloser != nullptr)
		AutoCloser->TX = nullptr;
	IMQS_ASSERT(DCon->IsTxBusy);
	DCon->IsTxBusy = false;
	auto err       = DCon->Rollback();
	delete this;
	return err;
}

SqlStr Tx::Sql() {
	return Con->Sql();
}

Error Tx::Exec(const char* sql, size_t nParams, const Attrib** params) {
	return Query(sql, nParams, params).Err();
}

Rows Tx::Query(const char* sql, size_t nParams, const Attrib** params) {
	smallvec<dba::Type> pTypes;
	pTypes.resize(nParams);
	for (size_t i = 0; i < nParams; i++)
		pTypes[i] = params[i] ? params[i]->Type : Type::Null;

	Rows        rows;
	DriverStmt* stmt   = nullptr;
	rows.DeadWithError = DCon->Prepare(sql, nParams, &pTypes[0], stmt);
	if (!rows.DeadWithError.OK())
		return rows;
	stmt->SQL = sql;

	DriverRows* drows  = nullptr;
	rows.DeadWithError = stmt->Exec(nParams, params, drows);
	if (rows.DeadWithError.OK())
		rows.Reset(drows, stmt);
	else
		delete stmt;
	return rows;
}

Error Tx::Prepare(const char* sql, size_t nParams, const Type* paramTypes, Stmt& stmt) {
	stmt.Close();
	DriverStmt* dstmt = nullptr;
	auto        err   = DCon->Prepare(sql, nParams, paramTypes, dstmt);
	if (!err.OK())
		return err;
	stmt.DStmt = dstmt;
	return Error();
}

Error Tx::Exec(const char* sql, std::initializer_list<Attrib> params) {
	return Executor::Exec(sql, params);
}

Rows Tx::Query(const char* sql, std::initializer_list<Attrib> params) {
	return Executor::Query(sql, params);
}

Error Tx::Prepare(const char* sql, std::initializer_list<Type> paramTypes, Stmt& stmt) {
	return Executor::Prepare(sql, paramTypes, stmt);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////

TxAutoCloser::TxAutoCloser(Tx* tx) {
	if (tx)
		Bind(tx);
}

TxAutoCloser::~TxAutoCloser() {
	if (TX)
		TX->Rollback();
}

void TxAutoCloser::Bind(Tx* tx) {
	IMQS_ASSERT(TX == nullptr);
	TX = tx;
	IMQS_ASSERT(TX->AutoCloser == nullptr);
	TX->AutoCloser = this;
}

} // namespace dba
} // namespace imqs
