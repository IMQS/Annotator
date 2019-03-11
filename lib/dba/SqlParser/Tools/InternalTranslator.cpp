#include "pch.h"
#include "InternalTranslator.h"
#include "ASTCache.h"
#include "SqlStr.h"
#include "ParserUtils.h"
#include "../../Schema/DB.h"

namespace imqs {
namespace dba {
namespace sqlparser {

const char* InternalTranslator::SpecialSelectFunctions[] = {
    "dba_ST_AsGeom", // For cases where DB has a binary field (sqlite, HANA)
    "dba_AsGUID",    // For cases where DB has a 16 byte binary field (sqlite)
    "dba_AsINT32",   // For cases where DB's integer fetch doesn't distinguish 32/64 (sqlite)
    (const char*) nullptr,
};

Error InternalTranslator::Translate(const char* sql, const ohash::map<std::string, std::string>& shortFieldToFullField, schema::DB* dbSchema, SqlStr& translated, SqlDialect* dialect) {
	return Translate(DetectStatementType(sql) == SqlStatementType::Expression, sql, shortFieldToFullField, dbSchema, translated, dialect);
}

Error InternalTranslator::Translate(bool isExpression, const char* sql, const ohash::map<std::string, std::string>& shortFieldToFullField, schema::DB* dbSchema, SqlStr& translated, SqlDialect* dialect) {
	if (!dialect)
		dialect = translated.Dialect;

	std::string   parseErr;
	const SqlAST* ast = nullptr;
	if (isExpression)
		ast = Glob.ASTCache->GetAST_Expression(sql, parseErr);
	else
		ast = Glob.ASTCache->GetAST(sql, parseErr);

	if (parseErr != "")
		return Error(parseErr);

	Context cx;
	cx.Dialect               = dialect;
	cx.Schema                = dbSchema;
	cx.ShortFieldToFullField = &shortFieldToFullField;

	auto  copy = ast->Clone();
	Error err  = TranslateInternal(copy, cx);
	if (err.OK())
		copy->Print(translated);
	delete copy;

	Glob.ASTCache->ReleaseAST(ast);
	return err;
}

Error InternalTranslator::TranslateInternal(SqlAST* ast, Context& cx) {
	// It's easier if we make certain phases run before others, which is why we need to recursive through the AST more than once.
	// For example, the CAST phase assumes that the table and field names are already fully resolved, so that it can easily
	// find them in the db schema.

	FixFieldNames_R(ast, cx);
	ApplyCasts_R(ast, cx);

	return Error();
}

void InternalTranslator::FixFieldNames_R(SqlAST* ast, Context& cx) {
	// replace a short, and potentially incorrectly cased field name, with it's fully referenced name.
	// eg. vacant -> WaterDemandStand.Vacant
	if (ast->IsVariable()) {
		auto actual = cx.ShortFieldToFullField->getp(strings::tolower(ast->Variable));
		if (actual)
			ast->Variable = *actual;
	}

	for (auto child : ast->Params) {
		if (child)
			FixFieldNames_R(child, cx);
	}
}

// Apply automatic casts for cases where a user is doing something like "varCharField = 0"
void InternalTranslator::ApplyCasts_R(SqlAST* ast, Context& cx) {
	if (ast->IsOperator() && ast->Params.size() == 2) {
		if (ast->Params[0]->IsVariable() ^ ast->Params[1]->IsVariable()) {
			size_t  variableIndex = 0;
			SqlAST* variable      = ast->Params[0];
			SqlAST* value         = ast->Params[1];
			if (value->IsVariable()) {
				// make sure we have VARIABLE <=> VALUE in that order
				std::swap(variable, value);
				variableIndex = 1;
			}

			auto tvariable = TypeOf(variable, cx.Schema);
			auto tvalue    = TypeOf(value, cx.Schema);
			if (tvariable != Type::Null && tvalue != Type::Null && tvariable != tvalue) {
				if (tvariable == Type::Text && IsTypeNumeric(tvalue)) {
					ast->Params[variableIndex] = AddTypeCast(variable, tvalue, cx.Dialect);
					delete variable;
				}
				if (tvariable == Type::Bool && IsTypeNumeric(tvalue)) {
					ast->Params[1 - variableIndex] = AddTypeCast(value, Type::Bool, cx.Dialect);
					delete value;
				}
			}
		}
	}

	for (auto child : ast->Params) {
		if (child)
			ApplyCasts_R(child, cx);
	}
}

SqlAST* InternalTranslator::AddTypeCast(SqlAST* var_ast, Type castTo, SqlDialect* dialect) {
	if (!(var_ast->IsVariable() || var_ast->IsValue()))
		return nullptr;

	SqlAST* cast = new SqlAST();
	cast->SetFunction("CAST");
	cast->Params.push_back(new SqlAST());
	cast->Params.push_back(new SqlAST());
	if (var_ast->IsVariable())
		cast->Params[0]->SetVariable(var_ast->Variable.c_str());
	else
		cast->Params[0]->SetValue(var_ast->Value);

	SqlStr tmp(dialect);
	tmp.FormatType(castTo, 0);
	cast->Params[1]->SetValue((const char*) tmp);

	return cast;
}

dba::Type InternalTranslator::TypeOf(SqlAST* ast, schema::DB* dbSchema) {
	if (ast->IsVariable()) {
		if (!dbSchema)
			return dba::Type::Null;
		auto split = strings::Split(ast->Variable, '.');
		if (split.size() != 2)
			return dba::Type::Null;
		auto tab = dbSchema->TableByName(split[0]);
		if (!tab)
			return dba::Type::Null;
		auto field = tab->FieldByName(split[1]);
		if (!field)
			return dba::Type::Null;
		return field->Type;
	} else if (ast->IsBoolValue()) {
		return dba::Type::Bool;
	} else if (ast->IsStringValue()) {
		return dba::Type::Text;
	} else if (ast->IsNumericValue()) {
		if (floor(ast->Value.NumberVal) == ast->Value.NumberVal)
			return dba::Type::Int64;
		else
			return dba::Type::Double;
	}
	return dba::Type::Null;
}

void InternalTranslator::BakeBuiltin_Select(SqlStr& s) {
	// Our SQL parser doesn't understand SELECT statements, which are probably the most complex
	// type of SQL statement. In order to get the job done, we do what we need to here with
	// a much simpler regex-based replacement scheme.

	// The only construct that we need to be able to parse is this:
	// SELECT id,dba_ST_AsGeom(geom) FROM ...
	// and perhaps some JOINs thrown in there.
	// This was originally created for HANA, where the above construct is translated
	// into something like SELECT id,geom.ST_AsEWKB() FROM ...

	// I cannot see a security violation here, by doing simple regex replacements, but I may be wrong about that.
	// The key observation is that we control the replacement.

	for (size_t ikeyword = 0; SpecialSelectFunctions[ikeyword]; ikeyword++) {
		// Get the replacement function name for this special dba_ function
		SqlAST tmp;
		tmp.FuncName = SpecialSelectFunctions[ikeyword];
		SqlStr   repKeyword(s.Dialect);
		uint32_t printFlags = 0;
		s.Dialect->NativeFunc(tmp, repKeyword, printFlags);

		// The replacement name may be empty

		std::string keyword   = SpecialSelectFunctions[ikeyword];
		const char* sql_match = s.Str.c_str();
		std::regex  ident(keyword + R"-(\(([^\)]+)\))-");
		std::string rep;
		while (true) {
			std::cmatch m;
			if (!std::regex_search(sql_match, m, ident)) {
				rep += sql_match;
				break;
			}
			rep.append(sql_match, m[0].first);
			if (repKeyword == "") {
				// Omit parentheses if replacement function string is empty. ie..
				// dba_ST_AsGeom(geom) -> geom        (omit parentheses)
				// dba_ST_AsGeom(geom) -> (geom)      (include parentheses)
				// This is the norm for most databases (specifically Postgres, Sqlite, MSSQL)
				rep.append(m[1].first, m[1].second);
			} else if (repKeyword[0] == '.') {
				// This is for HANA. If the function name starts with a dot, then it means we must emit
				//     SELECT geom.FUNC() ...
				// eg: SELECT geom.ST_AsEWKB() ...
				// Extra rule for MSSQL:
				//     SELECT geom -> SELECT geom.STAsBinary() AS _dbaWKB_
				//     The above rule relies on the fact that there is a space in the replacement string,
				//     which we detect below. When we see the space, then we know not to add the () at
				//     the end of the replacement.
				rep.append(m[1].first, m[1].second);
				rep += repKeyword;
				if (repKeyword.Str.find(' ') == -1) {
					// Only add "()" if there is not a space in the replacement word. See above about MSSQL.
					rep += "()";
				}
			} else {
				rep += repKeyword;
				rep += "(";
				rep.append(m[1].first, m[1].second);
				rep += ")";
			}
			sql_match = m[0].second;
		}
		s.Str = rep;
	}
}

} // namespace sqlparser
} // namespace dba
} // namespace imqs
