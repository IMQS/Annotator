#include "pch.h"
#include "TestUtils.h"

using namespace std;
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

void CreateTable(Conn* con, std::string table, std::vector<schema::Field> fields) {
	auto writer = con->SchemaWriter();
	auto err    = writer->CreateTable(con, table, fields.size(), &fields[0], {fields[0].Name});
	IMQS_ASSERT(err.OK());
}

/* Example:

dbutil::test::CreateTableWithData(tc.TempDB, "foo",
								{{"id", Type::Int64}, {"geom", Type::GeomAny}},
								"1,POINT(1 1)\n"
								"2,POINT(2 2)\n");

*/
void CreateTableWithData(dba::Conn* con, std::string table, std::vector<dba::schema::Field> fields, std::string data) {
	CreateTable(con, table, fields);

	auto         reader = io::StringReader(data);
	csv::Decoder dec(&reader);

	dba::AttribList values;
	size_t          nRecords = 0;

	for (size_t row = 0; true; row++) {
		string         decoded;
		vector<size_t> starts;
		auto           err = dec.ClearAndReadLine(decoded, starts);
		if (err == ErrEOF)
			break;
		if (!err.OK())
			tsf::print("CreateTableWithData: Invalid CSV line %v: %v\n", row, err.Message());
		IMQS_ASSERT(err.OK());
		IMQS_ASSERT(starts.size() - 1 == fields.size()); // each CSV line must have one value for every field
		for (size_t i = 0; i < starts.size() - 1; i++) {
			dba::Attrib val;
			val.SetText(decoded.substr(starts[i], starts[i + 1] - starts[i]));
			val = val.ConvertTo(fields[i].Type);
			if (val.IsGeom() && val.GeomSRID() == 0)
				val.Value.Geom.Head->SRID = fields[i].SRID;
			values.Add()->CopyFrom(val, &values);
		}
		nRecords++;
	}
	vector<string> fieldNames;
	for (const auto& f : fields)
		fieldNames.push_back(f.Name);
	auto err = dba::CrudOps::Insert(con, table, nRecords, fieldNames, values.At(0));
	if (!err.OK())
		tsf::print("CreateTableWithData: Failed to insert record: %v\n", err.Message());
	IMQS_ASSERT(err.OK());
}

void CreateIndexOnTable(dba::Conn* con, std::string table, bool isUnique, std::vector<std::string> fields) {
	auto writer = con->SchemaWriter();
	auto err    = writer->CreateIndex(con, table, "", isUnique, fields);
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