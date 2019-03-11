#pragma once

namespace imqs {
namespace dba {
class SqlStr;
class SqlDialect;
namespace schema {
class DB;
}

namespace sqlparser {
class SqlAST;

/* Translates SQL written in our "internal dba" dialect into SQL for a real DB.

The dba library embeds an SQL parser. This allows us to write SQL statements in a cross-platform manner,
and have those statements turned into SQL for a real DB the moment before it is executed.
This document describes the special functions that are available to use inside prepared statements
inside dba.

For example, this allows one to write the following statement:

	INSERT INTO table (geom) VALUES (dba_ST_GeomFrom($1))

When executing on Postgres, the above statement is transformed into:

	INSERT INTO table (geom) VALUES (ST_GeomFromEWKB($1))

Special Functions

dba_ST_GeomFrom()
	This function is replaced by a designated "native" geometry constructor, such as ST_GeomFromEWKB
	on PostGIS. The replacement function that is used is determined by SqlDialect::NativeFunc().
	The idea here is that we can use the most natural, fastest method to insert geometry into the database.
	We don't have to resort to Well Known Text, just to be compatible with different databases.

dba_ST_AsGeom()
	This function is replaced by a designated "native" geometry selector, such as ST_AsWKB in HANA.
	This is only needed by some spatial databases that don't expose their internal format.
	Others are happy to emit their native format, and we are capable of consuming it.

NOTE - Special functions that appear as part of a SELECT statement, such as dba_ST_AsGeom(),
must be added to SpecialSelectFunctions. These are dealt with by BakeBuiltin_Select(), which
doesn't do a proper parse of the SQL. See inside for details.

Additional Considerations

-	Whenever we manipulate the AST, we try to make sure that it is an AST which corresponds to our "internal"
	SQL dialect, and not the dialect of a particular real-world database. Print() is responsible for translating
	our universal AST into the native DB-dialect.

*/
class IMQS_DBA_API InternalTranslator {
public:
	static const char* SpecialSelectFunctions[];

	// If dialect is null, then the dialect of 'translated' is used. Automatically detect whether this is an expression or a statement.
	// shortFieldToFullField is a map from lower case field name such as "stand_id", to a fully qualified field name such as "WaterDemandStands.Stand_ID"
	// The fully qualified name doesn't need to include the table name. It can also just be "stand_id" -> "Stand_ID". If a field is not
	// found in the map, then it is left unchanged.
	static Error Translate(const char* sql, const ohash::map<std::string, std::string>& shortFieldToFullField, schema::DB* dbSchema, SqlStr& translated, SqlDialect* dialect = nullptr);

	// If dialect is null, then the dialect of 'translated' is used.
	static Error Translate(bool isExpression, const char* sql, const ohash::map<std::string, std::string>& shortFieldToFullField, schema::DB* dbSchema, SqlStr& translated, SqlDialect* dialect = nullptr);

	// A simple regex-based replacer, for SELECT statements. See inside for details.
	static void BakeBuiltin_Select(SqlStr& s);

private:
	struct Context {
		const ohash::map<std::string, std::string>* ShortFieldToFullField = nullptr;
		schema::DB*                                 Schema                = nullptr;
		SqlDialect*                                 Dialect               = nullptr;
	};

	static Error     TranslateInternal(SqlAST* ast, Context& cx);
	static void      FixFieldNames_R(SqlAST* ast, Context& cx);
	static void      ApplyCasts_R(SqlAST* ast, Context& cx);
	static SqlAST*   AddTypeCast(SqlAST* var_ast, Type castTo, SqlDialect* dialect);
	static dba::Type TypeOf(SqlAST* ast, schema::DB* dbSchema);
};
} // namespace sqlparser
} // namespace dba
} // namespace imqs