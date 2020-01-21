#include "pch.h"
#include "SchemaDiffSql.h"

using namespace std;

namespace imqs {
namespace dbutil {

SchemaDiffOutputSql::SchemaDiffOutputSql(std::string driver) : Output(nullptr) {
	Output.Dialect = dba::Glob.DriverDialect(driver.c_str());
}

Error SchemaDiffOutputSql::CreateTableSpace(const dba::schema::TableSpace& ts) {
	Output.Fmt("CREATE SCHEMA %Q;\n\n", ts.GetName());
	return Error();
}

Error SchemaDiffOutputSql::CreateTable(const dba::schema::Table& table) {
	Output.Fmt("CREATE TABLE %Q (\n", table.GetName());
	for (const auto& f : table.Fields) {
		Output.Fmt(" %Q ", f.Name);
		Output.FormatType(f.Type, f.WidthOrSRID(), f.Flags);
		Output.Fmt(",\n");
	}
	auto pkey = table.PrimaryKey();
	if (pkey) {
		Output.Fmt(" PRIMARY KEY(");
		for (auto f : pkey->Fields) {
			Output.Fmt("%Q,", f);
		}
		Output.Chop(1);
		Output += "),\n";
	}
	Output.Chop(2);
	Output += "\n);\n";

	for (const auto& idx : table.Indexes) {
		if (idx.IsPrimary)
			continue;
		CreateIndex(table.GetName(), idx);
	}
	Output += "\n";
	return Error();
}

Error SchemaDiffOutputSql::CreateField(std::string table, const dba::schema::Field& field) {
	Output.Fmt("ALTER TABLE %Q ADD COLUMN %Q ", table, field.Name);
	Output.FormatType(field.Type, field.WidthOrSRID(), field.Flags);
	Output += ";\n";
	return Error();
}

Error SchemaDiffOutputSql::CreateIndex(std::string table, const dba::schema::Index& idx) {
	Output.Fmt("CREATE %v ON %Q %v(", idx.IsUnique ? "UNIQUE INDEX" : "INDEX", table, idx.IsSpatial ? "USING GIST " : "");
	for (auto f : idx.Fields) {
		Output.Fmt("%Q,", f);
	}
	Output.Chop();
	Output += ");\n";
	return Error();
}

Error SchemaDiffOutputSql::AlterFieldType(std::string table, const dba::schema::Field& field) {
	Output.Fmt("ALTER TABLE %Q ALTER COLUMN %Q TYPE ", table, field.Name);
	Output.FormatType(field.Type, field.WidthOrSRID(), field.Flags);
	Output += ";\n";
	return Error();
}

Error SchemaDiffOutputSql::AlterFieldName(std::string table, std::string oldName, std::string newName) {
	Output.Fmt("ALTER TABLE %Q RENAME %Q TO %Q;\n", table, oldName, newName);
	return Error();
}

Error SchemaDiffOutputSql::DropTableSpace(std::string ts) {
	Output.Fmt("DROP SCHEMA %Q;\n", ts);
	return Error();
}

Error SchemaDiffOutputSql::DropTable(std::string table) {
	Output.Fmt("DROP TABLE %Q;\n", table);
	return Error();
}

Error SchemaDiffOutputSql::DropField(std::string table, std::string field) {
	Output.Fmt("ALTER TABLE %Q DROP COLUMN %Q;\n", table, field);
	return Error();
}

Error SchemaDiffOutputSql::DropIndex(std::string table, const dba::schema::Index& idx) {
	string name = idx.Name;
	if (name == "") {
		// guess the name
		name = table + "_";
		for (auto f : idx.Fields)
			name += f + "_";
		name += "idx";
	}
	Output.Fmt("DROP INDEX %Q;\n", name);
	return Error();
}

} // namespace dbutil
} // namespace imqs
