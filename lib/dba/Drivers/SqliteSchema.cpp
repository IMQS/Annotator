#include "pch.h"
#include "SqliteSchema.h"
#include "../Conn.h"
#include "../Rows.h"
#include "../Tx.h"
#include "../Schema/DB.h"
#include "../CrudOps.h"

using namespace std;

namespace imqs {
namespace dba {

static const char* StrValOrEmpty(const Attrib& a) {
	if (a.IsText())
		return a.Value.Text.Data;
	return "";
}

SqliteSchemaReader::SqliteSchemaReader() {
}

Error SqliteSchemaReader::ReadSchema(uint32_t readFlags, Executor* ex, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) {
	if (restrictTables && restrictTables->size() == 0)
		return Error();

	auto s = ex->Sql();
	s.Fmt("SELECT type,name,tbl_name,sql FROM SQLITE_MASTER");
	if (restrictTables) {
		s += " WHERE tbl_name IN (";
		for (const auto& t : *restrictTables)
			s.Fmt("%q,", t);
		s.Chop();
		s += ")";
	}

	auto rows = ex->Query(s);
	for (auto row : rows) {
		string type;
		string name;
		string tbl_name;
		string sql;
		auto   err = row.Scan(type, name, tbl_name, sql);
		if (!err.OK())
			return err;

		if (tbl_name.find("sqlite_") == 0)
			continue;

		if (type == "table") {
			auto tab = db.TableByName(tbl_name, true);
			if (!!(readFlags & ReadFlagFields)) {
				err = ReadTable(sql, *tab);
				if (!err.OK())
					return err;
			}
			if (tbl_name == "spatial_ref_sys" || tbl_name == "geometry_columns")
				tab->Flags |= schema::TableFlags::Internal;
		} else if (type == "index") {
			if (!(readFlags & ReadFlagIndexes))
				continue;
		}
	}
	if (!rows.OK())
		return rows.Err();

	// Skip the geometry read, if there are no geometry metadata tables
	s.Clear();
	s += "SELECT count(*) FROM SQLITE_MASTER WHERE name IN ('geometry_columns', 'spatial_ref_sys')";
	int  nGeomMeta = 0;
	auto err       = CrudOps::Query(ex, s, nGeomMeta);
	if (!err.OK())
		return err;
	if (nGeomMeta != 2)
		return Error();

	s.Clear();
	s += "SELECT f_table_name,f_geometry_column,srtext,proj4text FROM geometry_columns,spatial_ref_sys WHERE geometry_columns.srid = spatial_ref_sys.srid";
	if (restrictTables) {
		s.Fmt(" AND f_table_name IN (");
		for (const auto& t : *restrictTables)
			s.Fmt("%q,", t);
		s.Chop();
		s += ")";
	}

	rows = ex->Query(s);
	for (auto row : rows) {
		string table, column, srtext, proj4text;
		err = row.Scan(table, column, srtext, proj4text);
		if (!err.OK())
			return err;

		auto tab = db.TableByName(table);
		if (tab) {
			auto field = tab->FieldByName(column);
			if (field) {
				int srid = 0;
				if (proj4text != "") {
					srid = projwrap::Proj2SRID(proj4text.c_str());
				} else {
					projwrap::ParseWKT(srtext.c_str(), nullptr, &srid);
				}
				field->SRID = srid;
			}
		}
	}
	if (!rows.OK())
		return rows.Err();

	return Error();
}

static bool StartsWith(const char* str, const char* find) {
	size_t i = 0;
	for (; str[i] && find[i]; i++) {
		if (str[i] != find[i])
			return false;
	}
	return find[i] == 0;
}

Error SqliteSchemaReader::ReadTable(const std::string& sql, schema::Table& tab) {
	// Examples:
	// CREATE TABLE [WaterPipeResult_04] ([rowid] INTEGER PRIMARY KEY AUTOINCREMENT,[Locality] TEXT,[Balanced_Status] TEXT,[Flow] REAL)
	// CREATE TABLE sqlite_sequence(name,seq)
	// CREATE TABLE "one" ("id" BIGINT,"bool" BOOLEAN,"i32" INTEGER,"i64" BIGINT,"txt50" VARCHAR(50),"txt" VARCHAR,"dbl" REAL,"date" TIMESTAMP,"bin" BLOB, PRIMARY KEY("id"))

	// typedField, typedWidthField, and untypedField, all share the same first portion,
	// which is the matcher for the field name.
	// The name expression allows three types of names:
	// 1. unquoted
	// 2. "double quoted"
	// 3. [bracket quoted]
	// Our double-quoted and bracket-quoted field name expressions are purposefully as loose as possible.
	// Initially, we were stricter, but then we discovered a GLS field name "Initial_Relative_Spare_Capacity;".
	// If it's allowed in SQLite, then we should allow it here too.

	// clang-format off
	std::regex createTable     (R"-(^CREATE TABLE [\["]?([a-zA-Z_][a-zA-Z_0-9]*)[\]"]?)-");
	std::regex typedField      (R"-(^([a-zA-Z_0-9\-]+|"[^"]+"|\[[^\]]+\]) ([a-zA-Z0-9 ]+)[,\)] *)-");           // "name" VARCHAR,       id INTEGER PRIMARY KEY AUTOINCREMENT,
	std::regex typedWidthField (R"-(^([a-zA-Z_0-9\-]+|"[^"]+"|\[[^\]]+\]) ([a-zA-Z0-9]+)\(([^)]+)\)[,)] *)-");  // "name" VARCHAR(50),
	std::regex untypedField    (R"-(^([a-zA-Z_0-9\-]+|"[^"]+"|\[[^\]]+\])[,\)] *)-");
	std::regex fieldpkey       (R"-(^PRIMARY KEY\(([^)]+)\)\))-");                                              // PRIMARY KEY("id"))    -- always at the end, so two closing parentheses
	std::regex ident           (R"-(^[\["]?([^"\]]+)[\]"]? *)-");
	// clang-format on

	// These are a useful bunch of sense checks to include around here, like a mini unit test
	//{
	//	// expect all of these to pass, except where mentioned otherwise
	//	std::cmatch m;
	//	bool        t1  = std::regex_search("CREATE TABLE [abc]", m, createTable);
	//	bool        t2  = std::regex_search("CREATE TABLE \"abc\"", m, createTable);
	//	bool        t3  = std::regex_search("i32 VARCHAR)", m, typedField);
	//	bool        t4  = std::regex_search("[x] VARCHAR,", m, typedField);
	//	bool        t5  = std::regex_search("[x;] VARCHAR,", m, typedField);
	//	bool        t6  = std::regex_search("[x \"thing\"] VARCHAR,", m, typedField);
	//	bool        t7  = std::regex_search("\"x\" VARCHAR,", m, typedField);
	//	bool        t8  = std::regex_search("\"id\"", m, ident);
	//	bool        t9  = std::regex_search("\"id[1]\"", m, ident);
	//	bool        t10 = std::regex_search("[id]", m, ident);
	//	bool        t11 = std::regex_search("[x] VARCHAR(50),", m, typedWidthField);
	//	bool        t12 = std::regex_search("\"x\" VARCHAR(50))", m, typedWidthField);
	//	bool        t13 = std::regex_search("\"x\" VARCHAR(50),", m, typedWidthField);
	//	bool        t14 = std::regex_search("\"x\" VARCHAR,", m, typedWidthField); // expect this to fail (has no width)
	//	int         bbb = 2332;
	//}

	const char* src = sql.c_str();

	std::cmatch m;
	if (!std::regex_search(src, m, createTable))
		return Error::Fmt("Unable to find CREATE TABLE %v", tab.GetName());

	for (; *src && *src != '('; src++) {
	}
	if (*src == 0)
		return Error::Fmt("No fields found in table %v", tab.GetName());
	src++;

	while (*src != 0) {
		schema::Field f;
		if (std::regex_search(src, m, fieldpkey)) {
			// PRIMARY KEY("id", [name])
			// Note: This formulate of primary key (specified at the end of the field list) is ambiguous with a typed
			// field declaration, at least in our simple regex-based parser.
			auto           list = m[1].str();
			const char*    sl   = list.c_str();
			vector<string> fields;
			while (*sl) {
				std::cmatch mf;
				if (!std::regex_search(sl, mf, ident))
					break;
				fields.push_back(mf[1].str());
				sl += mf[0].length();
			}
			tab.SetPrimaryKey(fields);
			src += m[0].length();
		} else if (std::regex_search(src, m, typedWidthField)) {
			// VARCHAR(50)
			f.Name = m[1].str();
			if (f.Name.at(0) == '[' || f.Name.at(0) == '"') {
				f.Name.erase(f.Name.begin());
				f.Name.erase(f.Name.end() - 1);
			}
			f.Width     = atoi(m[3].str().c_str());
			bool ispkey = false;
			ReadFieldType(m[2].str(), f, ispkey);
			if (ispkey) {
				tab.SetPrimaryKey(f.Name.c_str());
			}
			src += m[0].length();
		} else if (std::regex_search(src, m, typedField)) {
			// VARCHAR
			f.Name = m[1].str();
			if (f.Name.at(0) == '[' || f.Name.at(0) == '"') {
				f.Name.erase(f.Name.begin());
				f.Name.erase(f.Name.end() - 1);
			}
			bool ispkey = false;
			ReadFieldType(m[2].str(), f, ispkey);
			if (ispkey) {
				tab.SetPrimaryKey(f.Name.c_str());
			}
			src += m[0].length();
		} else if (std::regex_search(src, m, untypedField)) {
			f.Name = m[1].str();
			if (f.Name.at(0) == '[' || f.Name.at(0) == '"') {
				f.Name.erase(f.Name.begin());
				f.Name.erase(f.Name.end() - 1);
			}
			src += m[0].length();
		} else {
			return Error::Fmt("Failed to read field from %v: %v", tab.GetName(), src);
		}
		tab.Fields.emplace_back(std::move(f));
	}

	return Error();
}

