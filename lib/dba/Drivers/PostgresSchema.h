#pragma once
#include "Driver.h"
#include "../Schema/DB.h"

namespace imqs {
namespace dba {

// A single instance of this class is reused for all connections, so don't store any state in here.
class PostgresSchemaReader : public SchemaReader {
public:
	Error ReadSchema(uint32_t readFlags, Executor* ex, std::string tableSpace, schema::DB& db, const std::vector<std::string>* restrictTables) override;

	void DecodeField(schema::Field& f, const Attrib& name, const Attrib& datatype, int typmod, bool notNull, const Attrib& defval);
	void DecodeGeomType(schema::Field& f, const Attrib& dims, const Attrib& type);
};

// A single instance of this class is reused for all connections, so don't store any state in here.
class PostgresSchemaWriter : public SchemaWriter {
public:
	Error DropTable(Executor* ex, std::string tableSpace, const std::string& table) override;
	Error CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table) override;
	Error CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) override;
	Error AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field) override;
	Error DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field) override;
	int   DefaultFieldWidth(Type type) override;

	//Error WriteFieldType(const schema::Field& f, SqlStr& s);
};
} // namespace dba
} // namespace imqs