#include "pch.h"
#include "SchemaDiff.h"

using namespace std;

namespace imqs {
namespace dbutil {

static vector<string> Sorted(vector<string> v) {
	std::sort(v.begin(), v.end());
	return v;
}

Error SchemaDiff::Diff(const dba::schema::DB& prev, const dba::schema::DB& next, ISchemaDiffOutput* out, std::string& log) {
	// If we support "schemas" inside databases, then we'll need to flesh this out here.
	// tableSpace is our word for "schemas". We use tablespace to avoid confusion with the
	// word "schema", which we use to refer to the schema of a DB.
	string oldTableSpace = "";
	string newTableSpace = "";

	// New tables
	for (auto newName : Sorted(next.TableNames())) {
		auto prevTable = prev.TableByName(newName);
		if (!prevTable) {
			log += tsf::fmt("Create table %v\n", newName);
			auto nextTable = next.TableByName(newName);
			if (!nextTable->PrimaryKey())
				log += tsf::fmt("Warning: %v has no primary key\n", newName);
			auto err = out->CreateTable(newTableSpace, *nextTable);
			if (!err.OK())
				return err;
		}
	}

	// Deleted tables
	for (auto oldName : Sorted(prev.TableNames())) {
		auto nextTable = next.TableByName(oldName);
		if (!nextTable) {
			log += tsf::fmt("Drop table %v\n", oldName);
			auto err = out->DropTable(oldTableSpace, oldName);
			if (!err.OK())
				return err;
		}
	}

	// Altered tables
	for (auto tableName : Sorted(next.TableNames())) {
		auto prevTable = prev.TableByName(tableName);
		auto nextTable = next.TableByName(tableName);
		if (!prevTable || !nextTable) {
			// This is a drop or create of a table, handled by one of the two cases above (ie New table or Deleted table)
			continue;
		}
		// Deleted indexes
		for (const auto& prevIndex : prevTable->Indexes) {
			auto nextIndex = nextTable->FindIndexPtr(prevIndex);
			if (!nextIndex) {
				log += tsf::fmt("Drop index %v.%v\n", tableName, prevIndex.Description());
				auto err = out->DropIndex(oldTableSpace, tableName, prevIndex);
				if (!err.OK())
					return err;
			}
		}

		// New/renamed/altered fields
		ohash::set<string> renamedPrevFields;
		for (const auto& nextField : nextTable->Fields) {
			auto prevField = prevTable->FieldByName(nextField.Name);
			if (!prevField && nextField.FriendlyName != "")
				prevField = prevTable->FieldByFriendlyNameNoCase(nextField.FriendlyName.c_str());

			if (!prevField) {
				log += tsf::fmt("Create field %v.%v\n", tableName, nextField.Name);
				auto err = out->CreateField(newTableSpace, tableName, nextField);
				if (!err.OK())
					return err;
			} else {
				if (nextField.Name != prevField->Name) {
					renamedPrevFields.insert(prevField->Name);
					log += tsf::fmt("Rename field %v.%v -> %v\n", tableName, prevField->Name, nextField.Name);
					auto err = out->AlterFieldName(newTableSpace, tableName, prevField->Name, nextField.Name);
					if (!err.OK())
						return err;
				}
				if (nextField.Type != prevField->Type) {
					log += tsf::fmt("Change field %v.%v type from %v to %v\n", tableName, nextField.Name, dba::FieldTypeToString(prevField->Type), dba::FieldTypeToString(nextField.Type));
					auto err = out->AlterFieldType(newTableSpace, tableName, nextField);
					if (!err.OK())
						return err;
				}
			}
		}
		// Deleted fields
		for (const auto& prevField : prevTable->Fields) {
			if (renamedPrevFields.contains(prevField.Name))
				continue;
			auto nextField = nextTable->FieldByName(prevField.Name);
			if (!nextField) {
				log += tsf::fmt("Drop field %v.%v\n", tableName, prevField.Name);
				auto err = out->DropField(oldTableSpace, tableName, prevField.Name);
				if (!err.OK())
					return err;
			}
		}
		// New indexes
		for (const auto& nextIndex : nextTable->Indexes) {
			auto prevIndex = prevTable->FindIndexPtr(nextIndex);
			if (!prevIndex) {
				log += tsf::fmt("Create index %v.%v\n", tableName, nextIndex.Description());
				auto err = out->CreateIndex(newTableSpace, tableName, nextIndex);
				if (!err.OK())
					return err;
			}
		}
	}
	return Error();
}

} // namespace dbutil
} // namespace imqs
