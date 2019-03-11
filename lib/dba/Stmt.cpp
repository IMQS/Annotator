#include "pch.h"
#include "Stmt.h"
#include "Attrib.h"
#include "Rows.h"
#include "Drivers/Driver.h"

namespace imqs {
namespace dba {

const int Stmt::MaxParams;

Stmt::Stmt() {
}

Stmt::~Stmt() {
	Close();
}

void Stmt::Close() {
	delete DStmt;
	DStmt = nullptr;
}

Error Stmt::Exec(std::initializer_list<Attrib> params) {
	smallvec<const Attrib*> ptr;
	for (size_t i = 0; i < params.size(); i++)
		ptr.push(params.begin() + i);
	return Exec(params.size(), &ptr[0]);
}

Error Stmt::Exec(size_t nParams, const Attrib** params) {
	DriverRows* rows = nullptr;
	auto        err  = DStmt->Exec(nParams, params, rows);
	delete rows;
	return err;
}

Rows Stmt::Query(size_t nParams, const Attrib** params) {
	Rows        rows;
	DriverRows* drows  = nullptr;
	rows.DeadWithError = DStmt->Exec(nParams, params, drows);
	if (!rows.DeadWithError.OK())
		IMQS_ASSERT(drows == nullptr);
	if (rows.DeadWithError.OK())
		rows.Reset(drows, nullptr);
	return rows;
}

Rows Stmt::Query(std::initializer_list<Attrib> params) {
	smallvec<const Attrib*> ptr;
	for (size_t i = 0; i < params.size(); i++)
		ptr.push(params.begin() + i);
	return Query(params.size(), &ptr[0]);
}

void Stmt::Bind(DriverStmt* dstmt) {
	Close();
	DStmt = dstmt;
}

} // namespace dba
} // namespace imqs