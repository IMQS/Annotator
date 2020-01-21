#include "pch.h"
#include "Global.h"
#include "Conn.h"
#include "Drivers/Postgres.h"
#include "Drivers/MSSQL.h"
#include "Drivers/Sqlite.h"
#include "Drivers/SqlApiHANA.h"
#include "FlatFiles/DBF.h"
#include "FlatFiles/Shapefile.h"
#include "FlatFiles/CSV.h"
#include "mem.h"
#include "SqlParser/Tools/ASTCache.h"
#include "SqlStr.h"

namespace imqs {
namespace dba {

// The one-and-only dba Global object
IMQS_DBA_API Global Glob;

void Global::Initialize() {
	InitCount++;
	if (InitCount != 1)
		return;

	memset(SipHashKey1, 0, sizeof(SipHashKey1));
	memset(SipHashKey2, 0, sizeof(SipHashKey2));
	imqs::crypto::RandomBytes(SipHashKey1, sizeof(SipHashKey1));
	imqs::crypto::RandomBytes(SipHashKey2, sizeof(SipHashKey2));
	IMQS_ASSERT(memcmp(SipHashKey1, SipHashKey2, sizeof(SipHashKey1)) != 0);

	InternalDialect = new SqlDialectInternal();

	ASTCache = new sqlparser::ASTCache();

	MemPool::Initialize();
	// Note: Driver names must be more than 1 character long, otherwise they will be confused with
	// drive letters on Windows, inside ConnDesc::Parse().
	RegisterDriver("postgres", new PostgresDriver());
	RegisterDriver("sqlite3", new SqliteDriver());
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
	RegisterDriver("mssql", new MSSQLDriver());
	RegisterDriver("hana", new SqlApiHANADriver());
#endif
}

// We need to clear all internal state here, because VS debug CRT leak detection
// runs before 'Glob' has been destroyed. That's why you'll see all sorts of
// 'clear' calls in here which might seem redundant.
void Global::Shutdown() {
	InitCount--;
	if (InitCount != 0)
		return;

	for (auto it : Drivers)
		delete it.second;
	Drivers.clear();
	Conns.clear();
	MemPool::Shutdown();
	delete ASTCache;
	delete InternalDialect;
}

void Global::RegisterDriver(const char* name, Driver* driver) {
	IMQS_ASSERT(!Drivers.contains(name));
	Drivers.insert(name, driver);
}

Error Global::Open(const ConnDesc& desc, Conn*& conn) {
	std::lock_guard<std::mutex> lock(ConnLock);

	if (desc.Driver == "")
		return Error::Fmt("No driver specified in database connection '%v'", desc.ToLogSafeString());

	conn = Conns.get(desc);
	if (conn != nullptr) {
		conn->RefCount++;
		return Error();
	}

	auto driver = Drivers.get(desc.Driver);
	if (driver == nullptr)
		return ErrDriverUnknown;

	conn = new Conn();
	conn->RefCount++;
	IMQS_ASSERT(conn->RefCount == 1);
	conn->Driver   = driver;
	conn->ConnDesc = desc;

	// Open one driver-level connection for this Conn object, because it is
	// useful to have an early fail here, instead of delaying the failure
	// to the first moment when a statement is executed.
	// Also - we need to store a single instance of the SqlDialect for this
	// driver, so it actually is vital that we do this.
	DriverConn* dcon = nullptr;
	auto        err  = conn->GetDriverConn(dcon);
	if (!err.OK()) {
		delete conn;
		conn = nullptr;
		return err;
	}
	conn->Dialect = dcon->Dialect();
	conn->ReleaseDriverConn(dcon);

	Conns.insert(desc, conn);
	return Error();
}

Error Global::Open(const std::string& desc, Conn*& conn) {
	ConnDesc cd;
	auto     err = cd.Parse(desc.c_str());
	if (!err.OK())
		return err;
	return Open(cd, conn);
}

SqlDialect* Global::DriverDialect(const char* name) {
	auto driver = Drivers.get(name);
	if (driver != nullptr)
		return driver->DefaultDialect();
	return nullptr;
}

Error Global::OpenFlatFile(const std::string& filename, bool create, FlatFile*& file) {
	std::string name, ext;
	path::SplitExt(filename, name, ext);
	ext = strings::tolower(ext);
	if (ext == ".dbf")
		file = new DBF();
	else if (ext == ".shp")
		file = new Shapefile();
	else if (ext == ".csv")
		file = new CSV();
	else
		return Error::Fmt("Unsupported file type %v", ext);

	auto err = file->Open(filename, create);
	if (!err.OK()) {
		delete file;
		return Error::Fmt("while opening file %s: %s", filename, err.Message());
	}
	return Error();
}

void Global::ConnClose(Conn* conn) {
	std::lock_guard<std::mutex> lock(ConnLock);
	conn->RefCount--;
	if (conn->RefCount == 0) {
		bool found = false;
		for (auto it : Conns) {
			if (it.second == conn) {
				found = true;
				Conns.erase(it.first);
				break;
			}
		}
		IMQS_ASSERT(found);
		delete conn;
	}
}

IMQS_DBA_API void Initialize() {
	projwrap::Initialize();
	Glob.Initialize();
}

IMQS_DBA_API void Shutdown() {
	Glob.Shutdown();
	projwrap::Shutdown();
}
} // namespace dba
} // namespace imqs
