#pragma once

#include "../Types.h"
#include "../Attrib.h"
#include "../Rows.h"
#include "../FlatFiles/FlatFile.h"
#include "PackedColumn.h"

namespace imqs {
namespace dba {

/* Temporary table, built for performing intersection queries across joined tables.

TempTable was built for the CrudServer, which needs to do joins manually, outside of
the database engine. The reason for this, is because of the cartesian product that
SQL databases give us, which can very quickly produce gigantic result sets
if joining values are repeated.

The main idea here is that we store an initial result set once, and then we whittle
that list of records down, until we're left with our final set. By storing an indirection
table (see 'Order'), whittling records down is cheap. One could, without difficulty,
support adding records into this data structure too, but there has not yet been a need
for that.

TempTable supports all field types.

A key aspect of TempTable is that all row-level access goes through the Order field,
which is an array of integers that points into the field columns. This allows us
to build up the initial set of fields once, and then quickly prune rows without
having to shuffle the memory of the attributes.

It also makes sorting the table fast, because we're only rearranging the index.

How to build up a TempTable manually
------------------------------------
TempTable has member functions such as StoreRows and StoreFlatFile, which build up a TempTable
from scratch. If you want to achieve the same thing, but by bringing your own data, from an
arbitrary data source, then you must follow this procedure:

1. Add all of your columns to TempTable using AddField(). New columns go onto the back
	of the Columns vector. Populate each of your new columns. When you're done, every column
	must have the exact same number of records.

2. Call AddRowsClean(). AddRowsClean() will ensure that every column has the same number of
	records inside it, and then it will populate Order, and set IsPopulated = true. If all columns
	do not have the same number of records, or if any column's type is null, then the function
	will return an error.

The above procedure can also be used to add new records to an existing non-empty table. All of
the above conditions apply in this case too.

*/
class IMQS_DBA_API TempTable {
public:
	// Type of indices into records. Someday we may need to support more than 4 billion records in here,
	// but I don't see that happening soon. If that need arises, then we'd like it to be as simple as
	// flipping this to uint64_t.
	typedef uint32_t TIndex;

	std::vector<TIndex>       Order; // Order of records. Points at records inside the columns.
	std::vector<PackedColumn> Columns;
	bool                      IsPopulated = false; // We need to store this, otherwise an empty result is indistinguishable from a table that has not been fetched

	TempTable();
	~TempTable();

	// Store one or more fields into an empty TempTable
	// If there are duplicate column names, then any column after the first
	// is discarded. Column names are case sensitive.
	Error StoreRows(const std::vector<std::string>& fields, Rows& rows, const std::vector<Type>& fieldTypes = {});

	// Store one or more fields from a FlatFile into an empty TempTable
	Error StoreFlatFile(const std::vector<std::string>& fields, FlatFile& ff);

	// Trim down the records in this table, by joining with 'foreign', and only keeping the records that are present in both.
	Error IntersectWith(const std::string& localField, TempTable& foreign, const std::string& foreignField);

	void   Clear();                                                                           // Delete all contents, and set IsPopulated to false
	void   ClearRows();                                                                       // Delete rows, but preserve columns
	bool   Sort(const std::vector<std::string>& fields);                                      // Sort by the fields specified. Result is stored only in 'Order'. Returns false if a field was not found.
	void   SetEmpty();                                                                        // Set IsPopulated to true, and clear Order
	size_t FieldIndex(const std::string& field) const;                                        // Return index of the field, or -1 if not present
	size_t FieldIndex(const char* field) const;                                               // Return index of the field, or -1 if not present
	bool   ResolveFieldName(std::string& field) const;                                        // Try to find the actual field name, by ignoring case. Return true if the field was found.
	void   GetDeepByIndex(size_t field, size_t rec, Attrib& tmp) const;                       // Fetch a temporary attribute that points deep inside our memory
	Attrib GetByIndex(size_t field, size_t rec, Allocator* alloc = nullptr) const;            // Fetch a copy of an attribute
	void   GetDeep(const char* field, size_t rec, Attrib& tmp) const;                         // Fetch a temporary attribute that points deep inside our memory
	bool   IsNullByIndex(size_t field, size_t rec) const;                                     // Returns true if the attribute is null
	bool   IsColumnNull(const std::string& field) const;                                      // Returns true if every record in the column is null. Also returns true when the column does not exist.
	Attrib Get(const char* field, size_t rec, Allocator* alloc = nullptr) const;              // Fetch a copy of an attribute
	void   Set(const char* field, size_t rec, const Attrib& val);                             // Set a value
	void   SetByIndex(size_t field, size_t rec, const Attrib& val);                           // Set a value
	size_t Size() const;                                                                      // Number of records
	bool   RenameField(const std::string& oldname, const std::string& newname);               // Rename a field. Returns false if the old name does not exist, or the new name is already taken.
	void   DeleteField(const std::string& field);                                             // Delete a field
	void   DeleteFields(const std::vector<std::string>& fields);                              // Delete fields
	void   DeleteFieldsExcept(const std::vector<std::string>& keep);                          // Delete all fields other than those in 'keep'
	Error  AddField(const std::string& name, dba::Type type);                                 // Add a field. Return an error only if the field already exists.
	Error  AddField(const std::string& name, dba::PackedColumn&& packedColumn);               // Add a field. Return an error only if the field already exists.
	Error  Add(const char* field, const Attrib& val);                                         // Add a new row to an existing field.
	void   AddByIndex(size_t field, const Attrib& val);                                       // Add a new row to an existing field.
	Error  AddRowsClean();                                                                    // Add rows after populating Columns. See instructions for "How to build up a TempTable manually"
	void   AddRowClean();                                                                     // Add a single row after populating columns.
	Error  AddRow(const std::vector<std::string>& fields, const std::vector<Attrib>& values); // Add one row. Not intended to be fast. For speed, read main instructions.
	void   EraseRows(size_t n, const size_t* rows);                                           // Erase zero or more rows. This involves a memory bump of 'Order', so it is best to batch up calls to EraseRows(). You can also just manipulate Order yourself.
	void   EraseRows(const std::vector<size_t>& rows);                                        // Erase zero or more rows. This involves a memory bump of 'Order', so it is best to batch up calls to EraseRows(). You can also just manipulate Order yourself.

