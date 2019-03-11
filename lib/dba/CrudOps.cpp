#include "pch.h"
#include "Attrib.h"
#include "Conn.h"
#include "CrudOps.h"
#include "Stmt.h"
#include "Tx.h"
#include "Rows.h"
#include "ETL/SqlGen.h"

namespace imqs {
namespace dba {

Error CrudOps::Insert(Conn* conn, const std::string& table, std::initializer_list<std::pair<const char*, Attrib>> list) {
	SqlStr sql = conn->Sql();
	InsertSQL(sql, table, list);

	return conn->Exec(sql);
}

Error CrudOps::InsertSQL(SqlStr& sql, const std::string& table, std::vector<std::pair<const char*, imqs::dba::Attrib>> fieldVals) {
	std::string valueSQL = "";
	sql.Clear();
	sql.Fmt("INSERT INTO %Q (", table);
	for (auto listItem : fieldVals) {
		sql.Fmt("%Q,", listItem.first);
		valueSQL += listItem.second.ToSQLString() + ",";
	}
	sql.Chop();
	valueSQL = valueSQL.substr(0, valueSQL.length() - 1);

	sql.Fmt(") VALUES(%v)", valueSQL);
	return Error();
}

// Given a contiguous set of values, which form consecutive records, extract the types
// of those values, assuming that the types are the same for each record. If a value if null,
// then keep going onto the next record, until we find the first attribute that is non-null.
// If all attributes for a particular column are null, then assign a type of Int32.
static void ExtractParamTypes(size_t nRecords, size_t stride, const Attrib** values, Type* types) {
	for (size_t i = 0; i < stride; i++)
		types[i] = Type::Null;

	{
		size_t i      = 0;
		size_t remain = stride;
		for (size_t rec = 0; rec < nRecords; rec++, i += stride) {
			for (size_t j = 0; j < stride; j++) {
				if (types[j] != Type::Null)
					continue;
				if (!values[i + j]->IsNull()) {
					types[j] = values[i + j]->Type;
					remain--;
					if (remain == 0)
						return;
				}
			}
		}
	}

	// fill in defaults of int32
	for (size_t i = 0; i < stride; i++) {
		if (types[i] == Type::Null)
			types[i] = Type::Int32;
	}
}

// Copy the types from the first 'stride' elements, so that a total of 'copies' number of records exists.
static void DuplicateParamTypes(size_t stride, size_t copies, Type* types) {
	for (size_t i = 1; i < copies; i++) {
		size_t k = i * stride;
		for (size_t j = 0; j < stride; j++)
			types[k + j] = types[j];
	}
}

Error CrudOps::Upsert(Executor* ex, const std::string& table, const std::string& keyField, size_t nRecords, const std::vector<std::string>& fields, const Attrib* values) {
	if (nRecords == 0)
		return Error();

	std::vector<Type> types;
	size_t            nParams = fields.size();
	types.resize(nParams);
	std::vector<const Attrib*> val;
	size_t                     n = nRecords * nParams;
	val.resize(n);

	for (size_t i = 0; i < n; i++)
		val[i] = &values[i];

	ExtractParamTypes(nRecords, nParams, &val[0], &types[0]);

	SqlStr sql = ex->Sql();
	auto   err = etl::GenerateUpsert(sql, table, fields, types, keyField);

	if (!err.OK())
		return err;

	Stmt stmt;
	err = ex->Prepare(sql, nParams, &types[0], stmt);
	for (size_t i = 0; i < nRecords; i++) {
		err = stmt.Exec(nParams, &val[0] + i * nParams);
		if (!err.OK())
			return err;
	}
	return err;
}

Error CrudOps::Upsert(Executor* ex, const std::string& table, const std::string& keyField, const std::vector<std::string>& fields, std::initializer_list<Attrib> values) {
	return Upsert(ex, table, keyField, 1, fields, values.begin());
}

Error CrudOps::Insert(Executor* ex, const std::string& table, size_t nRecords, const std::vector<std::string>& fields, const Attrib** values) {
	if (nRecords == 0)
		return Error();
	SqlStr sql            = ex->Sql();
	size_t maxQueryParams = std::min((size_t) Stmt::MaxParams, nRecords * fields.size());
	if (!(sql.Dialect->Flags() & SqlDialectFlags::MultiRowInsert))
		maxQueryParams = fields.size();
	size_t maxRecordsPerStmt = maxQueryParams / fields.size();

	// Figure out the types of the fields
	std::vector<Type> types;
	types.resize(maxQueryParams);
	ExtractParamTypes(nRecords, fields.size(), values, &types[0]);
	// ExtractParamTypes will only populate the first 'fields' elements.
	// We need to duplicate those types into all of the other records.
	DuplicateParamTypes(fields.size(), maxRecordsPerStmt, &types[0]);

	size_t nRecRemaining      = nRecords;
	size_t prevRecordsPerStmt = 0;
	size_t iValue             = 0;
	Stmt   stmt;
	while (nRecRemaining != 0) {
		size_t recordsPerStmt = Stmt::MaxParams / fields.size();
		if (!(sql.Dialect->Flags() & SqlDialectFlags::MultiRowInsert))
			recordsPerStmt = 1;
		recordsPerStmt       = std::min(recordsPerStmt, nRecRemaining);
		size_t paramsPerStmt = recordsPerStmt * fields.size();

		if (recordsPerStmt != prevRecordsPerStmt) {
			sql.Clear();
			/*
			sql.Fmt("INSERT INTO %Q (", table);
			for (const auto& f : fields)
				sql.Fmt("%Q,", f);
			sql.Chop();
			sql += ") VALUES ";
			size_t param = 1;
			for (size_t i = 0; i < recordsPerStmt; i++) {
				sql += "(";
				for (size_t j = 0; j < fields.size(); j++)
					sql.Fmt("$%d,", param++);
				sql.Chop();
				sql += "),";
			}
			sql.Chop();
			*/
			etl::GenerateInsert(sql, recordsPerStmt, table, fields, types);

			auto err = ex->Prepare(sql, recordsPerStmt * fields.size(), &types[0], stmt);
			if (!err.OK())
				return err;
			prevRecordsPerStmt = recordsPerStmt;
		}

		auto err = stmt.Exec(paramsPerStmt, values + iValue);
		if (!err.OK())
			return err;
		iValue += paramsPerStmt;
		nRecRemaining -= recordsPerStmt;
	}
	return Error();
}

Error CrudOps::Insert(Executor* ex, const std::string& table, size_t nRecords, const std::vector<std::string>& fields, const Attrib* values) {
	std::vector<const Attrib*> p;
	size_t                     n = nRecords * fields.size();
	p.resize(n);
	for (size_t i = 0; i < n; i++)
		p[i] = &values[i];
	return Insert(ex, table, nRecords, fields, &p[0]);
}

Error CrudOps::Insert(Executor* ex, const std::string& table, const std::vector<std::string>& fields, std::initializer_list<Attrib> values) {
	IMQS_ASSERT(values.size() % fields.size() == 0);
	std::vector<const Attrib*> p;
	p.reserve(values.size());
	for (const auto& val : values)
		p.push_back(&val);
	return Insert(ex, table, values.size() / fields.size(), fields, &p[0]);
}

Error CrudOps::UpdateSQL(SqlStr& sql, const std::string& table, std::vector<std::pair<const char*, imqs::dba::Attrib>> fieldVals, const char* oidField) {
	sql.Clear();
	sql.Fmt("UPDATE %Q SET ", table);
	std::string oidFieldVal = "";
	for (auto listItem : fieldVals) {
		if (strcmp(listItem.first, oidField) == 0) {
			oidFieldVal = listItem.second.ToSQLString();
			continue;
		}
		sql.Fmt("%Q = %v,", listItem.first, listItem.second.ToSQLString());
	}
	sql.Chop();
	sql.Fmt(" WHERE %Q = %v", oidField, oidFieldVal);
	return Error();
}

Error CrudOps::DeleteByKey(Executor* ex, const std::string& table, const char* field, const Attrib& val) {
	auto s = ex->Sql();
	s.Fmt("DELETE FROM %Q WHERE %Q = $1", table, field);
	return ex->Exec(s, {val});
}

Error CrudOps::Count(Executor* ex, const std::string& table, int64_t& count) {
	auto s = ex->Sql();
	s.Fmt("SELECT COUNT(*) FROM %Q", table);
	return Query(ex, s, count);
}

Error CrudOps::CheckExistence(Executor* ex, const std::string& table, const char* keyField, size_t n, const Attrib** values, std::vector<bool>& exists) {
	// Unfortunately we need to build a lookup table going from 'value' to 'index in values'.
	// It feels like there must be a smarter way of doing this. I believe the right way to do
	// it is with a LEFT OUTER join on the key field, into a temporary table containing
	// the keys inside 'values'. However, I can't find a cross-platform way of doing that.
	// It's a pity that SQL has no notion of a universal literal table syntax. That would have
	// been such a useful thing.
	ohash::map<const Attrib*, size_t, AttribPtrGetHashCode> valToIndex;
	for (size_t i = 0; i < n; i++)
		valToIndex.insert(values[i], i);

	exists.resize(n, false);

	auto   s       = ex->Sql();
	size_t s_batch = 0;
	for (size_t i = 0; i < n;) {
		size_t nbatch = std::min(n - i, s.Dialect->MaxQueryParams());
		if (nbatch != s_batch) {
			s_batch = nbatch;
			s.Clear();
			s.Fmt("SELECT %Q FROM %Q WHERE %Q IN (", keyField, table, keyField);
			for (size_t j = 0; j < nbatch; j++)
				s.Fmt("$%v,", j + 1);
			s.Chop();
			s += ")";
		}

		auto rows = ex->Query(s, nbatch, values + i);
		for (auto row : rows) {
			size_t idx = valToIndex.get(&row[0]);
			IMQS_ASSERT(idx != -1);
			exists[idx] = true;
		}
		if (!rows.OK())
			return rows.Err();
		i += nbatch;
	}
	return Error();
}

Error CrudOps::QueryStrings(Executor* ex, const char* sql, std::vector<std::string>& strings) {
	auto rows = ex->Query(sql);
	if (rows.ColumnCount() != 1)
		return Error("CrudOps::QueryStrings used on a query that produces more than one column");

	for (auto row : rows) {
		std::string s;
		auto        err = row.Scan(s);
		if (!err.OK())
			return err;
		strings.push_back(s);
	}
	return rows.Err();
}

Error CrudOps::QueryVarArgPack(Executor* ex, const char* sql, size_t nParams, const Attrib** params, size_t num_args, varargs::OutArg* pack_array) {
	bool haveAny = false;
	auto rows    = ex->Query(sql, nParams, params);
	for (auto row : rows) {
		if (row.Num() == 0) {
			if (rows.ColumnCount() != num_args)
				return Error::Fmt("%v arguments specified, but SQL query returns %v columns", num_args, rows.ColumnCount());
		} else {
			return ErrNotOneResult;
		}

		for (size_t i = 0; i < num_args; i++) {
			if (!row[i].AssignTo(pack_array[i]).OK())
				return Error::Fmt("Column %v of type %v is not assignable to argument of type %v", i, dba::FieldTypeToString(row[i].Type), dba::FieldTypeToString(pack_array[i].Type));
		}

		haveAny = true;
	}
	if (!rows.OK())
		return rows.Err();
	if (!haveAny)
		return ErrEOF;
	return Error();
}
} // namespace dba
} // namespace imqs