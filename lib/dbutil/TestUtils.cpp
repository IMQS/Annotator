#include "pch.h"
#include "TestUtils.h"

using namespace imqs::dba;

namespace imqs {
namespace dbutil {
namespace test {

Conn* CreateTempDB(std::string tempDir) {
	ConnDesc desc;
	desc.Driver   = "sqlite3";
	desc.Database = path::Join(tempDir, "temp-db.sqlite");
	os::Remove(desc.Database);
	Conn* con = nullptr;
	auto  err = dba::Glob.Open(desc, con);
	IMQS_ASSERT(err.OK());
	return con;
}

void CreateTempTable(Conn* con, std::string table, std::vector<schema::Field> fields) {
	auto writer = con->SchemaWriter();
	auto err    = writer->CreateTable(con, "", table, fields.size(), &fields[0], {fields[0].Name});
	IMQS_ASSERT(err.OK());
}

void CreateIndexOnTempTable(dba::Conn* con, std::string table, bool isUnique, std::vector<std::string> fields) {
	auto writer = con->SchemaWriter();
	auto err    = writer->CreateIndex(con, "", table, "", isUnique, fields);
	IMQS_ASSERT(err.OK());
}

void InsertRawSQL(Conn* con, std::string table, std::vector<std::string> fields, std::vector<std::string> records) {
	auto s = con->Sql();
	s.Fmt("INSERT INTO %Q (", table);
	for (auto f : fields)
		s.Fmt("%Q,", f);
	s.Chop();
	s += ") VALUES (";
	Tx*  tx  = nullptr;
	auto err = con->Begin(tx);
	IMQS_ASSERT(err.OK());
	for (const auto& record : records) {
		auto sr = s;
		sr += record;
		sr += ")";
		err = tx->Exec(sr);
		IMQS_ASSERT(err.OK());
	}
	err = tx->Commit();
	IMQS_ASSERT(err.OK());
}

} // namespace test
} // namespace dbutil
} // namespace imqs