#pragma once

#include "../SqlStr.h"
#include "../Types.h"

namespace imqs {
namespace dba {
namespace etl {

// Generate an INSERT statement.
// If nRecords is greater than 1, then the statement is of the form VALUES ($1,$2,$3),($4,$5,$6),...nRecords.
// That multi-value form is accepted by Postgres as a mechanism for inserting multiple rows into the database
// with a single statement.
// If a field type is geometry, then the function will wrap the parameter in ST_GeomFrom() for the dialect of the
// SqlStr provided.
IMQS_DBA_API void GenerateInsert(SqlStr& sql, size_t nRecords, const std::string& table, const std::vector<std::string>& fieldNames, const std::vector<Type>& fieldTypes);

// Generate an UPSERT statement
// If conflictField == "identity", the output looks like:
// INSERT INTO "Theme" ("identity","data") VALUES ($1,$2) ON CONFLICT ("identity") DO UPDATE SET ("data") = ($2)
IMQS_DBA_API Error GenerateUpsert(SqlStr& sql, const std::string& table, const std::vector<std::string>& fields, const std::vector<Type>& fieldTypes, const std::string& conflictField);
} // namespace etl
} // namespace dba
} // namespace imqs