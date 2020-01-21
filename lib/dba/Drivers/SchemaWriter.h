#pragma once

#include "../Schema/DB.h"

namespace imqs {
namespace dba {
class Tx;
class Executor;
class SqlStr;

// A SchemaWriter instantiates a schema inside a real database
class IMQS_DBA_API SchemaWriter {
public:
	Error CreateTableSpace(Executor* ex, const std::string& tableSpace);
	Error CreateTable(Executor* ex, const std::string& table, size_t nFields, const schema::Field* fields, const std::vector<std::string>& primKeyFields);
	Error CreateIndex(Executor* ex, const std::string& table, const std::string& idxName, bool isUnique, const std::vector<std::string>& fields);
	Error WriteSchema(Executor* ex, const schema::DB& db, const std::vector<std::string>* restrictTables);

	virtual Error DropTableSpace(Executor* ex, const std::string& ts)                                                            = 0;
	virtual Error DropTable(Executor* ex, const std::string& table)                                                              = 0;
	virtual Error CreateTableSpace(Executor* ex, const schema::TableSpace& tableSpace)                                           = 0;
	virtual Error CreateTable(Executor* ex, const schema::Table& table)                                                          = 0;
	virtual Error CreateIndex(Executor* ex, const std::string& table, const schema::Index& idx)                                  = 0;
	virtual Error AddField(Executor* ex, const std::string& table, const schema::Field& field)                                   = 0;
	virtual Error AlterField(Executor* ex, const std::string& table, const schema::Field& existing, const schema::Field& target) = 0;
	virtual Error DropField(Executor* ex, const std::string& table, const std::string& field)                                    = 0;

	// Return the field width that is given to the field when the width is left unspecified (ie 0).
	// Many databases force you to give a length for fields such as VARCHAR.
	virtual int DefaultFieldWidth(Type type) = 0;

protected:
	void  CreateTable_Fields(SqlStr& s, const schema::Table& table, bool addPrimaryKey = true);
	Error CreateTable_Indexes(Executor* ex, const schema::Table& table);
};
} // namespace dba
} // namespace imqs
