#pragma once
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "Driver.h"
#include "../Schema/DB.h"

namespace imqs {
namespace dba {

class HANASchemaReader : public SchemaReader {
public:
	Error ReadSchema(uint32_t readFlags, Executor* ex, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace) override;

	void DecodeField(schema::Field& f, const char* name, int32_t datatype, bool nullable, int width, const char* generation_type);
};

class HANASchemaWriter : public SchemaWriter {
public:
	static const int DefaultTextFieldWidth;
	static const int DefaultBinFieldWidth;

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
#endif
