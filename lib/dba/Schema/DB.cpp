#include "pch.h"
#include "DB.h"
#include "SchemaParser.h"

namespace imqs {
namespace dba {
namespace schema {

DB::DB() {
}

DB::DB(const DB& src) {
	*this = src;
}

DB::~DB() {
	Clear();
}

void DB::Clear() {
	for (auto iter : Tables)
		delete iter.second;
	Tables.clear();
}

DB& DB::operator=(const DB& src) {
	Clear();
	Name    = src.Name;
	Version = src.Version;
	for (auto t : src.Tables)
		InsertOrReplaceTable(t.second->CloneNew());
	return *this;
}

std::vector<std::string> DB::TableSpaceNames() const {
	std::vector<std::string> names;
	names.reserve(TableSpaces.size());
	for (auto t : TableSpaces)
		names.push_back(t.first);
	return names;
}

TableSpace* DB::TableSpaceByName(const std::string& name, bool createIfNotExist) {
	TableSpace* ts = TableSpaces.get(name);
	if (!ts && createIfNotExist) {
		ts        = new TableSpace();
		ts->Name  = name;
		ts->Owner = this;
		TableSpaces.insert(name, ts);
	}
	return ts;
}

const TableSpace* DB::TableSpaceByName(const std::string& name) const {
	return const_cast<DB*>(this)->TableSpaceByName(name, false);
}

Table* DB::TableByName(const std::string& name, bool createIfNotExist) {
	Table* t = Tables.get(name);
	if (!t && createIfNotExist) {
		t        = new Table();
		t->Name  = name;
		t->Owner = this;
		Tables.insert(name, t);
	}
	return t;
}

const Table* DB::TableByName(const std::string& name) const {
	return const_cast<DB*>(this)->TableByName(name, false);
}

const Table* DB::TableByNameNoCase(const std::string& name) const {
	for (auto it : Tables) {
		if (strings::eqnocase(it.first, name))
			return it.second;
	}
	return nullptr;
}

std::vector<std::string> DB::TableNames() const {
	std::vector<std::string> names;
	names.reserve(Tables.size());
	for (auto t : Tables)
		names.push_back(t.first);
	return names;
}

void DB::InsertOrReplaceTable(Table* table) {
	IMQS_ASSERT(table != nullptr); // this is purely here to satisfy the MSVC static analyzer, VS 2015
	IMQS_ASSERT(table->Owner == nullptr);

	auto existing = Tables.get(table->Name);
	if (existing != nullptr) {
		IMQS_ASSERT(existing != table);
		delete existing;
	}
	Tables.insert(table->Name, table, true);
	table->Owner = this;
}

void DB::InsertOrReplaceTableSpace(TableSpace* ts) {
	IMQS_ASSERT(ts != nullptr); // this is purely here to satisfy the MSVC static analyzer, VS 2015
	IMQS_ASSERT(ts->Owner == nullptr);

	auto existing = TableSpaces.get(ts->Name);
	if (existing != nullptr) {
		IMQS_ASSERT(existing != ts);
		delete existing;
	}
	TableSpaces.insert(ts->Name, ts, true);
	ts->Owner = this;
}

void DB::RemoveTable(const std::string& name) {
	auto existing = Tables.get(name);
	if (existing == nullptr)
		return;
	// make a copy of name, just in case it is owned by the table.
	// This happens if the user does:
	// auto tab = db.TableByName("foo");
	// db.RemoveTable(tab->GetName());
	auto nameCopy = name;
	delete existing;
	Tables.erase(nameCopy);
}

Error DB::Parse(const char* schema) {
	return SchemaParse(schema, *this);
}

// Returns a list of all sub schema files excluding `DB`-prev.schema
std::vector<std::string> ScanSubSchemaFiles(std::string filename) {
	auto prevSchema   = path::ChangeExtension(path::Filename(filename), "-prev.schema");
	auto wildcard     = path::ChangeExtension(filename, "-*.schema");
	auto onlyWildcard = path::Filename(wildcard);
	auto dir          = wildcard.substr(0, wildcard.size() - onlyWildcard.size());

	std::vector<std::string> files;
	os::FindFiles(dir, [&](const os::FindFileItem& item) -> bool {
		if (item.IsDir)
			return false;
		if (item.Name != prevSchema && strings::MatchWildcardNoCase(item.Name, onlyWildcard))
			files.push_back(path::Join(dir, item.Name));
		return true;
	});
	return files;
}

Error DB::ParseFile(const std::string& filename, bool addSubSchemas, std::string* finalShemaStr) {
	std::string schemaStr;
	size_t      len = 0;
	auto        err = os::ReadWholeFile(filename, schemaStr);
	if (!err.OK())
		return err;

	if (addSubSchemas) {
		auto subSchemaFiles = ScanSubSchemaFiles(filename);
		for (const auto& subSchemaFile : subSchemaFiles) {
			std::string subSchemaStr;
			err = os::ReadWholeFile(subSchemaFile, subSchemaStr);
			if (!err.OK())
				return err;

			schemaStr += "\n\n" + subSchemaStr;
		}
		if (finalShemaStr)
			*finalShemaStr = schemaStr;
	}

	return Parse(schemaStr.c_str());
}

Error DB::WriteFile(const std::string& filename, const std::string& newline) const {
	std::string s;
	ToString(s, newline);
	return os::WriteWholeFile(filename, s.c_str(), s.length());
}

void DB::ToString(std::string& s, const std::string& newline) const {
	auto names = TableNames();
	sort(names.begin(), names.end(), [this](const std::string& a, const std::string& b) -> bool {
		auto ta = this->TableByName(a);
		auto tb = this->TableByName(b);
		if ((ta->InheritedFrom == "") != (tb->InheritedFrom == ""))
			return ta->InheritedFrom == "";
		return ta->GetName() < tb->GetName();
	});

	// write base tables
	for (auto name : names) {
		TableByName(name)->ToString(s, newline);
		s += newline;
	}
}
} // namespace schema
} // namespace dba
} // namespace imqs
