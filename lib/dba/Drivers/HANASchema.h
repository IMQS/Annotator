#pragma once
#if !defined(IMQS_DBA_EXCLUDE_SQLAPI)
#include "Driver.h"
#include "../Schema/DB.h"

namespace imqs {
namespace dba {

class HANASchemaReader : public SchemaReader {
public:
	Error ReadSchema(uint32_t readFlags, Executor* ex, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) override;

	void DecodeField(schema::Field& f, const char* name, int32_t datatype, bool nullable, int width, const char* generation_type);
};

class HANASchemaWriter : public SchemaWriter {
public:
	static const int DefaultTextFieldWidth;
	static const int DefaultBinFieldWidth;

	Error DropTable(Executor* ex, std::string tableSpace, const std::string& table) override;
	Error CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table) override;
	Error CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) override;
	Error AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field) override;
	Error AlterField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& srcField, const schema::Field& dstField) override;
	Error DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field) override;
	int   DefaultFieldWidth(Type type) override;

	std::string MakeIdent(SqlStr& s, const std::string& tableSpace, const std::string table);
};
} // namespace dba
} // namespace imqs
#endif