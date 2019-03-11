#pragma once

#ifdef NOTYET
namespace imqs {
namespace dba {
namespace sqlparser {
class SqlAST;

/* Translate an SQL expression from our "liberal SQLite" syntax, to something that can be
executed on Postgres or MSSQL.

This was built for executing AbGis render queries on the native database that the layer is defined upon.
The original functionality would always execute the statement against a virtual SQLite table, but this
is very inefficient compared to executing directly against the native SQL database.
SQLite is very liberal in the syntax that it allows. In particular, the following are all valid identifiers:
	[material]
	"material"
	material
	[MATERIAL]
	"MATERIAL"
	MATERIAL
Every one of those tokens will match a field called "material", regardless of its casing.
This is further complicated by the fact that this following is a legal Sqlite expression:
	"material" = "PVC"
If there is a field called 'PVC' (or 'pvc', etc), then the "PVC" in that query will refer
to the field. However, if there is no field with that name, then "PVC" becomes a string.

Our approach here is thus:
1. Use the known field names from the actual database and scan over the original statement.
	Wherever find a match, which is not single-quoted, treat it as an identifier.

Some examples of what we do here:

one > 1			=>		([one] > 1)						A regular table, MSSQL syntax
one > 1			=>		([table].[one] > 1)				A joined table, MSSQL syntax
one > 1			=>		("table"."one" > 1)				A joined table, Postgres (aka ANSI) syntax

The unit tests have plenty more examples (see TestModule_SQLTranslate and SQLTranslator)

*/
class IMQS_DBA_API Translator {
public:
	struct TxInput {
		PortableDB*              DB; // Either DB or ModTab must be populated. DB takes preference.
		ModTable*                ModTab;
		std::vector<std::string> Tables;
	};
	static ErrDetail Translate(PortableDB* db, const podvec<XStringA>& tables, const XStringA& input, SqlStr& output);
	static ErrDetail Translate(ModTable* modTab, const XStringA& input, SqlStr& output);

	// Mutate 'ast' in-place.
	static ErrDetail Translate(const TxInput& in, SqlAST* ast);

private:
	static ErrDetail ResolveVariables(const TxInput& in, SqlAST* ast);
	static void      ApplyCasts(const TxInput& in, SqlAST* ast);
	static void      TransformNullQuirks(const TxInput& in, SqlAST* ast);

	static FieldType    TypeOf(const TxInput& in, SqlAST* ast);
	static ISqlAdaptor* SqlAdaptor(const TxInput& in);
	static bool         FindField(const TxInput& in, const char* identifier, int& tableIndex, Field*& fieldPtr);
	static bool         FindGeomField(const TxInput& in, int& tableIndex, Field*& fieldPtr);
	static bool         TransformSpecialFieldsIntoFunctions(const TxInput& in, const char* fieldName, SqlAST* ast);
	static SqlAST*      AddTypeCast(const TxInput& in, SqlAST* var_ast, FieldType castTo);

	static ErrDetail Translate(PortableDB* db, ModTable* modTab, const podvec<XStringA>& tables, const XStringA& input, SqlStr& output);
	static int       TableIndexFromModTableField(const TxInput& in, int fieldIndexInModTable);
};
}
}
}
#endif