void SqliteSchemaReader::ReadFieldType(const std::string& sql, schema::Field& field, bool& ispkey) {
	auto uc = strings::toupper(sql);
	if (strings::EndsWith(uc, " NOT NULL")) {
		field.Flags |= TypeFlags::NotNull;
		uc = uc.substr(0, uc.size() - 9);
	}
	if (strings::EndsWith(uc, " AUTOINCREMENT")) {
		field.Flags |= TypeFlags::AutoIncrement;
		uc = uc.substr(0, uc.size() - 14);
	}
	if (strings::EndsWith(uc, " PRIMARY KEY")) {
		ispkey = true;
		field.Flags |= TypeFlags::NotNull;
		uc = uc.substr(0, uc.size() - 12);
	}

	if (strings::StartsWith(uc, "POLYGON"))
		field.Type = Type::GeomPolygon;
	else if (strings::StartsWith(uc, "POLYLINE"))
		field.Type = Type::GeomPolyline;
	else if (strings::StartsWith(uc, "POINT"))
		field.Type = Type::GeomPoint;
	else if (strings::StartsWith(uc, "MULTIPOINT"))
		field.Type = Type::GeomMultiPoint;
	else if (strings::StartsWith(uc, "GEOMETRY"))
		field.Type = Type::GeomAny;

	if (field.IsTypeGeom()) {
		if (strings::EndsWith(uc, "ZM"))
			field.Flags |= TypeFlags::GeomHasM | TypeFlags::GeomHasZ;
		else if (strings::EndsWith(uc, "Z"))
			field.Flags |= TypeFlags::GeomHasZ;
		else if (strings::EndsWith(uc, "M"))
			field.Flags |= TypeFlags::GeomHasM;
		return;
	}

	switch (hash::crc32(uc)) {
	case "TEXT"_crc32:
	case "VARCHAR"_crc32:
	case "CHAR"_crc32:
		field.Type = Type::Text;
		break;
	case "INT32"_crc32:
	case "INT"_crc32:
	case "INTEGER"_crc32:
		if (field.AutoIncrement())
			field.Type = Type::Int64;
		else
			field.Type = Type::Int32;
		break;
	case "INT64"_crc32:
	case "BIGINT"_crc32:
		field.Type = Type::Int64;
		break;
	case "FLOAT"_crc32:
	case "REAL"_crc32:
		field.Type = Type::Double;
		break;
	case "GUID"_crc32:
	case "UUID"_crc32:
		field.Type = Type::Guid;
		break;
	case "BLOB"_crc32:
	case "BIN"_crc32:
		field.Type = Type::Bin;
		break;
	case "DATE"_crc32:
	case "TIMESTAMP"_crc32:
	case "DATETIME"_crc32:
		field.Type = Type::Date;
		break;
	case "BOOL"_crc32:
	case "BOOLEAN"_crc32:
	case "LOGICAL"_crc32:
		field.Type = Type::Bool;
		break;
	default:
		field.Type = Type::Text;
		break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Error SqliteSchemaWriter::DropTable(Executor* ex, std::string tableSpace, const std::string& table) {
	auto s = ex->Sql();
	s.Fmt("DROP TABLE %Q", table);
	return ex->Exec(s);
}

Error SqliteSchemaWriter::CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table) {
	auto s = ex->Sql();

	if (table.IsView() || table.IsMaterializedView())
		return Error("Views are not implemented in SqliteSchemaWriter");

	bool addPrimaryKeyToFieldList = true;

	// When our id field is declared with "INTEGER PRIMARY KEY AUTOINCREMENT", then there is no need for an additional "PRIMARY KEY" statement.
	if (table.AutoIncrementField() != -1)
		addPrimaryKeyToFieldList = false;

	s.Fmt("CREATE %v TABLE %Q (", table.IsTemp() ? "TEMP" : "", table.GetName());
	CreateTable_Fields(s, table, addPrimaryKeyToFieldList);
	s += ")";

	auto err = ex->Exec(s);
	if (!err.OK())
		return err;

	err = CreateTable_Indexes(ex, tableSpace, table);
	if (!err.OK())
		return err;

	return Error();
}

Error SqliteSchemaWriter::CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) {
	auto s = ex->Sql();
	if (idx.IsSpatial) {
		// Ignore spatial indexes, because we only use sqlite as a file interchange format
		return Error();
	}

	string name = idx.Name;
	if (name == "") {
		name = tsf::fmt("idx_%v", table);
		for (const auto& f : idx.Fields) {
			name += "_";
			name += f;
		}
	}

	s.Fmt("CREATE %v INDEX %Q ON %Q (", idx.IsUnique ? "UNIQUE" : "", name, table);
	for (const auto& f : idx.Fields) {
		s.Identifier(f, true);
		s += ",";
	}
	s.Chop();
	s += ")";

	return ex->Exec(s);
}

Error SqliteSchemaWriter::AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field) {
	// This is support by SQLite - I have just not had the need for it yet
	return Error("SqliteSchemaWriter::AddField not implemented");
}

Error SqliteSchemaWriter::DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field) {
	// I don't think this is supported by SQLite
	return Error("SqliteSchemaWriter::DropField not implemented");
}

int SqliteSchemaWriter::DefaultFieldWidth(Type type) {
	return 0;
}

} // namespace dba
} // namespace imqs
