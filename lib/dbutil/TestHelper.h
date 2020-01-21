#pragma once

namespace imqs {
namespace dbutil {

// A class that is a friend to dba classes, and can reach in and call private methods.
// Built so that unit tests can gain access to some internals.
class TestHelper {
public:
	static void Conn_InjectError(dba::Conn* con, size_t failAfter, Error failAfterWith);

	enum Connect_Flags {
		Connect_WipeTables      = 1,
		Connect_InstallModTrack = 2,
		Connect_Close           = 4, // Close after connecting, and return null
		Connect_DoNotCreate     = 8, // Do not create the database. Used in combination with WipeTables.
	};

	// Return the connection string for the given unit test database
	static std::string TestDBConnStr(std::string dbName);

	// Connect to a test DB, or create it if necessary
	static dba::Conn* ConnectToTestDB(dba::ConnDesc dbaConn, uint32_t connectFlags = Connect_WipeTables | Connect_InstallModTrack);

	// Connect to a test DB, or create it if necessary
	static dba::Conn* ConnectToTestDB(std::string dbName, uint32_t connectFlags = Connect_WipeTables | Connect_InstallModTrack);

	static void WipeAllTables(dba::Conn* con);
};

} // namespace dbutil
} // namespace imqs
