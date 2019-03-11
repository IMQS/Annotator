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

Error DB::ParseFile(const std::string& filename) {
	void*  buf = nullptr;
	size_t len = 0;
	auto   err = os::ReadWholeFile(filename, buf, len);
	if (!err.OK())
		return err;
	err = Parse((const char*) buf);
	free(buf);
	return err;
}

Error DB::WriteFile(const std::string& filename) const {
	std::string s;
	ToString(s);
	return os::WriteWholeFile(filename, s.c_str(), s.length());
}

void DB::ToString(std::string& s) const {
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
		TableByName(name)->ToString(s);
		s += "\r\n";
	}
}
} // namespace schema
} // namespace dba
} // namespace imqs
