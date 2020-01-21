#pragma once

#include "Table.h"
#include "TableSpace.h"

namespace imqs {
namespace dba {
namespace schema {

class IMQS_DBA_API DB {
public:
	friend class Table;
	friend class TableSpace;
	DB();
	DB(const DB& db);
	~DB();

	int         Version = 0; // Used by the migration system
	std::string Name;

	void                     Clear();
	std::vector<std::string> TableSpaceNames() const;
	TableSpace*              TableSpaceByName(const std::string& name, bool createIfNotExist = false);
	const TableSpace*        TableSpaceByName(const std::string& name) const;
	Table*                   TableByName(const std::string& name, bool createIfNotExist = false);
	const Table*             TableByName(const std::string& name) const;
	const Table*             TableByNameNoCase(const std::string& name) const;
	std::vector<std::string> TableNames() const;
	size_t                   TableCount() const { return Tables.size(); }
	void                     InsertOrReplaceTable(Table* table);
	void                     InsertOrReplaceTableSpace(TableSpace* table);
	void                     RemoveTable(const std::string& name);
	Error                    Parse(const char* schema);
	void                     ToString(std::string& s, const std::string& newline) const;
	Error                    WriteFile(const std::string& filename, const std::string& newline) const;
	DB&                      operator=(const DB& src);

	// ParseFile's optional parameter `addSubSchemas` will append all subschema
	// files (`DB`-*.schema, excluding `DB`-prev.schema) to the base schema
	// before it gets parsed. This allows us to split a large schema into
	// smaller, more manageable files.
	Error ParseFile(const std::string& filename, bool addSubSchemas = false, std::string* finalShemaStr = nullptr);

private:
	ohash::map<std::string, Table*>      Tables;
	ohash::map<std::string, TableSpace*> TableSpaces;
};
} // namespace schema
} // namespace dba
} // namespace imqs
