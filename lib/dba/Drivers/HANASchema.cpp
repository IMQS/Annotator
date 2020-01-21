#include "pch.h"
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "HANASchema.h"
#include "../Executor.h"
#include "../Rows.h"

namespace imqs {
namespace dba {

const int HANASchemaWriter::DefaultTextFieldWidth = 5000;
const int HANASchemaWriter::DefaultBinFieldWidth  = 5000;

Error HANASchemaReader::ReadSchema(uint32_t readFlags, Executor* ex, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace) {
	if (restrictTables && restrictTables->size() == 0)
		return Error();
	auto s = ex->Sql();

	std::vector<std::string> tableNames;
	std::vector<uint32_t>    tableOIDs;

	s.Fmt("SELECT schema_name,table_name,table_type,table_oid FROM tables ");
	if (tableSpace != "")
		s.Fmt(" WHERE schema_name IN (%q)", tableSpace);
	else
		s.Fmt(
		    " WHERE schema_name "
		    " NOT IN ('SYS', 'UIS') "
		    " AND schema_name NOT LIKE 'HANA_%'"
		    " AND schema_name NOT LIKE '_SYS%'"
		    " AND schema_name NOT LIKE 'SYS_%'"
		    " AND schema_name NOT LIKE 'SAP_%'");

	if (restrictTables) {
		s += " AND table_name IN (";
		for (const auto& t : *restrictTables)
			s.Fmt("%q,", t);
		s.Chop();
		s += ")";
	}

	auto rows = ex->Query(s);
	for (auto row : rows) {
		auto table = db.TableByName(row[1].ToString(), true);
		tableNames.push_back(row[1].ToString());
		tableOIDs.push_back(row[3].ToInt32());
	}
	if (!rows.OK())
		return rows.Err();

	if (tableOIDs.size() == 0)
		return Error();

	if (!!(readFlags & ReadFlagFields)) {
		s.Clear();
		s.Fmt("SELECT table_name,column_name,data_type_id,is_nullable,length,generation_type FROM table_columns WHERE table_oid IN (");
		for (auto oid : tableOIDs) {
			s.Fmt("%v,", oid);
		}
		s.Chop();
		s += ") ORDER BY table_oid ASC";
		rows = ex->Query(s);

		std::string    tableName;
		schema::Table* table = nullptr;
		for (auto row : rows) {
			if (tableName != row[0].RawString()) {
				tableName = row[0].RawString();
				table     = db.TableByName(tableName, true);
			}
			IMQS_ANALYSIS_ASSUME(table != nullptr); // for clang
			schema::Field f;
			DecodeField(f, row[1].RawString(), row[2].ToInt32(), row[3].ToBool(), row[4].ToInt32(), row[5].IsNull() ? "" : row[5].RawString());
			table->Fields.emplace_back(f);
		}
		if (!rows.OK())
			return rows.Err();

		// Geometry fields. The only thing we retrieve here is the SRID
		s.Clear();
		s.Fmt("SELECT table_name,column_name,srs_id FROM st_geometry_columns WHERE table_name IN (");
		for (auto name : tableNames) {
			s.Fmt("%q,", name);
		}
		s.Chop();
		s += ")";
		rows = ex->Query(s);

		tableName = "";
		table     = nullptr;
		for (auto row : rows) {
			if (tableName != row[0].RawString()) {
				tableName = row[0].RawString();
				table     = db.TableByName(tableName, true);
			}
			IMQS_ANALYSIS_ASSUME(table != nullptr); // for clang
			schema::Field* f = table->FieldByName(row[1].RawString());
			f->SRID          = row[2].ToInt32();
		}
		if (!rows.OK())
			return rows.Err();
	}

	if (!!(readFlags & ReadFlagIndexes)) {
		s.Clear();
		s.Fmt("SELECT table_name,index_name,constraint,column_name FROM index_columns WHERE table_oid IN (");
		for (auto oid : tableOIDs) {
			s.Fmt("%v,", oid);
		}
		s.Chop();
		s += ") ORDER BY table_oid,index_oid,position ASC";
		rows = ex->Query(s);

		std::string    tableName;
		schema::Table* table = nullptr;
		schema::Index* index = nullptr;
		for (auto row : rows) {
			if (tableName != row[0].RawString()) {
				tableName = row[0].RawString();
				table     = db.TableByName(tableName);
			}
			IMQS_ANALYSIS_ASSUME(table != nullptr); // for clang
			if (index == nullptr || index->Name != row[1].RawString()) {
				table->Indexes.push_back(schema::Index());
				index       = &table->Indexes.back();
				index->Name = row[1].RawString();
				if (row[2].IsText() && strcmp(row[2].RawString(), "PRIMARY KEY") == 0) {
					index->IsUnique  = true;
					index->IsPrimary = true;
				} else if (row[2].IsText() && strcmp(row[2].RawString(), "UNIQUE") == 0) {
					index->IsUnique = true;
				}
			}
			index->Fields.emplace_back(row[3].ToString());
		}
	}

	return Error();
}

void HANASchemaReader::DecodeField(schema::Field& f, const char* name, int32_t datatype, bool nullable, int width, const char* generation_type) {
	f.Name = name;
	switch (datatype) {
	case 16: f.Type = Type::Bool; break;
	case 4: f.Type = Type::Int32; break;
	case -5: f.Type = Type::Int64; break;
	case -9:
		f.Type  = Type::Text;
		f.Width = width;
		break;
	case 8: f.Type = Type::Double; break;
	case 93: f.Type = Type::Date; break;
	case -3:
		f.Type  = Type::Bin;
		f.Width = width;
		break;
	case 29812: f.Type = Type::GeomAny; break;
	default:
		f.Type = Type::Int32;
	}
	if (!nullable)
		f.Flags |= TypeFlags::NotNull;
	if (generation_type && strstr(generation_type, "AS IDENTITY") != nullptr)
		f.Flags |= TypeFlags::AutoIncrement;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Error HANASchemaWriter::DropTable(Executor* ex, const std::string& table) {
	auto s = ex->Sql();
	s.Fmt("DROP TABLE %Q", table);
	return ex->Exec(s);
}

Error HANASchemaWriter::CreateTable(Executor* ex, const schema::Table& table) {
	if (table.IsView() || table.IsMaterializedView())
		return Error("Views are not implemented in HANASchemaWriter");

	auto s = ex->Sql();
	s.Fmt("CREATE COLUMN TABLE %Q (", table.GetName());
	// Don't include the PRIMARY KEY part of the statement if we have an AUTOINCREMENT field, because
	// the AUTOINCREMENT ends up producing "BIGINT PRIMARY KEY GENERATED BY DEFAULT AS IDENTITY", and
	// HANA doesn't allow us to specify PRIMARY KEY twice.
	bool addPrimaryKey = table.AutoIncrementField() == -1;
	CreateTable_Fields(s, table, addPrimaryKey);
	s += ")";

	auto err = ex->Exec(s);
	if (!err.OK())
		return err;

	err = CreateTable_Indexes(ex, table);
	if (!err.OK())
		return err;

	return Error();
}

Error HANASchemaWriter::CreateIndex(Executor* ex, const std::string& table, const schema::Index& idx) {
	auto s = ex->Sql();

	std::string fields = "";
	for (const auto& f : idx.Fields) {
		fields += f;
		fields += "_";
	}
	if (fields.length() != 0)
		fields.erase(fields.begin() + fields.length() - 1);
	std::string idxName = "idx_" + table + "_" + fields;

	s.Clear();
	s.Fmt("CREATE %v INDEX %Q ON %Q (", idx.IsUnique ? "UNIQUE" : "", idxName, table);
	for (const auto& f : idx.Fields) {
		s.Identifier(f, true);
		s += ",";
	}
	s.Chop();
	s += ")";

	return ex->Exec(s);
}

Error HANASchemaWriter::AddField(Executor* ex, const std::string& table, const schema::Field& field) {
	return Error("HANASchemaWriter::AddField not implemented");
}
Error HANASchemaWriter::AlterField(Executor* ex, const std::string& table, const schema::Field& existing, const schema::Field& target) {
	return Error("HANASchemaWriter::AlterField not implemented");
}

Error HANASchemaWriter::DropField(Executor* ex, const std::string& table, const std::string& field) {
	return Error("HANASchemaWriter::DropField not implemented");
}

int HANASchemaWriter::DefaultFieldWidth(Type type) {
	switch (type) {
	case Type::Text: return DefaultTextFieldWidth;
	case Type::Bin: return DefaultBinFieldWidth;
	default:
		return 0;
	}
}

} // namespace dba
} // namespace imqs
#endif
