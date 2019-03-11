#pragma once

#include "Table.h"

namespace imqs {
namespace dba {
namespace schema {

class IMQS_DBA_API DB {
public:
	friend class Table;
	DB();
	DB(const DB& db);
	~DB();

	int         Version = 0; // Used by the migration system
	std::string Name;

	void                     Clear();
	Table*                   TableByName(const std::string& name, bool createIfNotExist = false);
	const Table*             TableByName(const std::string& name) const;
	const Table*             TableByNameNoCase(const std::string& name) const;
	std::vector<std::string> TableNames() const;
	size_t                   TableCount() const { return Tables.size(); }
	void                     InsertOrReplaceTable(Table* table);
	void                     RemoveTable(const std::string& name);
	Error                    Parse(const char* schema);
	Error                    ParseFile(const std::string& filename);
	void                     ToString(std::string& s) const;
	Error                    WriteFile(const std::string& filename) const;
	DB&                      operator=(const DB& src);

private:
	ohash::map<std::string, Table*> Tables;
};
} // namespace schema
} // namespace dba
} // namespace imqs
