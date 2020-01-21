#include "pch.h"
#include "Table.h"
#include "DB.h"
#include "SchemaParser.h"

using namespace std;
using namespace tsf;

namespace imqs {
namespace dba {
namespace schema {

bool Relation::operator==(const Relation& b) const {
	return Type == b.Type &&
	       AccessorName == b.AccessorName &&
	       LocalField == b.LocalField &&
	       ForeignTable == b.ForeignTable &&
	       ForeignField == b.ForeignField &&
	       UIGroupOrder == b.UIGroupOrder &&
	       UIModule == b.UIModule;
}

bool Relation::operator!=(const Relation& b) const {
	return !(*this == b);
}

bool Index::operator==(const Index& b) const {
	return Name == b.Name &&
	       Fields == b.Fields &&
	       IsPrimary == b.IsPrimary &&
	       IsUnique == b.IsUnique;
}
bool Index::operator!=(const Index& b) const {
	return !(*this == b);
}

bool Index::IsSingleField(std::string fieldName) const {
	return Fields.size() == 1 && Fields[0] == fieldName;
}

std::string Index::Description() const {
	std::string s;
	if (IsPrimary)
		s += "pkey";
	else if (IsUnique)
		s += "unique";
	s += "(";
	for (const auto& f : Fields)
		s += f + ",";
	s.erase(s.end() - 1);
	s += ")";
	return s;
}

Table* Table::CloneNew() const {
	auto t   = new Table();
	*t       = *this;
	t->Owner = nullptr;
	return t;
}

Table Table::Clone() const {
	auto t  = *this;
	t.Owner = nullptr;
	return t;
}

bool Table::SetName(const std::string& name) {
	if (Owner && Owner->Tables.contains(name))
		return false;
	if (Owner)
		Owner->Tables.erase(Name);
	Name = name;
	if (Owner)
		Owner->Tables.insert(Name, this);
	return true;
}

std::string Table::FriendlyNameOrName() const {
	return FriendlyName != "" ? FriendlyName : Name;
}

Field* Table::FieldByName(const char* name) {
	// safe cast, because createIfNotExist = false
	return const_cast<Table*>(this)->FieldByName(name, false);
}

Field* Table::FieldByName(const std::string& name) {
	return FieldByName(name.c_str());
}

const Field* Table::FieldByName(const char* name) const {
	// safe cast, because createIfNotExist = false
	return const_cast<Table*>(this)->FieldByName(name, false);
}

const Field* Table::FieldByName(const std::string& name) const {
	return FieldByName(name.c_str());
}

Field* Table::FieldByName(const char* name, bool createIfNotExist) {
	auto f = FieldIndex(name);
	if (f != -1)
		return &Fields.at(f);
	if (!createIfNotExist)
		return nullptr;
	Fields.push_back(Field());
	Field* newF = &Fields.back();
	newF->Name  = name;
	return newF;
}

const Field* Table::FieldByNameNoCase(const std::string& name) const {
	return FieldByNameNoCase(name.c_str());
}

const Field* Table::FieldByNameNoCase(const char* name) const {
	for (size_t i = 0; i < Fields.size(); i++) {
		if (strings::eqnocase(Fields[i].Name, name))
			return &Fields[i];
	}
	return nullptr;
}

const Field* Table::FieldByFriendlyNameNoCase(const char* name) const {
	const Field* f = nullptr;
	for (size_t i = 0; i < Fields.size(); i++) {
		if (strings::eqnocase(Fields[i].FriendlyName, name)) {
			if (f) {
				// not unique
				return nullptr;
			}
			f = &Fields[i];
		}
	}
	return f;
}

size_t Table::FieldIndex(const char* name) const {
	size_t index = 0;
	for (auto& f : Fields) {
		if (f.Name == name)
			return index;
		index++;
	}
	return -1;
}

size_t Table::FieldIndex(const std::string& name) const {
	size_t index = 0;
	for (auto& f : Fields) {
		if (f.Name == name)
			return index;
		index++;
	}
	return -1;
}

std::vector<std::string> Table::FieldNames() const {
	std::vector<std::string> names;
	for (auto& f : Fields)
		names.push_back(f.Name);
	return names;
}

bool Table::RemoveFieldByName(const char* name) {
	auto index = FieldIndex(name);
	if (index == -1)
		return false;
	Fields.erase(Fields.begin() + index);
	return true;
}

bool Table::RemoveFieldByName(const std::string& name) {
	return RemoveFieldByName(name.c_str());
}

Error Table::AddInheritedSchemaFrom(const Table& baseTable) {
	// We add source fields before our fields, because this is designed to 'inherit from' src.
	auto newFields = baseTable.Fields;
	for (const auto& f : Fields)
		newFields.push_back(f);
	Fields = newFields;

	for (const auto& t : baseTable.Tags)
		Tags.insert(t);

	for (const auto& rel : baseTable.Relations)
		Relations.push_back(rel);

	for (const auto& idx : baseTable.Indexes)
		Indexes.push_back(idx);

	if (Network != "" && baseTable.Network != "")
		return Error(tsf::fmt("Base '%s' and child table '%s' both define a Network. Only one may define a network", baseTable.Name.c_str(), Name.c_str()));

	if (baseTable.Network != "") {
		Network       = baseTable.Network;
		NetworkLinkId = baseTable.NetworkLinkId;
		NetworkLinkA  = baseTable.NetworkLinkA;
		NetworkLinkB  = baseTable.NetworkLinkB;
		NetworkNode   = baseTable.NetworkNode;
	}

	return Error();
}

size_t Table::FindRelation(const char* foreignTable, const char* foreignField) const {
	for (size_t i = 0; i < Relations.size(); i++) {
		if (Relations[i].ForeignTable == foreignTable && Relations[i].ForeignField == foreignField)
			return i;
	}
	return -1;
}

void Table::ToString(std::string& s, const std::string& newline) const {
	auto fields = Fields;
	std::stable_sort(fields.begin(), fields.end(), [](const Field& a, const Field& b) -> bool {
		return a.UIGroup < b.UIGroup;
	});

	s += fmt("CREATE TABLE \"%s\" %s", FriendlyName, Name);
	if (InheritedFrom != "")
		s += fmt(" : %s", InheritedFrom);
	s += newline + "{" + newline;

	std::string currentGroup;
	for (const auto& f : fields) {
		if (f.UIGroup != currentGroup) {
			s += "\t\"";
			s += f.UIGroup;
			s += "\t\"" + newline;
			currentGroup = f.UIGroup;
		}
		// optional text field "Field" unit:m tags:disp
		auto line = fmt("\t%s  ", !!(f.Flags & TypeFlags::NotNull) ? "required" : "optional");
		if (f.Width == 0)
			line += fmt("%-16s", FieldTypeToSchemaFileType(f));
		else
			line += fmt("%-16s(%d)", FieldTypeToSchemaFileType(f), f.Width);

		line += fmt("%-35s %-40s", f.Name, "\"" + f.FriendlyName + "\"");

		if (f.UIOrder != 0)
			line += fmt(" uiorder:%v", f.UIOrder);

		if (f.UIDigits != 0)
			line += fmt(" digits:%v", f.UIDigits);

		if (f.Unit != "")
			line += fmt(" unit:%v", f.Unit);

		if (f.Tags.size() != 0) {
			line += " tags:";
			line += strings::Join(f.TagArray(), ",");
		}
		s += strings::TrimRight(line) + newline;
	}

	if (Relations.size() != 0) {
		s += newline;
		for (const auto& rel : Relations) {
			const char* rtype = nullptr;
			switch (rel.Type) {
			case RelationType::OneToOne: rtype = "hasone"; break;
			case RelationType::OneToMany: rtype = "hasmany"; break;
			case RelationType::ManyToOne:
				continue; // assume this will be handled by opposite side. not supported by parser. probably should be, just in case.
			}
			auto line = fmt("%-8s %-30s %s.%s", rel.AccessorName, rel.LocalField, rel.ForeignTable, rel.ForeignField);

			if (rel.UIModule != "")
				line += fmt(" module:%v", rel.UIModule);

			if (rel.UIGroupOrder != "")
				line += fmt(" order:%v", rel.UIGroupOrder);

			s += strings::TrimRight(line);
		}
	}

	// nothing supported here yet
	// UPDATE - just ignore this - this code is used as part of a semi-manual process, so it's OK to
	// just dump all tables, regardless of their flags.
	//IMQS_ASSERT(Flags == TableFlags::None);

	if (Indexes.size() == 0) {
		s += "};" + newline;
	} else {
		s += "}" + newline;
		auto pkey = PrimaryKeyIndex();
		if (pkey != -1)
			s += "PRIMARY KEY(" + strings::Join(Indexes[pkey].Fields, ", ") + ")" + newline;

		for (size_t i = 0; i < Indexes.size(); i++) {
			if (i == pkey)
				continue;
			s += Indexes[i].IsUnique ? "UNIQUE INDEX" : "INDEX";
			s += "(" + strings::Join(Indexes[i].Fields, ", ") + ")" + newline;
		}
		s.erase(s.end() - newline.size()); // erase dangling newline
		s += ";" + newline;
	}
}

bool Table::FieldHasUniqueIndex(const Field& field) const {
	for (const auto& idx : Indexes) {
		if (idx.IsUnique && idx.IsSingleField(field.Name))
			return true;
	}
	return false;
}

size_t Table::AutoIncrementField() const {
	for (size_t i = 0; i < Fields.size(); i++) {
		if (Fields[i].AutoIncrement())
			return i;
	}
	return -1;
}

size_t Table::IntKeyField() const {
	size_t best = -1;
	for (const auto& idx : Indexes) {
		if (idx.IsUnique && idx.Fields.size() == 1) {
			size_t      fi = FieldIndex(idx.Fields[0].c_str());
			const auto& f  = Fields[fi];
			if (f.Type == dba::Type::Int32 || f.Type == dba::Type::Int64) {
				if (f.AutoIncrement())
					return fi;
				else if (best == -1)
					best = fi;
			}
		}
	}
	return best;
}

size_t Table::FirstGeomFieldIndex() const {
	for (size_t i = 0; i < Fields.size(); i++)
		switch (Fields[i].Type) {
		case Type::GeomPoint:
		case Type::GeomPolygon:
		case Type::GeomPolyline:
		case Type::GeomMultiPoint:
		case Type::GeomAny:
			return i;
		default:
			break;
		};
	return -1;
}

size_t Table::FindIndex(const Index& idx) const {
	for (size_t i = 0; i < Indexes.size(); i++) {
		if (Indexes[i].Fields == idx.Fields)
			return i;
	}
	return -1;
}

const Index* Table::FindIndexPtr(const Index& idx) const {
	size_t i = FindIndex(idx);
	return i != -1 ? &Indexes[i] : nullptr;
}

size_t Table::FindIndexForField(const char* fieldName) const {
	for (size_t i = 0; i < Indexes.size(); i++) {
		if (Indexes[i].Fields.size() == 1 && Indexes[i].Fields[0] == fieldName)
			return i;
	}
	return -1;
}

size_t Table::PrimaryKeyIndex() const {
	for (size_t i = 0; i < Indexes.size(); i++) {
		if (Indexes[i].IsPrimary)
			return i;
	}
	return -1;
}

const Index* Table::PrimaryKey() const {
	auto i = PrimaryKeyIndex();
	return i == -1 ? nullptr : &Indexes[i];
}

void Table::SetPrimaryKey(const char* field1, const char* field2, const char* field3) {
	auto i = PrimaryKeyIndex();
	if (i != -1)
		Indexes.erase(Indexes.begin() + i);
	Index ix;
	ix.IsPrimary = true;
	ix.IsUnique  = true;
	ix.Fields.push_back(field1);
	if (field2)
		ix.Fields.push_back(field2);
	if (field3)
		ix.Fields.push_back(field3);
	Indexes.push_back(ix);
}

void Table::SetPrimaryKey(const std::vector<std::string>& fields) {
	auto i = PrimaryKeyIndex();
	if (i != -1)
		Indexes.erase(Indexes.begin() + i);
	Index ix;
	ix.IsPrimary = true;
	ix.IsUnique  = true;
	ix.Fields    = fields;
	Indexes.push_back(ix);
}

bool Table::Equals(const Table& b, SqlDialectFlags myFlags, SqlDialectFlags bFlags) const {
	if (Fields.size() != b.Fields.size())
		return false;

	bool geomSpecific = !!(myFlags & SqlDialectFlags::GeomSpecificFieldTypes) &&
	                    !!(bFlags & SqlDialectFlags::GeomSpecificFieldTypes);

	bool hasGUID = !!(bFlags & SqlDialectFlags::UUID);

	for (const auto& f : Fields) {
		auto fb = b.FieldByName(f.Name.c_str());
		if (!fb)
			return false;
		if (f.Type != fb->Type) {
			bool geomOK = IsTypeGeom(f.Type) && IsTypeGeom(fb->Type) && !geomSpecific;
			bool guidOK = f.Type == Type::Guid && fb->Type == Type::Bin && !hasGUID; // emulated GUID
			if (!(geomOK || guidOK))
				return false;
		}
		// A width of 0 on the src side means we don't care
		if (f.Width != fb->Width && f.Width != 0)
			return false;
	}

	for (const auto& ix : Indexes) {
		size_t bi = b.FindIndex(ix);
		if (bi == -1) {
			// If target doesn't support spatial indexes (ie HANA), then tolerate that being missing
			bool geomOK = ix.Fields.size() == 1 && FieldByName(ix.Fields[0].c_str())->IsTypeGeom() && !(bFlags & SqlDialectFlags::SpatialIndex);
			if (geomOK)
				continue;
			return false;
		}
		if (ix.IsUnique != b.Indexes[bi].IsUnique)
			return false;
		if (ix.IsPrimary != b.Indexes[bi].IsPrimary)
			return false;
	}

	return true;
}

void Table::OverlayOnto(Table& onto) const {
	for (const auto& t : Tags)
		onto.Tags.insert(t);
	onto.Flags |= Flags;

	// this threshold of 30 is pure thumbsuck
	ohash::map<string, size_t> dstFieldMap;
	if (onto.Fields.size() > 30) {
		for (size_t i = 0; i < onto.Fields.size(); i++)
			dstFieldMap.insert(onto.Fields[i].Name, i);
	}

	for (const auto& f : Fields) {
		size_t dstIndex = -1;
		if (dstFieldMap.size() != 0 && dstFieldMap.contains(f.Name))
			dstIndex = dstFieldMap.get(f.Name);
		else
			dstIndex = onto.FieldIndex(f.Name);
		if (dstIndex == -1) {
			// skip fields that don't exist in 'onto'. The typical use case for this function, is to overlay information from
			// a .schema file, onto the actual schema that is read out of a Postgres database. In such a case, we don't want to
			// lie about what's inside the DB.
			continue;
		}
		auto& df = onto.Fields[dstIndex];
		if (f.FriendlyName != "")
			df.FriendlyName = f.FriendlyName;
		if (f.Unit != "")
			df.Unit = f.Unit;
		if (f.UIGroup != "")
			df.UIGroup = f.UIGroup;
		if (f.UIOrder != 0)
			df.UIOrder = f.UIOrder;
		for (const auto& t : f.Tags)
			df.Tags.insert(t);
	}
}

} // namespace schema
} // namespace dba
} // namespace imqs
