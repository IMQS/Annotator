#pragma once

#include "SchemaDiff.h"

namespace imqs {
namespace dbutil {

// Null diff. Produces no output.
class SchemaDiffOutputNull : public ISchemaDiffOutput {
public:
	Error CreateTable(std::string tableSpace, const dba::schema::Table& table) override { return Error(); }
	Error CreateField(std::string tableSpace, std::string table, const dba::schema::Field& field) override { return Error(); }
	Error CreateIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override { return Error(); }
	Error AlterFieldType(std::string tableSpace, std::string table, const dba::schema::Field& field) override { return Error(); }
	Error AlterFieldName(std::string tableSpace, std::string table, std::string oldName, std::string newName) override { return Error(); }
	Error DropTable(std::string tableSpace, std::string table) override { return Error(); }
	Error DropField(std::string tableSpace, std::string table, std::string field) override { return Error(); }
	Error DropIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override { return Error(); }
};

} // namespace dbutil
} // namespace imqs