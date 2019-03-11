#pragma once
#include "ConnDesc.h"

namespace imqs {
namespace dba {

class Conn;
class ConnDesc;
class Driver;
class DriverConn;
class SqlDialect;
class FlatFile;

namespace sqlparser {
class ASTCache;
}

/* Global dba root object.
*/
class IMQS_DBA_API Global {
public:
	friend class Conn;

	// Two 16-byte siphash keys. These are initialized with crypto entropy by Initialize().
	// They are used for hash table keys where the inputs are subject to DOS attacks.
	// It would be good to have a set of these keys that are cluster-wide, for shared caches, etc.
	char SipHashKey1[16];
	char SipHashKey2[16];

	// Static instantiation of our internal SQL dialect
	SqlDialect* InternalDialect = nullptr;

	// Cache of parsed SQL statements. This is safe to use from multiple threads
	sqlparser::ASTCache* ASTCache = nullptr;

	// Open a connection to a database.
	Error       Open(const ConnDesc& desc, Conn*& conn);
	Error       Open(const std::string& desc, Conn*& conn);
	SqlDialect* DriverDialect(const char* name); // This was exposed solely for the /crud/sql_translate API. When we remove that API, consider removing this too.

	// Open a flatfile.
	// If create is true, then the file is created, or an existing file is truncated.
	// If create is false, then the file must exist.
	// When you are finished with it, delete it.
	Error OpenFlatFile(const std::string& filename, bool create, FlatFile*& file);

	// Internal methods
	void Initialize();
	void Shutdown();
	void RegisterDriver(const char* name, Driver* driver);

private:
	int                              InitCount = 0;
	std::mutex                       ConnLock; // This guards access to Conns
	ohash::map<ConnDesc, Conn*>      Conns;
	ohash::map<std::string, Driver*> Drivers;

	// Called by a connection when it has .Close() called on it.
	void ConnClose(Conn* conn);
};

IMQS_DBA_API extern Global Glob;

IMQS_DBA_API void Initialize();
IMQS_DBA_API void Shutdown();
} // namespace dba
} // namespace imqs
