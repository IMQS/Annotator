#include "pch.h"
#include "TempTable.h"
#include "AttribSet.h"

using namespace std;

namespace imqs {
namespace dba {

TempTable::TempTable() {
}

TempTable::~TempTable() {
}

void TempTable::Clear() {
	Order.clear();
	Columns.clear();
	IsPopulated = false;
	NameToIndex.clear();
	NextRow = 0;
}

void TempTable::ClearRows() {
	Order.clear();
	NextRow = 0;
	for (auto& c : Columns)
		c.Clear();
}

Error TempTable::StoreRows(const std::vector<std::string>& fields, Rows& rows, const std::vector<Type>& fieldTypes) {
	if (IsPopulated)
		return Error("StoreRows cannot be used on an already populated TempTable");

	auto   queryCols = rows.GetColumns();
	size_t nfield    = std::max(fields.size(), queryCols.size());

	for (size_t i = 0; i < nfield; i++) {
		Columns.push_back(PackedColumn());
		if (i < fields.size())
			NameToIndex.insert(fields[i], i + 1);
		else
			NameToIndex.insert(queryCols[i].Name, i + 1);

		if (i < fieldTypes.size()) {
			Columns[i].SetType(fieldTypes[i]);
		} else {
			// This branch is trickier than you'd think.
			// When reading Sqlite databases, the rows.GetColumns() call cannot determine the exact type of
			// certain types of fields (specifically GUIDs, and Geometry). Those fields just come through
			// as "bin" when we ask for the column types of the result set. It is only when we start actually
			// retrieving the values, that we get a concrete type out.
			// For this reason, we avoid setting the column type explicitly, and rather let PackedColumn
			// give itself a type, when it sees the first non-null object.
			// However, we make a special exception for GeomAny. In the case of GeomAny, we don't want the
			// PackedColumn to pick the first concrete geometry type it sees (eg Point). So in this case,
			// we rather inform it to expect any type of geometry up front.
			// Another alternative would be to add special logic inside PackedColumn, so that it can transition
			// from a concrete geometry type such as Point, to GeomAny, if it seems a combination of different
			// geometry types coming in.
			if (queryCols[i].Type == Type::GeomAny)
				Columns[i].SetType(queryCols[i].Type);
		}
	}

	for (auto row : rows) {
		for (size_t i = 0; i < nfield; i++) {
			if (!Columns[i].Add(row[i]))
				return Error::Fmt("Out of memory storing result in temporary table (%v records)", NextRow);
		}
		Order.push_back(NextRow++);
	}
	if (!rows.OK())
		return rows.Err();

	IsPopulated = true;

	return Error();
}

Error TempTable::StoreFlatFile(const std::vector<std::string>& fields, FlatFile& ff) {
	if (IsPopulated)
		return Error("StoreFlatFile cannot be used on an already populated TempTable");

	size_t         nfield   = fields.size();
	auto           ffFields = ff.Fields();
	vector<size_t> ffIndex; // index of fields[i] in FlatFile

	for (size_t i = 0; i < nfield; i++) {
		for (size_t j = 0; j < ffFields.size(); j++) {
			if (strings::eqnocase(ffFields[j].Name, fields[i])) {
				ffIndex.push_back(j);
				break;
			}
		}
		if (ffIndex.size() != i + 1)
			return Error::Fmt("Unable to find field '%v' in FlatFile", fields[i]);
		Columns.push_back(PackedColumn());
		NameToIndex.insert(fields[i], i + 1);
	}

	OnceOffAllocator alloc;
	size_t           nrows = ff.RecordCount();
	for (size_t row = 0; row < nrows; row++) {
		for (size_t i = 0; i < nfield; i++) {
			alloc.Reset();
			Attrib val;
			auto   err = ff.Read(ffIndex[i], row, val, &alloc);
			if (!err.OK())
				return Error::Fmt("Error reading %v at record %v, from FlatFile: %v", fields[i], row, err.Message());
			if (!Columns[i].Add(val))
				return Error::Fmt("Out of memory storing result in temporary table (%v/%v records)", row, nrows);
		}
		Order.push_back(NextRow++);
	}

	IsPopulated = true;

	return Error();
}

Error TempTable::IntersectWith(const std::string& localField, TempTable& foreign, const std::string& foreignField) {
	size_t localFieldIndex   = FieldIndex(localField);
	size_t foreignFieldIndex = foreign.FieldIndex(foreignField);
	IMQS_ASSERT(localFieldIndex != -1);
	IMQS_ASSERT(foreignFieldIndex != -1);

	// Build a hash table on the entries in foreignField
	AttribSet fSet;
	// We do a little trick here, to avoid having to make copies of the attribute internals - specifically
	// the text, binary, and guid content. By doing a hard memcpy to duplicate the temporary attributes,
	// we avoid the copy. We know we can do this, because of the way Attrib works internally. The temporary
	// Attrib objects that Get() yields already have pointers that point directly into our Container.Data,
	// so here we're just propagating those deep pointers even further.
	Attrib* fList = (Attrib*) malloc(sizeof(Attrib) * foreign.Size());
	if (!fList)
		return Error::Fmt("Out of memory allocating attributes for hash table for TempTable.IntersectWith");

	for (size_t i = 0; i < foreign.Size(); i++) {
		Attrib tmp;
		foreign.GetDeepByIndex(foreignFieldIndex, i, tmp);
		memcpy(&fList[i], &tmp, sizeof(Attrib));
	}
	for (size_t i = 0; i < foreign.Size(); i++)
		fSet.InsertNoCopy(&fList[i]);

	// Iterate over our records, and reject any that aren't present in the foreign table
	decltype(Order) newOrder;
	for (size_t i = 0; i < Size(); i++) {
		Attrib tmp;
		GetDeepByIndex(localFieldIndex, i, tmp);
		if (tmp.IsNull() || !fSet.Contains(tmp))
			continue;
		newOrder.push_back((TIndex) i);
	}
	Order = newOrder;
	free(fList);
	return Error();
}

bool TempTable::Sort(const std::vector<std::string>& fields) {
	const size_t maxFields = 30;
	size_t       ncol      = fields.size();
	if (ncol > maxFields)
		return false;

	PackedColumn*  colsStatic[maxFields];
	PackedColumn** cols = colsStatic;
	for (size_t i = 0; i < ncol; i++) {
		size_t idx = FieldIndex(fields[i]);
		if (idx == -1)
			return false;
		cols[i] = &Columns[i];
	}

	pdqsort(Order.begin(), Order.end(), [ncol, cols](TIndex a, TIndex b) -> bool {
		for (size_t i = 0; i < ncol; i++) {
			Attrib tmpA, tmpB;
			cols[i]->GetDeep(a, tmpA);
			cols[i]->GetDeep(b, tmpB);
			if (tmpA != tmpB)
				return tmpA < tmpB;
		}
		return false;
	});

	return true;
}

void TempTable::SetEmpty() {
	IsPopulated = true;
	Order.clear();
	for (auto& c : Columns)
		c.Clear();
	NextRow = 0;
}

bool TempTable::ResolveFieldName(std::string& field) const {
	if (NameToIndex.contains(field))
		return true;
	for (const auto& f : FieldNames()) {
		if (strings::eqnocase(f, field)) {
			field = f;
			return true;
		}
	}
	return false;
}

bool TempTable::RenameField(const std::string& oldname, const std::string& newname) {
	size_t index = NameToIndex.get(oldname);
	if (index == 0) {
		// oldname not found
		return false;
	}

	if (NameToIndex.get(newname) != 0) {
		// newname already exists
		return false;
	}

	NameToIndex.erase(oldname);
	NameToIndex.insert(newname, index);
	return true;
}

void TempTable::DeleteField(const std::string& field) {
	DeleteFields({field});
}

void TempTable::DeleteFields(const std::vector<std::string>& fields) {
	if (fields.size() == 0)
		return;

	auto names = FieldNames();

	// map names to indexes
	vector<size_t> indexes;
	for (const auto& f : fields) {
		size_t index = FieldIndex(f);
		if (index != -1)
			indexes.push_back(index);
	}
	if (indexes.size() == 0)
		return;

	// delete in reverse order, so that indexes remain stable
	sort(indexes.begin(), indexes.end());
	reverse(indexes.begin(), indexes.end());

	for (const auto& idx : indexes) {
		Columns.erase(Columns.begin() + idx);
		names.erase(names.begin() + idx);
	}

	// rebuild NameToIndex
	NameToIndex.clear_noalloc();
	for (size_t i = 0; i < names.size(); i++) {
		NameToIndex.insert(names[i], i + 1);
	}
}

void TempTable::DeleteFieldsExcept(const std::vector<std::string>& keep) {
	auto                       fields = FieldNames();
	std::vector<std::string>   toDelete;
	std::map<std::string, int> keepMap;
	for (auto f : keep) {
		keepMap[f] = 1;
	}

	for (auto f : fields) {
		if (keepMap[f] != 1)
			toDelete.push_back(f);
	}

	return DeleteFields(toDelete);
}

Error TempTable::AddField(const std::string& name, dba::Type type) {
	if (FieldIndex(name) != -1)
		return Error::Fmt("TempTable.AddField failed - %v already exists", name);
	size_t index = Columns.size();
	Columns.push_back(PackedColumn());
	auto& f = Columns.back();
	f.SetType(type);
	if (!f.GrowTo(NextRow)) {
		auto msg = tsf::fmt("Out of memory adding field %v to TempTable", name);
		IMQS_DIE_MSG(msg.c_str());
	}
	NameToIndex.insert(name, index + 1);
	return Error();
}

Error TempTable::AddField(const std::string& name, dba::PackedColumn&& packedColumn) {
	if (FieldIndex(name) != -1)
		return Error::Fmt("TempTable.AddField failed - %v already exists", name);
	size_t index = Columns.size();
	Columns.push_back(std::move(packedColumn));
	NameToIndex.insert(name, index + 1);
	return Error();
}

Error TempTable::AddRowsClean() {
	if (Columns.size() == 0)
		return Error();
	for (size_t i = 0; i < Columns.size(); i++) {
		if (Columns[i].Type() == Type::Null) {
			auto names = FieldNames();
			return Error::Fmt("TempTable column %v has null type", names[i]);
		}
		if (Columns[i].Size() != Columns[0].Size()) {
			auto names = FieldNames();
			return Error::Fmt("TempTable column size mismatch. %v:%v, %v:%v", names[0], Columns[0].Size(), names[i], Columns[i].Size());
		}
	}
	size_t nNew = Columns[0].Size() - NextRow;
	for (size_t i = 0; i < nNew; i++) {
		Order.push_back(NextRow++);
	}
	IsPopulated = true;
	return Error();
}

Error TempTable::AddRow(const std::vector<std::string>& fields, const std::vector<Attrib>& values) {
	static_assert(sizeof(bool) == sizeof(int8_t), "sizeof(bool) != sizeof(int8)");
	const size_t   maxStaticTouched = 128;
	bool           staticTouched[maxStaticTouched];
	bool*          touched = staticTouched;
	vector<int8_t> dynTouched; // vector<bool> is specialized and doesn't use a byte per entry. see above static assert
	if (Columns.size() > maxStaticTouched) {
		dynTouched.resize(Columns.size());
		touched = (bool*) dynTouched.data();
	}
	memset(touched, 0, Columns.size());

	for (size_t i = 0; i < fields.size(); i++) {
		size_t fi = FieldIndex(fields[i]);
		if (fi == -1)
			return Error::Fmt("TempTable.AddRow: Field '%v' not found", fields[i]);
		touched[fi] = true;
		Add(fields[i].c_str(), values[i]);
	}
	// add null values for all columns not present in 'fields'
	for (size_t i = 0; i < Columns.size(); i++) {
		if (!touched[i]) {
			bool ok = Columns[i].Add(Attrib());
			IMQS_ASSERT(ok);
		}
	}
	Order.push_back(NextRow++);
	return Error();
}

void TempTable::EraseRows(size_t n, const size_t* rows) {
	if (n == 0)
		return;
	if (n == 1) {
		Order.erase(Order.begin() + rows[0]);
		return;
	}
	vector<size_t> sortedRows;
	sortedRows.resize(n);
	for (size_t i = 0; i < n; i++)
		sortedRows[i] = rows[i];
	pdqsort(sortedRows.begin(), sortedRows.end());

	vector<TIndex> newOrder;
	// Add the rows that may have been erased
	size_t i = 0;
	size_t j = 0;
	for (; i < Order.size() && j < sortedRows.size(); i++) {
		if (i == sortedRows[j])
			j++; // skip
		else
			newOrder.push_back((TIndex) i);
	}

	// Add the remaining rows that we know are not erased (because j hit the limit)
	for (; i < Order.size(); i++)
		newOrder.push_back((TIndex) i);

	std::swap(Order, newOrder);
}

void TempTable::EraseRows(const std::vector<size_t>& rows) {
	if (rows.size() == 0)
		return;
	EraseRows(rows.size(), &rows[0]);
}

vector<string> TempTable::FieldNames() const {
	vector<string> names;
	names.resize(Columns.size());
	for (const auto& p : NameToIndex)
		names[p.second - 1] = p.first.Z;
	return names;
}

vector<dba::Type> TempTable::FieldTypes() const {
	vector<dba::Type> types;
	types.resize(Columns.size());
	for (size_t i = 0; i < Columns.size(); i++)
		types[i] = Columns[i].Type();
	return types;
}

Error TempTable::AddVarArgPack(size_t n, const varargs::Arg* args) {
	if (n != Columns.size())
		return Error::Fmt("TempTable.AddFullRow expected %v parameters, but only got %v", Columns.size(), n);
	for (size_t i = 0; i < n; i++) {
		Attrib val;
		args[i].ToAttribDeep(val);
		bool ok = Columns[i].Add(val);
		IMQS_ASSERT(ok);
	}
	Order.push_back(NextRow++);

	return Error();
}

} // namespace dba
} // namespace imqs
