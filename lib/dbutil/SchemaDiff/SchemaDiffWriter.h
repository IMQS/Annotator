#pragma once

#include "SchemaDiff.h"

namespace imqs {
namespace dbutil {

class SchemaDiffWriter : public ISchemaDiffOutput {
public:
	dba::SchemaWriter* Writer = nullptr;
	dba::Executor*     Ex     = nullptr;

	SchemaDiffWriter(dba::SchemaWriter* writer = nullptr, dba::Executor* ex = nullptr);

	Error CreateTableSpace(const dba::schema::TableSpace& ts) override;
	Error CreateTable(const dba::schema::Table& table) override;
	Error CreateField(std::string table, const dba::schema::Field& field) override;
	Error CreateIndex(std::string table, const dba::schema::Index& idx) override;
	Error AlterFieldType(std::string table, const dba::schema::Field& field) override;
	Error AlterFieldName(std::string table, std::string oldName, std::string newName) override;
	Error DropTableSpace(std::string ts) override;
	Error DropTable(std::string table) override;
	Error DropField(std::string table, std::string field) override;
	Error DropIndex(std::string table, const dba::schema::Index& idx) override;
};

} // namespace dbutil
} // namespace imqs
