#include "pch.h"
#include "SchemaWriter.h"
#include "../SqlStr.h"
#include "../Executor.h"

namespace imqs {
namespace dba {

Error SchemaWriter::WriteSchema(Executor* ex, const schema::DB& db, const std::vector<std::string>* restrictTables) {
	for (auto tableSpaceName : db.TableSpaceNames()) {
		auto err = CreateTableSpace(ex, *db.TableSpaceByName(tableSpaceName));
		if (!err.OK())
			return err;
	}

	for (auto tableName : db.TableNames()) {
		if (restrictTables && !stdutils::contains(*restrictTables, tableName))
			continue;
		auto err = CreateTable(ex, *db.TableByName(tableName));
		if (!err.OK())
			return err;
	}
	return Error();
}

Error SchemaWriter::CreateTableSpace(Executor* ex, const std::string& tableSpace) {
	schema::TableSpace ts;
	ts.SetName(tableSpace);

	return CreateTableSpace(ex, ts);
}

Error SchemaWriter::CreateTable(Executor* ex, const std::string& table, size_t nFields, const schema::Field* fields, const std::vector<std::string>& primKeyFields) {
	schema::Table t;
	t.SetName(table);

	for (size_t i = 0; i < nFields; i++)
		t.Fields.push_back(fields[i]);

	if (primKeyFields.size() != 0) {
		schema::Index idx;
		idx.IsPrimary = true;
		for (const auto& f : primKeyFields)
			idx.Fields.push_back(f);
		t.Indexes.push_back(idx);
	}

	return CreateTable(ex, t);
}

Error SchemaWriter::CreateIndex(Executor* ex, const std::string& table, const std::string& idxName, bool isUnique, const std::vector<std::string>& fields) {
	schema::Index idx;
	idx.Name     = idxName;
	idx.IsUnique = isUnique;
	for (const auto& f : fields)
		idx.Fields.push_back(f);
	return CreateIndex(ex, table, idx);
}

void SchemaWriter::CreateTable_Fields(SqlStr& s, const schema::Table& table, bool addPrimaryKey) {
	for (const auto& f : table.Fields) {
		s.Identifier(f.Name, true);
		s += " ";
		s.FormatType(f.Type, f.IsTypeGeom() ? f.SRID : f.Width, f.Flags);
		if (f.NotNull())
			s += " NOT NULL";
		s += ",";
	}
	size_t pkeyIndex = table.PrimaryKeyIndex();
	if (pkeyIndex != -1 && addPrimaryKey) {
		s += " PRIMARY KEY(";
		const auto& pkey = table.Indexes[pkeyIndex];
		for (auto f : pkey.Fields) {
			s.Identifier(f);
			s += ",";
		}
		s.Chop();
		s += ")";
	} else {
		s.Chop();
	}
}

Error SchemaWriter::CreateTable_Indexes(Executor* ex, const schema::Table& table) {
	bool dbHasSpatialIndex = !!(ex->Sql().Dialect->Flags() & SqlDialectFlags::SpatialIndex);

	for (auto idx : table.Indexes) {
		if (idx.IsPrimary)
			continue;
		// figure out if this is a spatial index
		if (idx.Fields.size() == 1) {
			auto f = table.FieldByName(idx.Fields[0]);
			if (f && f->IsTypeGeom()) {
				// Skip over spatial indexes, if the database doesn't support them (HANA)
				if (!dbHasSpatialIndex)
					continue;
				idx.IsSpatial = true;
			}
		}
		auto err = CreateIndex(ex, table.GetName(), idx);
		if (!err.OK())
			return err;
	}
	return Error();
}

} // namespace dba
} // namespace imqs
