#pragma once

#include "Field.h"

/* 
I initially used imqs::String here, and imqs::cheapvec, in the fear that heap allocations of std::string and
std::vector would dominate the time required to build up a schema. I then switched back to std::vector
and std::string, and I was unable to spot a difference with the eye. Both measurements, for reading a large-ish
schema (1500 tables), were around 240 milliseconds, give or take 5 milliseconds. So I'm sticking with the
std stuff in the name of standardization. [BMH 2016-09-22]
*/

namespace imqs {
namespace dba {
namespace schema {

class DB;

enum class RelationType {
	OneToOne,
	OneToMany,
	ManyToOne,
};

// A table-table relation inside a DB schema
class IMQS_DBA_API Relation {
public:
	RelationType Type = RelationType::OneToOne;
	std::string  AccessorName;
	std::string  LocalField;
	std::string  ForeignTable;
	std::string  ForeignField;
	std::string  UIGroupOrder;
	std::string  UIModule;
	bool         operator==(const Relation& b) const;
	bool         operator!=(const Relation& b) const;
};

// An index inside a DB schema (ie BTree, Spatial, etc)
// Wherever we can, we automatically infer IsSpatial. However, there are certain code paths
// where it's impossible to do that, so if it's clear that the code doesn't have appropriate
// context to know whether fields are geometry, then you need to toggle IsSpatial = true manually.
// Note also, that when reading schema out of a database, IsSpatial is not necessarily toggled.
// IsSpatial is particularly needed for the schema creation path inside SchemaWriter. It is
// valuable to be able to have a function called SchemaWriter::CreateIndex, which doesn't need any
// other context.
class IMQS_DBA_API Index {
public:
	std::string              Name;
	std::vector<std::string> Fields;
	bool                     IsPrimary = false;
	bool                     IsUnique  = false;
	bool                     IsSpatial = false; // This is not necessarily populated by schema readers. Ignored by operator==
	bool                     operator==(const Index& b) const;
	bool                     operator!=(const Index& b) const;
	bool                     IsSingleField(std::string fieldName) const;
	std::string              Description() const; // Returns a summary of this index, which includes it's field names, and whether it's primary and unique
};

enum class TableFlags {
	None             = 0,
	Internal         = 1, // Internal to the database, such as spatial_ref_sys inside PostGIS
	View             = 2,
	MaterializedView = 4,
	SuffixBase       = 8,  // This is a defining table. There are a bunch of tables with our name, suffixed with _SOMETHING, with the exact same structure.
	SuffixLeaf       = 16, // This is one of many of the same. They all have the exact same structure as ONE table without the suffix, that is marked as SuffixBase.
	Temp             = 32, // This is a temporary table (for use in schema creation by code - eg CREATE TEMP TABLE ...)
};

inline uint32_t    operator&(TableFlags a, TableFlags b) { return (uint32_t) a & (uint32_t) b; }
inline TableFlags& operator|=(TableFlags& a, TableFlags b) {
	a = (TableFlags)((uint32_t) a | (uint32_t) b);
	return a;
}

// A table inside a DB schema
// Use caution when copying Table objects, because of the private member 'Owner' that gets copied too.
// Use Clone() to make a copy of a table with the owner set to null.
class IMQS_DBA_API Table {
public:
	friend class DB;

	std::string             FriendlyName;
	std::vector<Field>      Fields;
	std::vector<Relation>   Relations;
	std::vector<Index>      Indexes;
	TableFlags              Flags = TableFlags::None;
	ohash::set<std::string> Tags; // Much like Flags, but just a bag of strings, so extendable outside of dba

	// It's conceivable that a table is a member of more than one network.
	// This design allows a table to be a member of at most one network.
	std::string Network;
	std::string NetworkLinkId;
	std::string NetworkLinkA;
	std::string NetworkLinkB;
	std::string NetworkNode;

	// Used during schema parsing
	std::string InheritedFrom;

	Table*                   CloneNew() const; // Return a clone with Owner set to null
	Table                    Clone() const;    // Return a clone with Owner set to null
	const std::string&       GetName() const { return Name; }
	bool                     SetName(const std::string& name); // Returns false if a table with this name already exists in the owning DB
	std::string              FriendlyNameOrName() const;       // Return FriendlyName if not empty, otherwise Name
	Field*                   FieldByName(const char* name);
	Field*                   FieldByName(const std::string& name);
	const Field*             FieldByName(const char* name) const;
	const Field*             FieldByName(const std::string& name) const;
	const Field*             FieldByNameNoCase(const char* name) const;
	const Field*             FieldByNameNoCase(const std::string& name) const;
	Field*                   FieldByName(const char* name, bool createIfNotExist);
	const Field*             FieldByFriendlyNameNoCase(const char* name) const; // Returns a field only if the friendly name is unique
	size_t                   FieldIndex(const char* name) const;                // Returns nullptr if not found
	size_t                   FieldIndex(const std::string& name) const;         // Returns nullptr if not found
	std::vector<std::string> FieldNames() const;
	bool                     RemoveFieldByName(const char* name);
	bool                     RemoveFieldByName(const std::string& name);
	Error                    AddInheritedSchemaFrom(const Table& baseTable);
	size_t                   FindRelation(const char* foreignTable, const char* foreignField) const;
	bool                     FieldHasUniqueIndex(const Field& field) const;
	void                     ToString(std::string& s, const std::string& newline) const;
	size_t                   PrimaryKeyIndex() const; // Returns the index of the primary key inside "Indexes", or -1 if there is no such index
	const Index*             PrimaryKey() const;      // Returns null if none exists
	void                     SetPrimaryKey(const char* field1, const char* field2 = nullptr, const char* field3 = nullptr);
	void                     SetPrimaryKey(const std::vector<std::string>& fields);
	size_t                   AutoIncrementField() const; // Returns the index of the first field that is an auto-incremented integer, with a unique index on it.
	size_t                   IntKeyField() const;        // Returns the index of the first field that is an integer, with a unique index on it. Prioritizes autoincrement fields.
	size_t                   FirstGeomFieldIndex() const;
	size_t                   FindIndex(const Index& idx) const;              // Looks ONLY at the field names. Ignores Primary, Unique, Name.
	const Index*             FindIndexPtr(const Index& idx) const;           // Looks ONLY at the field names. Ignores Primary, Unique, Name.
	size_t                   FindIndexForField(const char* fieldName) const; // Find single-field index that covers just this field
	bool                     Equals(const Table& b, SqlDialectFlags myFlags, SqlDialectFlags bFlags) const;
	void                     OverlayOnto(Table& onto) const;

	bool IsInternal() const { return !!(Flags & TableFlags::Internal); }
	bool IsView() const { return !!(Flags & TableFlags::View); }
	bool IsMaterializedView() const { return !!(Flags & TableFlags::MaterializedView); }
	bool IsTemp() const { return !!(Flags & TableFlags::Temp); }

private:
	// Because our Name is indexed by DB, we need to maintain the link to our DB so that
	// when our name is changed, we update the index (ie a hash table inside DB).
	DB*         Owner = nullptr;
	std::string Name;
};
} // namespace schema
} // namespace dba
} // namespace imqs
