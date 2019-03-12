#pragma once

#include "SchemaDiff.h"

namespace imqs {
namespace dbutil {

class SchemaDiffWriter : public ISchemaDiffOutput {
public:
	dba::SchemaWriter* Writer = nullptr;
	dba::Executor*     Ex     = nullptr;

	SchemaDiffWriter(dba::SchemaWriter* writer = nullptr, dba::Executor* ex = nullptr);

	Error CreateTable(std::string tableSpace, const dba::schema::Table& table) override;
	Error CreateField(std::string tableSpace, std::string table, const dba::schema::Field& field) override;
	Error CreateIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override;
	Error AlterFieldType(std::string tableSpace, std::string table, const dba::schema::Field& field) override;
	Error AlterFieldName(std::string tableSpace, std::string table, std::string oldName, std::string newName) override;
	Error DropTable(std::string tableSpace, std::string table) override;
	Error DropField(std::string tableSpace, std::string table, std::string field) override;
	Error DropIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override;
};

} // namespace dbutil
} // namespace imqs