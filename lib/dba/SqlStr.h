#pragma once

#include "SqlParser/Generated/Parser.h"
#include "Schema/Field.h"

namespace imqs {
namespace dba {

class Attrib;
class SqlStr;

// Translate SQL parameter placeholders from $1, $2 to something like
// :1, :2, etc. The exact prefix (':' in this example) is a function parameter.
// The $1, $2 syntax is native to Postgres. The SQlite equivalent is ?1, ?2, ?3.
// The databases can't seem to agree on this, so we need to harmonize them,
// and we arbitrarily pick Postgres' syntax as our own.
// Returns true if any translation is necessary. If no translation is necessary,
// then 'out' is empty. This avoids an unnecessary allocation for statements
// that do not use numbered parameters.
// This was created for SqlAPI, which uses :1, :2 syntax.
IMQS_DBA_API bool TranslateParamString(const char* s, char outPrefix, std::string& out);

// Controls whether identifiers are escaped with [square brackets] or "double quotes".
enum class Syntax {
	ANSI,
	Microsoft,
};

/* Functions that help building up SQL strings, where certain elements
are database-specific, such as data types.

From a certain perspective, it would be ideal to represent all syntactic differences between
SQL databases at the AST level. For some cases, this is what we do, and this method is used in
functions such as NativeFunc, which takes an AST node and emits the DB's actual function name
for our abstract version of that function.

However, there are other times when it's just been much simpler to represent the differences
between databases as simple string building. An example of this is the AddLimit() function,
which adds the equivalent of the " LIMIT count OFFSET offset" clause, in a way that is
compatible with the various different database types.
*/
class IMQS_DBA_API SqlDialect {
public:
	virtual ~SqlDialect();
	virtual SqlDialectFlags Flags()                                                                   = 0;
	virtual dba::Syntax     Syntax()                                                                  = 0;
	virtual void            NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) = 0;
	virtual void            NativeHexLiteral(const char* hexLiteral, SqlStr& s)                       = 0;
	virtual bool            UseThisCall(const char* funcName)                                         = 0;

	// Ideally, we could satisfy the functionality of ST_GeomFrom with our SQL parser, but right now we're unable
	// to parse the multi-insert statement of the form
	//   INSERT INTO table (a,b,c) (SELECT $1,$2,$3 FROM dummy UNION SELECT $4,$5,$6 FROM dummy UNION ...)
	// It's a lot of work to make that parseable, so instead I'm just adding support for this now.
	virtual void ST_GeomFrom(SqlStr& s, const char* insertElement);

	virtual void   FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags);
	virtual void   WriteValue(const Attrib& val, SqlStr& s);
	virtual void   AddLimit(SqlStr& s, const std::vector<std::string>& fields, int64_t limitCount, int64_t limitOffset, const std::vector<std::string>& orderBy);
	virtual void   TruncateTable(SqlStr& s, const std::string& table, bool resetSequences = false);
	virtual size_t MaxQueryParams();                // Maximum number of $1, $2, $3.. etc. Default is 999.
	virtual bool   IsSoftKeyword(const char* name); // For things like CURRENT_DATE in postgres, so that we don't try turn them into "CURRENT_DATE" (ie a field name)
};

// Internal SQL dialect for SQL statements that we parse with our own parser
// A global static instance of this is available from dba::Glob.InternalDialect
// You can use SqlStr::InternalDialect() to construct a new SqlStr that references
// this dialect.
class IMQS_DBA_API SqlDialectInternal : public SqlDialect {
public:
	SqlDialectFlags Flags() override;
	dba::Syntax     Syntax() override;
	void            NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) override;
	void            NativeHexLiteral(const char* hexLiteral, SqlStr& s) override;
	bool            UseThisCall(const char* funcName) override;
	void            FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) override;
};

/* An SQL string builder.
This is tied to a particular SQL syntax, ie Microsoft or ANSI.
Microsoft syntax escapes identifiers with [square brackets], while ANSI
uses "double quotes".
In addition, an SqlStr is bound to an SqlDialect, which knows how to format
SQL for a specific database (ie BYTEA vs BLOB, UUID vs UNIQUEIDENTIFIER, etc).
*/
class IMQS_DBA_API SqlStr {
public:
	std::string Str;
	SqlDialect* Dialect = nullptr;

	SqlStr(SqlDialect* dialect);

	// Return a new SqlStr set to our internal dialect
	static SqlStr InternalDialect();

	void Clear();
	bool IsEmpty() const { return Str == ""; }

