#include "pch.h"
#include "SqlStr.h"
#include "Attrib.h"
#include "SqlParser/Tools/InternalTranslator.h"

namespace imqs {
namespace dba {

IMQS_DBA_API bool TranslateParamString(const char* s, char outPrefix, std::string& out) {
	const char* base = s;
	const char* pos  = base;

	for (; *s;) {
		switch (*s) {
		case '[':
			// MSSQL identifier
			while (*s && *s != ']')
				s++;
			if (*s)
				s++;
			break;
		case '"':
			// ANSI identifier
			s++;
			while (*s && *s != '"')
				s++;
			if (*s)
				s++;
			break;
		case '\'': {
			// string literal
			char p = 0;
			s++;
			while (*s && !(*s == '\'' && p != '\'')) {
				p = *s;
				s++;
			}
			if (*s)
				s++;
			break;
		}
		case '$': {
			// parameter
			auto start = s;
			s++;
			while (*s >= '0' && *s <= '9')
				s++;
			while (pos != start)
				out += *pos++;
			out += outPrefix;
			for (auto x = start + 1; x != s; x++)
				out += *x;
			pos = s;
			break;
		}
		default:
			s++;
			break;
		}
	}

	if (pos == base)
		return false;

	while (pos != s)
		out += *pos++;
	return true;
}

SqlDialect::~SqlDialect() {
}

void SqlDialect::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	switch (type) {
	case Type::Null:
		IMQS_DIE_MSG("SqlDialect::FormatType - Cannot format 'Null'");
		break;
	case Type::Bool: s += "BOOLEAN"; break;
	case Type::Int16: s += "SMALLINT"; break;
	case Type::Int32: s += "INTEGER"; break;
	case Type::Int64: s += "BIGINT"; break;
	case Type::Float: s += "REAL"; break;
	case Type::Double: s += "REAL"; break;
	case Type::Text:
		if (width_or_srid != 0)
			s += tsf::fmt("VARCHAR(%v)", width_or_srid);
		else
			s += "VARCHAR";
		break;
	case Type::Guid: s += "UUID"; break;
	case Type::Date: s += "TIMESTAMP"; break;
	case Type::Time: s += "TIME"; break;
	case Type::Bin:
		if (width_or_srid != 0)
			s += tsf::fmt("BLOB(%v)", width_or_srid);
		else
			s += "BLOB";
		break;
	case Type::GeomPoint: s += "GEOMETRY"; break;
	case Type::GeomMultiPoint: s += "GEOMETRY"; break;
	case Type::GeomPolyline: s += "GEOMETRY"; break;
	case Type::GeomPolygon: s += "GEOMETRY"; break;
	case Type::GeomAny: s += "GEOMETRY"; break;
	default:
		IMQS_DIE_MSG(tsf::fmt("SqlDialect::FormatType - unimplemented data type %v", (int) type).c_str());
	}
}

void SqlDialect::WriteValue(const Attrib& val, SqlStr& s) {
	char buf[128];
	// size_t* data;
	switch (val.Type) {
	case Type::Null:
		s += "NULL";
		break;
	case Type::Bool:
		s += val.Value.Bool ? "TRUE" : "FALSE";
		break;
	case Type::Int16:
	case Type::Int32:
	case Type::Int64:
	case Type::Float:
	case Type::Double:
		val.ToText(buf, sizeof(buf));
		s += buf;
		break;
	case Type::Text:
		s.Squote(val.Value.Text.Data, val.Value.Text.Size);
		break;
	case Type::Guid:
		val.ToText(buf, sizeof(buf));
		s.Squote(buf);
		break;
	case Type::Bin: {
		char* hbuf = buf;
		if (val.Value.Bin.Size * 2 + 1 > sizeof(buf))
			hbuf = (char*) imqs_malloc_or_die(val.Value.Bin.Size * 2 + 1);
		strings::ToHex(val.Value.Bin.Data, val.Value.Bin.Size, hbuf);
		NativeHexLiteral(hbuf, s);
		if (hbuf != buf)
			free(hbuf);
		break;
	}
	case Type::JSONB:
		s.Squote(val.Value.Text.Data, val.Value.Text.Size);
		break;
	case Type::Date:
	case Type::Time:
	case Type::GeomPoint:
	case Type::GeomMultiPoint:
	case Type::GeomPolyline:
	case Type::GeomPolygon:
	case Type::GeomAny:
		IMQS_DIE_MSG(tsf::fmt("SqlDialect::WriteValue - unimplemented type %v", (int) val.Type).c_str());
	}
}

void SqlDialect::AddLimit(SqlStr& s, const std::vector<std::string>& fields, int64_t limitCount, int64_t limitOffset, const std::vector<std::string>& orderBy) {
	s.Fmt("\n LIMIT %v OFFSET %v", limitCount, limitOffset);
}

void SqlDialect::TruncateTable(SqlStr& s, const std::string& table, bool resetSequences) {
	IMQS_DIE_MSG("SqlDialect::TruncateTable - Unimplemented function");
}

size_t SqlDialect::MaxQueryParams() {
	// Maximum number of parameters allowable in an SQL statement (i.e. $1, $2 ... $999)
	// The default value of 999 is a conversative lower bound, which happens to match the
	// default value for Sqlite. Other databases are probably higher, so we might want to
	// consider making this value specific to the dialect. However, in such cases, one may
	// also want to look at other APIs, such as the Postgres COPY API.
	return 999;
}

bool SqlDialect::IsSoftKeyword(const char* name) {
	return false;
}

// This must match the value produced by NativeFunc(dba_ST_GeomFrom)
void SqlDialect::ST_GeomFrom(SqlStr& s, const char* insertElement) {
	s += insertElement;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

SqlDialectFlags SqlDialectInternal::Flags() {
	return SqlDialectFlags::None;
}

void SqlDialectInternal::FormatType(SqlStr& s, Type type, int width_or_srid, TypeFlags flags) {
	return SqlDialect::FormatType(s, type, width_or_srid, flags);
}

void SqlDialectInternal::NativeFunc(const sqlparser::SqlAST& ast, SqlStr& s, uint32_t& printFlags) {
	s += ast.FuncName;
}

void SqlDialectInternal::NativeHexLiteral(const char* hexLiteral, SqlStr& s) {
	s += hexLiteral;
}

bool SqlDialectInternal::UseThisCall(const char* funcName) {
	return false;
}

dba::Syntax SqlDialectInternal::Syntax() {
	return dba::Syntax::Microsoft;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6313) // /analyze doesn't expect us to use flags for a template, but bools suffer from parameter opaqueness
#endif

enum TFmtQuoterFlags {
	TFmtQuoterFlag_SplitAtDot            = 1,
	TFmtQuoterFlag_DoubleEscapeBackslash = 2,
};

template <char QuoteA, char QuoteB, unsigned flags>
size_t TFmtQuoter(char* outBuf, size_t outBufSize, const tsf::fmtarg& val) {
	if (val.Type != tsf::fmtarg::TCStr) {
		IMQS_DIE_MSG("Parameters to be quoted must be 8-bit strings");
		return 0;
	}

	const bool doubleEscapeBackslash = !!(flags & TFmtQuoterFlag_DoubleEscapeBackslash);
	const bool splitAtDot            = !!(flags & TFmtQuoterFlag_SplitAtDot);

	const char* str         = val.CStr;
	size_t      len         = strlen(str);
	size_t      maxTotalLen = len * 3 + 2; // worst case is every character is a dot, so "..." becomes "[].[].[].[]"
	if (outBufSize < maxTotalLen)
		return -1;

	size_t nout    = 0;
	outBuf[nout++] = QuoteA;

	for (size_t i = 0; str[i]; i++) {
		if (splitAtDot && str[i] == '.') {
			outBuf[nout++] = QuoteB;
			outBuf[nout++] = '.';
			outBuf[nout++] = QuoteA;
		} else if (doubleEscapeBackslash && str[i] == '\\') {
			outBuf[nout++] = '\\';
			outBuf[nout++] = '\\';
		} else if (str[i] == '\'') {
			outBuf[nout++] = '\'';
			outBuf[nout++] = '\'';
		} else {
			outBuf[nout++] = str[i];
		}
	}

	outBuf[nout++] = QuoteB;
	return nout;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SqlStr::SqlStr(SqlDialect* dialect) : Dialect(dialect) {
}

SqlStr SqlStr::InternalDialect() {
	return SqlStr(Glob.InternalDialect);
}

void SqlStr::Clear() {
	Str = "";
}

SqlStr& SqlStr::operator+=(const std::string& s) {
	Str += s;
	return *this;
}

SqlStr& SqlStr::operator+=(const char* s) {
	Str += s;
	return *this;
}

SqlStr& SqlStr::operator+=(const Attrib& v) {
	Dialect->WriteValue(v, *this);
	return *this;
}

SqlStr::operator const char*() const {
	return Str.c_str();
}

void SqlStr::FormatType(Type type, int width_or_srid, TypeFlags flags) {
	Dialect->FormatType(*this, type, width_or_srid, flags);
}

void SqlStr::Squote(const char* s, size_t len) {
	SQuoteAny(Str, s, len);
}

void SqlStr::Squote(const std::string& s) {
	Squote(s.c_str(), s.size());
}

std::string SqlStr::SQuotedCopy(const std::string& s) {
	std::string sql;
	SQuoteAny(sql, s.c_str(), s.size());
	return sql;
}

void SqlStr::SQuoteAny(std::string& sql, const char* s, size_t len) {
	sql += '\'';
	for (size_t i = 0; s[i] && i != len; i++) {
		if (s[i] == '\'') {
			sql += '\'';
			sql += '\'';
		} else {
			sql += s[i];
		}
	}
	sql += '\'';
}

void SqlStr::Identifier(const char* str, size_t len, bool singular) {
	if (len == -1)
		len = strlen(str);
	const char quoteOpen  = Dialect->Syntax() == Syntax::Microsoft ? '[' : '"'; // [DB].[Schema].[TableName].[Fieldname]
	const char quoteClose = Dialect->Syntax() == Syntax::Microsoft ? ']' : '"'; // "DB"."Schema"."Tablename"."Fieldname"
	Str += quoteOpen;
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '.' && !singular) {
			Str += quoteClose;
			Str += '.';
			Str += quoteOpen;
		} else if (str[i] == '\'') {
			IMQS_DIE_MSG("Illegal character (apostrophe) in SQL identifier");
		} else if (str[i] == '"') {
			IMQS_DIE_MSG("Illegal character (double quote) in SQL identifier");
		} else {
			Str += str[i];
		}
	}
	Str += quoteClose;
}

void SqlStr::Identifier(const std::string& str, bool singular) {
	Identifier(str.c_str(), str.length(), singular);
}

Error SqlStr::BakeBuiltinFuncs() {
	// See comment inside BakeBuiltin_Select for what's going on here.
	if (strings::StartsWith(Str, "SELECT")) {
		sqlparser::InternalTranslator::BakeBuiltin_Select(*this);
		return Error();
	}

	SqlStr translated(Dialect);
	Error  err = sqlparser::InternalTranslator::Translate(Str.c_str(), {}, nullptr, translated, Dialect);
	if (err.OK())
		Str = translated.Str;
	return err;
}

void SqlStr::Chop(int n) {
	n = (int) std::min((size_t) n, Str.length());
	if (n < 0)
		n = 0;
	if (n > 0)
		Str.erase(Str.end() - n, Str.end());
}

size_t SqlStr::FmtQuoteName_ANSI(char* outBuf, size_t outBufSize, const tsf::fmtarg& val) {
	return TFmtQuoter<'"', '"', TFmtQuoterFlag_SplitAtDot>(outBuf, outBufSize, val);
}

size_t SqlStr::FmtQuoteName_MSSQL(char* outBuf, size_t outBufSize, const tsf::fmtarg& val) {
	return TFmtQuoter<'[', ']', TFmtQuoterFlag_SplitAtDot>(outBuf, outBufSize, val);
}

// This was only necessary for some Postgres installations prior to 9.1
//size_t SqlStr::FmtQuoteString_DoubleBackslash(char* outBuf, size_t outBufSize, const tsf::fmtarg& val)
//{
//	return TFmtQuoter<'\'', '\'', TFmtQuoterFlag_DoubleEscapeBackslash>(outBuf, outBufSize, val);
//}

size_t SqlStr::FmtQuoteString_Standard(char* outBuf, size_t outBufSize, const tsf::fmtarg& val) {
	return TFmtQuoter<'\'', '\'', 0>(outBuf, outBufSize, val);
}

char* SqlStr::PretranslateToANSI(const char* format_str, char* buf, size_t bufsize) {
	size_t i   = 0;
	char*  out = buf;
	for (; format_str[i]; i++) {
		char in = format_str[i];
		if (i >= bufsize - 2 && out == buf) {
			size_t len = strlen(format_str);
			out        = (char*) imqs_malloc_or_die(len + 1);
			memcpy(out, buf, i);
		}
		if (in == '[')
			out[i] = '"';
		else if (in == ']')
			out[i] = '"';
		else
			out[i] = in;
	}
	out[i] = 0;
	return out;
}

} // namespace dba
} // namespace imqs
