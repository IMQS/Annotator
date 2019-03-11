#pragma once

#include "../Schema/DB.h"

namespace imqs {
namespace dba {
class Tx;
class Executor;
class SqlStr;

// A SchemaWriter instantiates a schema inside a real database
// The 'tableSpace' referred to here is what most databases call a "schema".
// We use the term "table space" to avoid confusion with the term "schema" that we already use a lot.
// If tableSpace is empty or null, then we use the "default" tablespace. For Postgres, this is "public".
// For Microsoft SQL Server, this is "dbo".
// NOTE: This has no unit tests
class IMQS_DBA_API SchemaWriter {
public:
	Error CreateTable(Executor* ex, std::string tableSpace, const std::string& table, size_t nFields, const schema::Field* fields, const std::vector<std::string>& primKeyFields);
	Error CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const std::string& idxName, bool isUnique, const std::vector<std::string>& fields);
	Error WriteSchema(Executor* ex, std::string tableSpace, const schema::DB& db, const std::vector<std::string>* restrictTables);

	virtual Error DropTable(Executor* ex, std::string tableSpace, const std::string& table)                             = 0;
	virtual Error CreateTable(Executor* ex, std::string tableSpace, const schema::Table& table)                         = 0;
	virtual Error CreateIndex(Executor* ex, std::string tableSpace, const std::string& table, const schema::Index& idx) = 0;
	virtual Error AddField(Executor* ex, std::string tableSpace, const std::string& table, const schema::Field& field)  = 0;
	virtual Error DropField(Executor* ex, std::string tableSpace, const std::string& table, const std::string& field)   = 0;

	// Return the field width that is given to the field when the width is left unspecified (ie 0).
	// Many databases force you to give a length for fields such as VARCHAR.
	virtual int DefaultFieldWidth(Type type) = 0;

protected:
	void  CreateTable_Fields(SqlStr& s, const schema::Table& table, bool addPrimaryKey = true);
	Error CreateTable_Indexes(Executor* ex, const std::string& tableSpace, const schema::Table& table);
};
} // namespace dba
} // namespace imqs