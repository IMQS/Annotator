#include "pch.h"
#include "ModTracker.h"

using namespace std;

namespace imqs {
namespace dbutil {

StaticError ModTracker::ErrNotInstalled("ModTracker not installed");

//                                     0x1337C0D3DEADBEEF  0xBAAAAAADF000000D
const ModStamp ModTracker::NotFound = {0xEFBEADDED3C03713, 0x0D0000F0ADAAAABA};

static const int LatestVersion = 3;

hash::Sig16 ModStamp::Combine16(const std::vector<ModStamp>& stamps, uint64_t seed1, uint64_t seed2) {
	auto     s       = hash::Sig16::Compute(&stamps[0], sizeof(ModStamp) * stamps.size());
	uint64_t seeds[] = {seed1, seed2};
	s.MixBytes(seeds, sizeof(seeds));
	return s;
}

Error ModTracker::Install(dba::Executor* ex) {
	bool isInstalled    = false;
	int  currentVersion = 0;
	auto err            = CheckInstallStatus(ex, isInstalled, currentVersion);
	if (currentVersion == LatestVersion)
		return Error();

	if (isInstalled && currentVersion != LatestVersion)
		return Error::Fmt("ModTracker: don't know how to upgrade to latest (current: %v. latest: %v)", currentVersion, LatestVersion);

	auto writer = ex->GetConn()->SchemaWriter();

	// Only start a transaction if 'ex' is a Conn* object.
	dba::Tx*          tx = nullptr;
	dba::TxAutoCloser txCloser(nullptr);
	if (dba::Conn* con = dynamic_cast<dba::Conn*>(ex)) {
		err = con->Begin(tx);
		if (!err.OK())
			return err;
		txCloser.Bind(tx);
	}

	dba::schema::Field fMeta[3];
	fMeta[0].Set("recid", dba::Type::Int64, 0, dba::TypeFlags::AutoIncrement | dba::TypeFlags::NotNull);
	fMeta[1].Set("version", dba::Type::Int32, 0, dba::TypeFlags::NotNull);
	fMeta[2].Set("identity", dba::Type::Guid, 0, dba::TypeFlags::NotNull);
	err = writer->CreateTable(ex, "", "modtrack_meta", arraysize(fMeta), fMeta, {"recid"});
	if (!err.OK()) {
		if (dba::IsRelationAlreadyExists(err)) {
			// This means somebody has beaten us to it.
			return Error();
		}
		return err;
	}

	err = dba::CrudOps::Insert(ex, "modtrack_meta", {"version", "identity"}, {LatestVersion, Guid::Create()});
	if (!err.OK())
		return err;

	dba::schema::Field fTables[4];
	fTables[0].Set("recid", dba::Type::Int64, 0, dba::TypeFlags::AutoIncrement | dba::TypeFlags::NotNull);
	fTables[1].Set("tablename", dba::Type::Text, 0, dba::TypeFlags::NotNull);
	fTables[2].Set("createcount", dba::Type::Int64, 0, dba::TypeFlags::NotNull);
	fTables[3].Set("stamp", dba::Type::Int64, 0, dba::TypeFlags::NotNull);
	err = writer->CreateTable(ex, "", "modtrack_tables", arraysize(fTables), fTables, {"recid"});
	if (!err.OK())
		return err;

	err = writer->CreateIndex(ex, "", "modtrack_tables", "idx_modtrack_tables_tablename", true, {"tablename"});
	if (!err.OK())
		return err;

	if (tx)
		err = tx->Commit();
	if (dba::IsRelationAlreadyExists(err)) {
		// Somebody beat us to it
		err = Error();
	}
	return err;
}

Error ModTracker::CheckInstallStatus(dba::Executor* ex, bool& isInstalled, int& version) {
	isInstalled = false;
	version     = 0;
	auto reader = ex->GetConn()->SchemaReader();
	if (!reader)
		return Error::Fmt("Unable to check if ModTracker is installed: database does not support SchemaReader");
	dba::schema::DB db;
	vector<string>  tables = {"modtrack_meta"};
	auto            err    = reader->ReadSchema(0, ex, "", db, &tables);
	if (!err.OK())
		return err;

	if (!db.TableByName("modtrack_meta"))
		return Error();

	isInstalled = true;

	auto s = ex->Sql();
	s.Fmt("SELECT [version] FROM [modtrack_meta]");
	auto rows = ex->Query(s);
	for (auto row : rows)
		row.Scan(version);
	return Error();
}

Error ModTracker::GetTableStamp(dba::Executor* ex, const std::string& table, ModStamp& stamp) {
	vector<ModStamp> stamps;
	auto             err = GetTablesStamps(ex, {table}, stamps);
	if (!err.OK())
		return err;
	stamp = stamps[0];
	return Error();
}

Error ModTracker::GetTablesStamps(dba::Executor* ex, const std::vector<std::string>& tables, std::vector<ModStamp>& stamps) {
	auto s = ex->Sql();
	s.Fmt("SELECT 0 AS [type], dba_AsGUID([identity]), '' AS [tablename], 0 AS [createcount], 0 AS [stamp] FROM [modtrack_meta]");
	s += "\n UNION \n";
	s.Fmt("SELECT 1 AS [type], 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa' AS [identity], [tablename], [createcount], dba_AsGUID([stamp]) FROM [modtrack_tables] WHERE [tablename] in (");
	for (const auto& t : tables) {
		s.Fmt("%q,", t);
	}
	s.Chop();
	s += ")";

	dba::sqlparser::InternalTranslator::BakeBuiltin_Select(s);

	stamps.resize(tables.size());
	for (auto& st : stamps)
		st = NotFound;

	ohash::map<string, size_t> nameToIndex;
	for (size_t i = 0; i < tables.size(); i++)
		nameToIndex.insert(tables[i], i);

	auto    rows        = ex->Query(s);
	Guid    dbStamp     = Guid::Null();
	int64_t createCount = 0, istamp = 0;
	// Ordinarily, one uses unknown keys with siphash, but we are still lacking
	// a key rotation system for a server cluster, so we use static keys for now.
	char key1[17] = "O3UhKnplwAmhUsve";
	char key2[17] = "dJaY1LAKepIGDnne";
	for (auto row : rows) {
		int         type;
		Guid        identity;
		std::string tableName;
		int64_t     _createCount, _istamp;
		auto        err = row.Scan(type, identity, tableName, _createCount, _istamp);
		if (!err.OK())
			return err;
		if (type == 0) {
			dbStamp = identity;
		} else {
			createCount = _createCount;
			istamp      = _istamp;

			uint8_t     stat[512];
			HashBuilder h(stat);
			h.Add(dbStamp);
			h.Add(tableName);
			h.Add(createCount);
			h.Add(istamp);

			ModStamp stamp;
			stamp.QWords[0] = siphash24(h.Buf, h.Len, key1);
			stamp.QWords[1] = siphash24(h.Buf, h.Len, key2);

			if (!nameToIndex.contains(tableName))
				return Error::Fmt("ModTracker::GetTablesStamps(): table '%v' not found in lookup list [%v]", tableName, strings::Join(tables, ","));
			stamps[nameToIndex.get(tableName)] = stamp;
		}
	}
	if (!rows.OK()) {
		if (dba::IsTableNotFound(rows.Err()))
			return ErrNotInstalled;
		return rows.Err();
	}

	return Error();
}

Error ModTracker::IncrementTableStamp(dba::Executor* ex, const std::string& table) {
	return IncrementTableStamps(ex, {table});
}

Error ModTracker::IncrementTableStamps(dba::Executor* ex, const std::vector<std::string>& tables) {
	if (tables.size() == 0)
		return Error();
	auto s = ex->Sql();
	s.Fmt("SELECT [tablename] FROM [modtrack_tables] WHERE [tablename] IN (");
	for (const auto& t : tables)
		s.Fmt("%q,", t);
	s.Chop();
	s += ")";

	ohash::set<std::string> exist;
	auto                    rows = ex->Query(s);
	for (auto row : rows)
		exist.insert(row[0].ToString());
	if (!rows.OK())
		return rows.Err();

	std::vector<std::string> notexist;
	for (const auto& t : tables) {
		if (!exist.contains(t))
			notexist.push_back(t);
	}

	if (exist.size() != 0) {
		s.Clear();
		s.Fmt("UPDATE [modtrack_tables] SET [stamp] = [stamp] + 1 WHERE [tablename] IN (");
		for (const auto& t : exist)
			s.Fmt("%q,", t);
		s.Chop();
		s += ")";
		auto err = ex->Exec(s);
		if (!err.OK())
			return err;
	}

	if (notexist.size() != 0) {
		if (!!(s.Dialect->Flags() & dba::SqlDialectFlags::MultiRowInsert)) {
			s.Clear();
			s.Fmt("INSERT INTO [modtrack_tables] ([tablename], [createcount], [stamp]) VALUES ");
			for (const auto& t : notexist) {
				s.Fmt("(%q,1,1),", t);
			}
			s.Chop();
			auto err = ex->Exec(s);
			if (!err.OK())
				return err;
		} else {
			s.Clear();
			s.Fmt("INSERT INTO [modtrack_tables] ([tablename], [createcount], [stamp]) VALUES ($1,1,1)");
			dba::Stmt st;
			auto      err = ex->Prepare(s, {dba::Type::Text}, st);
			if (!err.OK())
				return err;
			for (const auto& t : notexist) {
				err = st.Exec({t});
				if (!err.OK())
					return err;
			}
		}
	}

	return Error();
}
} // namespace dbutil
} // namespace imqs
