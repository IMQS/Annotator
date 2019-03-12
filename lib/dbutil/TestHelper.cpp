#include "pch.h"
#include "TestHelper.h"
#include "ModTracker.h"

using namespace std;
using namespace imqs::dba;

namespace imqs {
namespace dbutil {

void TestHelper::Conn_InjectError(Conn* con, size_t failAfter, Error failAfterWith) {
	con->InjectError(failAfter, failAfterWith);
}

std::string TestHelper::TestDBConnStr(std::string dbName) {
	std::string host = os::GetEnv("UNIT_TEST_POSTGRES_HOST");
	if (host == "")
		host = "localhost";

	ConnDesc d;
	d.Driver   = "postgres";
	d.Host     = host;
	d.Database = dbName;
	d.Username = "unit_test_user";
	d.Password = "unit_test_password";
	return d.ToString();
}

Conn* TestHelper::ConnectToTestDB(dba::ConnDesc desc, uint32_t connectFlags) {
	dba::Conn* con    = nullptr;
	auto       err    = dba::Glob.Open(desc, con);
	bool       exists = err.OK();
	if (!exists && !!(connectFlags & Connect_DoNotCreate))
		return nullptr;

	if (!exists) {
		// If Postgres, connect to 'postgres' DB, then create desired DB from there
		IMQS_ASSERT(desc.Driver == "postgres");
		dba::ConnDesc tmp = desc;
		tmp.Database      = "postgres";
		err               = dba::Glob.Open(tmp, con);
		if (!err.OK())
			tsf::print("Error opening seed 'postgres' DB (%v): %v\nWas trying to connect to %v", tmp.ToString(), err.Message(), desc.ToString());
		IMQS_ASSERT(err.OK());
		err = con->Exec(tsf::fmt("CREATE DATABASE %v OWNER = %v", desc.Database, desc.Username).c_str());
		IMQS_ASSERT(err.OK());
		con->Close();

		// Reconnect to DB, and install PostGIS
		err = dba::Glob.Open(desc, con);
		IMQS_ASSERT(err.OK());
		err = con->Exec("CREATE EXTENSION postgis");
		if (!err.OK())
			tsf::print("Failed to install postgis in test db %v: %v\n", desc.ToString(), err.Message());
		IMQS_ASSERT(err.OK());
	} else {
		if (!!(connectFlags & Connect_WipeTables)) {
			// drop all tables
			dba::schema::DB schema;
			IMQS_ASSERT(con->SchemaReader()->ReadSchemaInTx(0, con, "", schema, nullptr).OK()); // read existing schema, so we can wipe existing DB
			for (auto t : schema.TableNames()) {
				auto tab = schema.TableByName(t);
				if (tab->IsInternal())
					continue;
				auto s = con->Sql();
				s.Fmt("DROP TABLE %Q", t);
				err = con->Exec(s);
				if (!err.OK())
					tsf::print("Failed to drop table: %v\n", err.Message());
				IMQS_ASSERT(err.OK());
			}
		}
	}

	if (!!(connectFlags & Connect_InstallModTrack)) {
		err = ModTracker::Install(con);
		IMQS_ASSERT(err.OK());
	}
	if (!!(connectFlags & Connect_Close)) {
		con->Close();
		con = nullptr;
	}
	return con;
}

Conn* TestHelper::ConnectToTestDB(std::string dbName, uint32_t connectFlags) {
	ConnDesc d;
	auto     err = d.Parse(TestDBConnStr(dbName).c_str());
	IMQS_ASSERT(err.OK());
	return ConnectToTestDB(d, connectFlags);
}

} // namespace dbutil
} // namespace imqs