	// Add a new row, with exactly as many attributes as there are fields
	template <typename... Args>
	Error AddFullRow(const Args&... args) {
		const auto   num_args = sizeof...(Args);
		varargs::Arg pack_array[num_args + 1]; // +1 for zero args case
		varargs::PackArgs(pack_array, args...);
		return AddVarArgPack(num_args, pack_array);
	}

	std::vector<std::string> FieldNames() const;
	std::vector<dba::Type>   FieldTypes() const;

private:
	// Mapping from field name to index in Columns + 1 (so zero is invalid).
	// We use our own string class, because that allows us to perform a hash table lookup
	// from a "const char*" without performing any memory allocations. If we use
	// std::string, then we need to make a temporary std::string for that purpose,
	// when performing a lookup.
	ohash::map<imqs::StaticString, size_t> NameToIndex;
	TIndex                                 NextRow = 0; // The index of the next row that we'll add

	Error AddVarArgPack(size_t n, const varargs::Arg* args);
};

inline size_t TempTable::FieldIndex(const std::string& field) const {
	StaticString tmp;
	tmp.Z      = const_cast<char*>(field.c_str());
	size_t res = NameToIndex.get(tmp) - 1;
	tmp.Z      = nullptr;
	return res;
}

inline size_t TempTable::FieldIndex(const char* field) const {
	StaticString tmp;
	tmp.Z      = const_cast<char*>(field);
	size_t res = NameToIndex.get(tmp) - 1;
	tmp.Z      = nullptr;
	return res;
}

inline bool TempTable::IsNullByIndex(size_t field, size_t rec) const {
	return Columns[field].IsNull(Order[rec]);
}

inline void TempTable::GetDeepByIndex(size_t field, size_t rec, Attrib& tmp) const {
	Columns[field].GetDeep(Order[rec], tmp);
}

inline Attrib TempTable::GetByIndex(size_t field, size_t rec, Allocator* alloc) const {
	return Columns[field].Get(Order[rec], alloc);
}

inline void TempTable::GetDeep(const char* field, size_t rec, Attrib& tmp) const {
	size_t i = FieldIndex(field);
	if (i == -1)
		return;
	GetDeepByIndex(i, rec, tmp);
}

inline Attrib TempTable::Get(const char* field, size_t rec, Allocator* alloc) const {
	size_t i = FieldIndex(field);
	if (i == -1)
		return Attrib();
	return GetByIndex(i, rec);
}

inline void TempTable::Set(const char* field, size_t rec, const Attrib& val) {
	size_t i = FieldIndex(field);
	if (i == -1)
		return;
	Columns[i].Set(Order[rec], val);
}

inline void TempTable::SetByIndex(size_t field, size_t rec, const Attrib& val) {
	Columns[field].Set(Order[rec], val);
}

inline size_t TempTable::Size() const {
	return Order.size();
}

inline Error TempTable::Add(const char* field, const Attrib& val) {
	size_t i = FieldIndex(field);
	if (i == -1)
		return Error::Fmt("TempTable.Add: Field '%v' not found", field);
	bool ok = Columns[i].Add(val);
	IMQS_ASSERT(ok);
	return Error();
}

inline void TempTable::AddByIndex(size_t field, const Attrib& val) {
	bool ok = Columns[field].Add(val);
	IMQS_ASSERT(ok);
}

inline bool TempTable::IsColumnNull(const std::string& field) const {
	size_t i = FieldIndex(field);
	if (i == -1)
		return true;
	return Columns[i].IsAllNull();
}

} // namespace dba
} // namespace imqs