	// Append the formatted string, that gets run through our custom formatter, which uses the tsf library underneath,
	// which supports the regular printf formatting rules, as well as the following constructs:
	// %q escapes an SQL string and surrounds it with single quotes, so it will transform (the 'value') into ('the ''value''')
	// %Q escapes an SQL identifier, so you'll get [Fieldname] for MSSQL syntax and "Fieldname" for ANSI syntax.
	// An identifier (%Q) that has dots inside it will be split, for example:
	//   MyTable.MyField will be escaped as [MyTable].[MyField] or "MyTable"."MyField"
	// Additionally, in your format string, you can enclose any field reference inside square brackets,
	// and that will get treated identically to a %Q. For example, Fmt("SELECT [field] FROM [table]") is
	// mostly equivalent to writing Fmt("SELECT %Q FROM %Q, "field", "table"). It is most equivalent, and not
	// exactly equivalent, because the dot expansion does not take place. Specifically, if you write
	// [table.field], it will get translated to "table.field". This makes the translation phase simpler.
	// Be cautious with the [bracket] automatic translation - It is very naive. In other words, don't feed
	// this function machine-generated, or user-supplied format_str. Only feed it hand-crafted explicit strings.
	// This function naively translates every single square bracket - it does no parsing of any kind.
	template <typename... Args>
	void Fmt(const char* format_str, const Args&... args) {
		const size_t outbufsize = 200;
		char         outbuf[outbufsize];

		// buffer for our translated format_str, after undergoing [ident] -> "ident" transformations (if necessary)
		const size_t txbufsize = 200;
		char         txbuf[txbufsize];
		char*        tx = const_cast<char*>(format_str);

		auto syntax = Dialect->Syntax();
		if (syntax == Syntax::ANSI)
			tx = PretranslateToANSI(format_str, txbuf, txbufsize);

		tsf::context cx;
		cx.Escape_Q = syntax == Syntax::ANSI ? FmtQuoteName_ANSI : FmtQuoteName_MSSQL;
		cx.Escape_q = FmtQuoteString_Standard;

		tsf::StrLenPair msg = tsf::fmt_buf(cx, outbuf, outbufsize, tx, args...);
		Str.append(msg.Str, msg.Len);

		if (msg.Str != outbuf)
			delete[] msg.Str;

		if (tx != txbuf && tx != format_str)
			free(tx);
	}

	SqlStr& operator+=(const std::string& s);
	SqlStr& operator+=(const char* s);
	SqlStr& operator+=(const Attrib& v);

	bool operator==(const SqlStr& b) const { return Str == b.Str; }
	bool operator!=(const SqlStr& b) const { return Str != b.Str; }
	bool operator==(const char* b) const { return Str == b; }
	bool operator!=(const char* b) const { return Str != b; }
	bool operator==(const std::string& b) const { return Str == b; }
	bool operator!=(const std::string& b) const { return Str != b; }

	operator const char*() const;

	void FormatType(Type type, int width_or_srid, TypeFlags flags = TypeFlags::None);

	void Squote(const char* s, size_t len = -1);                              // Escape s in 'single quotes'
	void Squote(const std::string& s);                                        // Escape s in 'single quotes'
	void Chop(int n = 1);                                                     // Chop n characters off the end of the string. n is clamped to the string length.
	void Identifier(const char* str, size_t len = -1, bool singular = false); // Add a properly quoted and escaped identifier (for db, table, or field name).
	void Identifier(const std::string& str, bool singular = false);           // Add a properly quoted and escaped identifier (for db, table, or field name).

	// Translate functions such as dba_ST_GeomFrom() into their native driver equivalent,
	// such as ST_GeomFromEWKB() for PostGIS. If translation fails, then the string is left unchanged.
	Error BakeBuiltinFuncs();

	// Return a copy of the string, single quoted
	static std::string SQuotedCopy(const std::string& s);

private:
	static size_t FmtQuoteName_ANSI(char* outBuf, size_t outBufSize, const tsf::fmtarg& val);
	static size_t FmtQuoteName_MSSQL(char* outBuf, size_t outBufSize, const tsf::fmtarg& val);
	static size_t FmtQuoteString_Standard(char* outBuf, size_t outBufSize, const tsf::fmtarg& val);
	static char*  PretranslateToANSI(const char* format_str, char* buf, size_t bufsize);
	static void   SQuoteAny(std::string& sql, const char* s, size_t len = -1);
};
} // namespace dba
} // namespace imqs
