#pragma once

#include "Containers/VarArgs.h"

namespace imqs {
namespace dba {

class Attrib;
class Conn;
class Tx;
class Executor;

class IMQS_DBA_API CrudOps {
public:
	// Insert one record
	// TODO: Improve this API
	static Error Insert(Conn* conn, const std::string& table, std::initializer_list<std::pair<const char*, Attrib>> list);

	// TODO: Improve this API
	static Error InsertSQL(SqlStr& sql, const std::string& table, std::vector<std::pair<const char*, Attrib>> fieldVals);

	// Upsert one or more records. keyField must be present in fields
	static Error Upsert(Executor* ex, const std::string& table, const std::string& keyField, size_t nRecords, const std::vector<std::string>& fields, const Attrib* values);

	// Upsert one record. keyField must be present in fields
	static Error Upsert(Executor* ex, const std::string& table, const std::string& keyField, const std::vector<std::string>& fields, std::initializer_list<Attrib> values);

	// Insert zero or more records. The number of attributes inside 'values' must be equal to fields.size() * nRecords.
	static Error Insert(Executor* ex, const std::string& table, size_t nRecords, const std::vector<std::string>& fields, const Attrib** values);

	// Insert contiguously packed Attrib values. The number of attributes inside 'values' must be equal to fields.size() * nRecords.
	static Error Insert(Executor* ex, const std::string& table, size_t nRecords, const std::vector<std::string>& fields, const Attrib* values);

	// Insert zero or more records. The number of records inserted is the number of attributes / fields.size()
	static Error Insert(Executor* ex, const std::string& table, const std::vector<std::string>& fields, std::initializer_list<Attrib> values);

	// TODO: Improve this API
	static Error UpdateSQL(SqlStr& sql, const std::string& table, std::vector<std::pair<const char*, Attrib>> fieldVals, const char* oidField);

	// DELETE FROM table WHERE field = val
	static Error DeleteByKey(Executor* ex, const std::string& table, const char* field, const Attrib& val);

	// SELECT COUNT(*) FROM table
	static Error Count(Executor* ex, const std::string& table, int64_t& count);

	// Determine whether each given key exists in the table, and return a bit array
	// indicating the presence of that record inside the table. This is intended to be
	// used to build an UPSERT statement.
	// SELECT key FROM table WHERE keyField IN (key[0], key[1] ... key[n-1]) --> As vector<bool>
	static Error CheckExistence(Executor* ex, const std::string& table, const char* keyField, size_t n, const Attrib** values, std::vector<bool>& exists);

	// Run a query that produces a single field, and convert those values to strings, and place them in an array
	static Error QueryStrings(Executor* ex, const char* sql, std::vector<std::string>& strings);

	// Query that produces a single row. Returns ErrEOF if no record found, or ErrNotOneResult if more than one record found.
	template <typename... Args>
	static Error Query(Executor* ex, const char* sql, Args&... args) {
		const auto      num_args = sizeof...(Args);
		varargs::OutArg pack_array[num_args + 1]; // +1 for zero args case
		varargs::PackOutArgs(pack_array, args...);
		return QueryVarArgPack(ex, sql, 0, nullptr, num_args, pack_array);
	}

	// Query that produces a single row. Returns ErrEOF if no record found, or ErrNotOneResult if more than one record found.
	template <typename... Args>
	static Error Query(Executor* ex, const char* sql, std::initializer_list<Attrib> params, Args&... args) {
		const auto      num_args = sizeof...(Args);
		varargs::OutArg pack_array[num_args + 1]; // +1 for zero args case
		varargs::PackOutArgs(pack_array, args...);
		smallvec<const Attrib*> pParams;
		for (size_t i = 0; i < params.size(); i++)
			pParams.push(params.begin() + i);
		return QueryVarArgPack(ex, sql, pParams.size(), &pParams[0], num_args, pack_array);
	}

	static Error QueryVarArgPack(Executor* ex, const char* sql, size_t nParams, const Attrib** params, size_t num_args, varargs::OutArg* pack_array);
};
} // namespace dba
} // namespace imqs