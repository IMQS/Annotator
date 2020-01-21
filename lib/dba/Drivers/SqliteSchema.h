#pragma once
#include "Driver.h"
#include "../Schema/DB.h"

namespace imqs {
namespace dba {

// A single instance of this class is reused for all connections, so don't store any state in here.
class SqliteSchemaReader : public SchemaReader {
public:
	SqliteSchemaReader();

	Error ReadSchema(uint32_t readFlags, Executor* ex, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace) override;

private:
	Error ReadTable(const std::string& sql, schema::Table& tab);
	void  ReadFieldType(const std::string& sql, schema::Field& field, bool& ispkey);
};

// A single instance of this class is reused for all connections, so don't store any state in here.
class SqliteSchemaWriter : public SchemaWriter {
public:
	Error DropTableSpace(Executor* ex, const std::string& ts) override { return Error(); };
	Error CreateTableSpace(Executor* ex, const schema::TableSpace& ts) override { return Error(); };
	Error DropTable(Executor* ex, const std::string& table) override;
	Error CreateTable(Executor* ex, const schema::Table& table) override;
	Error CreateIndex(Executor* ex, const std::string& table, const schema::Index& idx) override;
	Error AddField(Executor* ex, const std::string& table, const schema::Field& field) override;
	Error AlterField(Executor* ex, const std::string& table, const schema::Field& existing, const schema::Field& target) override;
	Error DropField(Executor* ex, const std::string& table, const std::string& field) override;
	int   DefaultFieldWidth(Type type) override;
};

} // namespace dba
} // namespace imqs
