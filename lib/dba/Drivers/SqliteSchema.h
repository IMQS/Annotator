#pragma once
#include "Driver.h"
#include "../Schema/DB.h"

namespace imqs {
namespace dba {

// A single instance of this class is reused for all connections, so don't store any state in here.
class SqliteSchemaReader : public SchemaReader {
public:
	SqliteSchemaReader();

	Error ReadSchema(uint32_t readFlags, Executor* ex, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) override;

private:
	Error ReadTable(const std::string& sql, schema::Table& tab);
	void  ReadFieldType(const std::string& sql, schema::Field& field, bool& ispkey);
};

// A single instance of this class is reused for all connections, so don't store any state in here.
class SqliteSchemaWriter : public SchemaWriter {
public:
	Error DropTable(Executor* ex, std::string tableSpace, const std::string& table) override;
	Error CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table) override;
	Error CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) override;
	Error AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field) override;
	Error AlterField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& srcField, const schema::Field& dstField) override;
	Error DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field) override;
	int   DefaultFieldWidth(Type type) override;
};

} // namespace dba
} // namespace imqs
