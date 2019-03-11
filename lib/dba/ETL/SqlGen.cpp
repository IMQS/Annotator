#include "pch.h"
#include "SqlGen.h"

namespace imqs {
namespace dba {
namespace etl {

IMQS_DBA_API void GenerateInsert(SqlStr& sql, size_t nRecords, const std::string& table, const std::vector<std::string>& fieldNames, const std::vector<Type>& fieldTypes) {
	sql.Fmt("INSERT INTO %Q (", table);
	for (const auto& f : fieldNames)
		sql.Fmt("%Q,", f);
	sql.Chop();
	sql += ") VALUES ";
	size_t param = 1;
	for (size_t i = 0; i < nRecords; i++) {
		sql += "(";
		for (size_t j = 0; j < fieldNames.size(); j++) {
			char buf[10] = {0};
			buf[0]       = '$';
			imqs::ItoA((int) param++, buf + 1, 10);

			if (IsTypeGeom(fieldTypes[j])) {
				sql.Dialect->ST_GeomFrom(sql, buf);
				sql += ",";
			} else {
				sql.Fmt("%v,", buf);
			}
		}
		sql.Chop();
		sql += "),";
	}
	sql.Chop();
}

IMQS_DBA_API Error GenerateUpsert(SqlStr& sql, const std::string& table, const std::vector<std::string>& fields, const std::vector<Type>& fieldTypes, const std::string& conflictField) {
	std::vector<size_t> fieldsToUpdateOnConflictIdx; // List of index's for fields we want to update (orignal list without the conflict field)
	bool                foundConflictField = false;

	// Build the "INSERT" part
	GenerateInsert(sql, 1, table, fields, fieldTypes);

	// Build the "ON CONFLICT DO UPDATE" part
	for (size_t i = 0; i < fields.size(); i++) {
		// In case of a conflict, we need to save the list of fields we want to update and their index (for the $ value below).
		// The conflict field cannot be part of this list.
		// We also need to make sure the conflict field is part of the provided fields otherwise the sql we are building will be wrong
		if (conflictField != fields[i]) {
			fieldsToUpdateOnConflictIdx.push_back(i);
		} else {
			foundConflictField = true;
		}
	}

	if (!foundConflictField)
		return Error("The conflict field must be part of the provided list of fields");

	sql += " ON CONFLICT (";
	sql.Fmt("%Q", conflictField);
	sql += ") DO UPDATE SET ";

	for (const auto idx : fieldsToUpdateOnConflictIdx) {
		sql.Fmt("%Q", fields[idx]);
		sql += " = ";

		char buf[10] = {0};
		buf[0]       = '$';
		size_t param = idx + 1;
		imqs::ItoA((int) param, buf + 1, 10);

		if (IsTypeGeom(fieldTypes[idx])) {
			sql.Dialect->ST_GeomFrom(sql, buf);
			sql += ",";
		} else {
			sql.Fmt("%v,", buf);
		}
	}
	sql.Chop();
	return Error();
}

} // namespace etl
} // namespace dba
} // namespace imqs
