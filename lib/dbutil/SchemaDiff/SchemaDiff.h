#pragma once

namespace imqs {
namespace dbutil {

// This interface consumes the output of the schema differ.
// For example, an implementation of this might generate SQL statements that will migrate
// a schema from one version to the next.
class ISchemaDiffOutput {
public:
	virtual Error CreateTableSpace(const dba::schema::TableSpace& ts)                         = 0;
	virtual Error CreateTable(const dba::schema::Table& table)                                = 0;
	virtual Error CreateField(std::string table, const dba::schema::Field& field)             = 0;
	virtual Error CreateIndex(std::string table, const dba::schema::Index& idx)               = 0;
	virtual Error AlterFieldType(std::string table, const dba::schema::Field& field)          = 0;
	virtual Error AlterFieldName(std::string table, std::string oldName, std::string newName) = 0;
	virtual Error DropTableSpace(std::string ts)                                              = 0;
	virtual Error DropTable(std::string table)                                                = 0;
	virtual Error DropField(std::string table, std::string field)                             = 0;
	virtual Error DropIndex(std::string table, const dba::schema::Index& idx)                 = 0;
};

class SchemaDiff {
public:
	static Error Diff(const dba::schema::DB& prev, const dba::schema::DB& next, ISchemaDiffOutput* out, std::string& log);
};

} // namespace dbutil
} // namespace imqs
