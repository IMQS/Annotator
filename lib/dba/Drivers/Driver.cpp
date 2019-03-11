#include "pch.h"
#include "Postgres.h"
#include "../Conn.h"
#include "../Global.h"
#include "../SqlParser/Tools/ASTCache.h"

namespace imqs {
namespace dba {

static uint64_t NextDriverConnInternalID = 1;

DriverRows::DriverRows(DriverConn* dcon) : DCon(dcon) {
	DCon->IncrementRefCount(this, "DriverRows");
}

DriverRows::~DriverRows() {
	DCon->DecrementRefCount(this, "DriverRows");
}

DriverStmt::DriverStmt(DriverConn* dcon) : DCon(dcon) {
	DCon->IncrementRefCount(this, "DriverStmt");
}

DriverStmt::~DriverStmt() {
	if (AST != nullptr)
		Glob.ASTCache->ReleaseAST(AST);
	DCon->DecrementRefCount(this, "DriverStmt");
}

void DriverStmt::Initialize(const char* sql) {
	SQL = sql;
	std::string parseError;
	AST = Glob.ASTCache->GetAST(sql, parseError);
}

DriverConn::DriverConn() {
	InternalID = NextDriverConnInternalID++;
	RefCount   = 0;
}

DriverConn::~DriverConn() {
}

void DriverConn::DecrementFailAfter() {
	if (FailAfter != 0)
		FailAfter--;
}

Error DriverConn::Exec(const char* sql, size_t nParams, const Attrib** params, DriverRows*& rowsOut) {
	return ErrUnsupported;
}

// Uncomment this in order to get data in RefCountLog, which is very useful for debugging
// reference counting issues.
// #define IMQS_DEBUG_REF_COUNT

void DriverConn::IncrementRefCount(void* caller, const char* callerName) {
	RefCount++;

#ifdef IMQS_DEBUG_REF_COUNT
	std::lock_guard<std::mutex> lock(RefCountLogLock);
	RefCountLog.push_back(tsf::fmt("%v:%p", callerName, caller));
#endif
}

void DriverConn::DecrementRefCount(void* caller, const char* callerName) {
	RefCount--;

#ifdef IMQS_DEBUG_REF_COUNT
	std::lock_guard<std::mutex> lock(RefCountLogLock);
	auto                        sig = tsf::fmt("%v:%p", callerName, caller);
	auto                        pos = std::find(RefCountLog.begin(), RefCountLog.end(), sig);
	IMQS_ASSERT(pos != RefCountLog.end());
	RefCountLog.erase(pos);
#endif
}

Driver::~Driver() {
}

SchemaReader* Driver::SchemaReader() {
	return nullptr;
}

SchemaWriter* Driver::SchemaWriter() {
	return nullptr;
}
} // namespace dba
} // namespace imqs
