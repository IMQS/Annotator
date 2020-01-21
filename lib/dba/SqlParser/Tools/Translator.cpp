#include "pch.h"
#ifdef NOTYET
#include "Translator.h"
#include "ASTCache.h"

namespace adb {

#ifdef _MSC_VER
static bool StrEqNoCase(const char* a, const char* b) {
	return _stricmp(a, b) == 0;
}
#else
static bool StrEqNoCase(const char* a, const char* b) {
	return strcasecmp(a, b) == 0;
}
#endif

ErrDetail SqlTranslator::Translate(PortableDB* db, const podvec<XStringA>& tables, const XStringA& input, SqlStr& output) {
	return Translate(db, nullptr, tables, input, output);
}

ErrDetail SqlTranslator::Translate(ModTable* modTab, const XStringA& input, SqlStr& output) {
	return Translate(nullptr, modTab, {}, input, output);
}

ErrDetail SqlTranslator::Translate(PortableDB* db, ModTable* modTab, const podvec<XStringA>& tables, const XStringA& input, SqlStr& output) {
	ErrDetail     err = ErOk;
	XStringA      errorStr;
	const SqlAST* ast = GetPool()->SqlASTCache->GetAST(input, errorStr);
	if (ast) {
		TxInput in;
		in.DB     = db;
		in.ModTab = modTab;
		in.Tables = tables;
		if (modTab != nullptr) {
			// Fill in.Tables with [ModTable, Join1, Join2, Join3...]
			// If ModTable is not a joined table, then it will just be [ModTable]
			in.Tables += modTab->TableName().ToUtf8();
			pvect<ModTable*> raw;
			modTab->GetUnderlyingTables(raw, 0);
			for (auto r : raw)
				in.Tables += r->TableName().ToUtf8();
		}

		SqlAST* outAST = ast->Clone();
		err            = Translate(in, outAST);
		adb::GetPool()->SqlASTCache->ReleaseAST(ast);

		outAST->Print(output);
		delete outAST;
	} else {
		err = ErrDetail(ErSqlParse, errorStr);
	}

	return err;
}

ErrDetail SqlTranslator::Translate(const TxInput& in, SqlAST* ast) {
	ErrDetail er = ResolveVariables(in, ast);
	if (!OK(er))
		return er;
	TransformNullQuirks(in, ast);
	ApplyCasts(in, ast);
	return ErOk;
}

ErrDetail SqlTranslator::ResolveVariables(const TxInput& in, SqlAST* ast) {
	bool isDoubleQuote = ast->IsStringValue() && ast->Value.Kind == AnyType::DQuotedString;
	if (isDoubleQuote || ast->IsVariable()) {
		int         tableIndex = -1;
		Field*      fieldPtr   = nullptr;
		const char* fieldName  = nullptr;
		bool        resolved   = false;

		if (isDoubleQuote) {
			// SQLite allows variables to be entered as double quoted string values. If the string doesn't
			// match a known identifier, then it is treated as a string literal.
			fieldName = ast->Value.StrVal.c_str();
		} else {
			fieldName = ast->Variable.c_str();
		}

		if (FindField(in, fieldName, tableIndex, fieldPtr)) {
			ast->SetVariable(in.Tables[tableIndex] + "." + fieldPtr->Name.ToUtf8());
			resolved = true;
		}

		if (!resolved && TransformSpecialFieldsIntoFunctions(in, fieldName, ast))
			resolved = true;

		if (!resolved && !isDoubleQuote)
			return ErrDetail(ErFieldNotFound, fieldName);
	}

	for (auto p : ast->Params) {
		ErrDetail er = ResolveVariables(in, p);
		if (!OK(er))
			return er;
	}

	return ErOk;
}

void SqlTranslator::ApplyCasts(const TxInput& in, SqlAST* ast) {
	if (ast->IsOperator() && ast->Params.size() == 2) {
		if (ast->Params[0]->IsVariable() ^ ast->Params[1]->IsVariable()) {
			int  varIndex = 0;
			auto variable = ast->Params[0];
			auto value    = ast->Params[1];
			if (value->IsVariable()) {
				std::swap(variable, value);
				varIndex = 1;
			}
			auto tvariable = TypeOf(in, variable);
			auto tvalue    = TypeOf(in, value);
			if (tvariable != FieldTypeNull && tvalue != FieldTypeNull && tvariable != tvalue) {
				if (tvariable == FieldTypeText && IsNumType(tvalue)) {
					ast->Params[varIndex] = AddTypeCast(in, variable, tvalue);
					delete variable;
				}
				if (tvariable == FieldTypeBool && IsNumType(tvalue)) {
					ast->Params[1 - varIndex] = AddTypeCast(in, value, tvariable);
					delete value;
				}
			}
		}
	}

	for (auto p : ast->Params)
		ApplyCasts(in, p);
}

void SqlTranslator::TransformNullQuirks(const TxInput& in, SqlAST* ast) {
	// SQLite allows "x NOT NULL", but other database don't.
	// So we always transform "x NOT NULL" into "x IS NOT NULL";
	if (ast->IsOperator() && ast->FuncName == "NOT" && ast->Params.size() == 2 && ast->Params[1]->IsNullValue()) {
		ast->FuncName = "IS NOT NULL";
		delete ast->Params[1];
		ast->Params.pop_back();
	}

	for (auto p : ast->Params)
		TransformNullQuirks(in, p);
}

FieldType SqlTranslator::TypeOf(const TxInput& in, SqlAST* ast) {
	if (ast->IsVariable()) {
		int    tableIndex = -1;
		Field* fieldPtr   = nullptr;
		if (FindField(in, ast->Variable.c_str(), tableIndex, fieldPtr))
			return fieldPtr->Type;
	} else if (ast->IsValue()) {
		if (ast->Value.IsBool())
			return FieldTypeBool;
		else if (ast->Value.IsNumber())
			return FieldTypeDouble;
		else if (ast->Value.IsString())
			return FieldTypeText;
	}

	return FieldTypeNull;
}

ISqlAdaptor* SqlTranslator::SqlAdaptor(const TxInput& in) {
	if (in.DB != nullptr)
		return in.DB->SqlAdaptor();
	auto sqc = in.ModTab->ParentDB()->RawSQL();
	if (sqc != nullptr)
		return sqc->Adap;
	pvect<ModTable*> rawTables;
	in.ModTab->GetUnderlyingTables(rawTables);
	if (rawTables.size() != 0)
		sqc = rawTables[0]->ParentDB()->RawSQL();
	if (sqc != nullptr)
		return sqc->Adap;
	return nullptr;
}

bool SqlTranslator::FindField(const TxInput& in, const char* identifier, int& tableIndex, Field*& fieldPtr) {
	const char* dot = strchr(identifier, '.');
	if (dot != nullptr) {
		// identifier is already "table.field"
		XStringA partTable(identifier, dot - identifier);
		XStringA partField(dot + 1);
		if (in.DB != nullptr) {
			fieldPtr = in.DB->GetFieldPtrByName(partTable, partField);
			if (fieldPtr != nullptr) {
				tableIndex = in.Tables.find(partTable);
				if (tableIndex == -1)
					return false;
				return true;
			}
		} else {
			// ModTable
			int fi = in.ModTab->FieldIndexFast(partField, partField.Length());
			if (fi != -1) {
				fieldPtr = in.ModTab->GetFieldPtr(fi);
				if (fieldPtr != nullptr) {
					tableIndex = in.Tables.find(partTable);
					if (tableIndex == -1)
						return false;
					return true;
				}
			}
		}
		return false;
	}

	// identifier is just "field"
	if (in.DB != nullptr) {
		// PortableDB
		for (intp i = 0; i < in.Tables.size(); i++) {
			fieldPtr = in.DB->GetFieldPtrByName(in.Tables[i], identifier);
			if (fieldPtr != nullptr) {
				tableIndex = (int) i;
				return true;
			}
		}
	} else {
		// ModTable
		int fi = in.ModTab->FieldIndexFast(identifier, strlen(identifier));
		if (fi != -1) {
			fieldPtr = in.ModTab->GetFieldPtr(fi);
			if (fieldPtr != nullptr && !fieldPtr->IsDynamic()) {
				tableIndex = TableIndexFromModTableField(in, fi);
				return true;
			}
		}
	}
	return false;
}

bool SqlTranslator::FindGeomField(const TxInput& in, int& tableIndex, Field*& fieldPtr) {
	if (in.DB != nullptr) {
		for (int i = 0; i < in.Tables.size(); i++) {
			int fi = in.DB->GetGeomField(in.Tables[i]);
			if (fi != -1) {
				tableIndex = (int) i;
				fieldPtr   = in.DB->GetFieldPtr(in.Tables[i], fi);
				return true;
			}
		}
	} else {
		int fi_top = FirstGeomField(in.ModTab);
		if (fi_top != -1) {
			fieldPtr = in.ModTab->GetFieldPtr(fi_top);
			if (!fieldPtr->IsDynamic()) {
				tableIndex = TableIndexFromModTableField(in, fi_top);
				return true;
			}
		}
	}
	return false;
}

bool SqlTranslator::TransformSpecialFieldsIntoFunctions(const TxInput& in, const char* fieldName, SqlAST* ast) {
	int    geomTable = -1;
	Field* geomField = nullptr;
	if (!FindGeomField(in, geomTable, geomField))
		return false;

	// Turn 'ast' from a variable into a function, and add a single parameter to that
	// function. This new parameter is the geometry field.

	if (StrEqNoCase(fieldName, "area"))
		ast->SetFunction("ST_Area");
	else if (StrEqNoCase(fieldName, "length2d"))
		ast->SetFunction("ST_Length");
	else if (StrEqNoCase(fieldName, "length3d"))
		ast->SetFunction("ST_3DLength");
	else
		return false;

	auto newVar = new SqlAST();
	newVar->SetVariable(in.Tables[geomTable] + "." + geomField->Name.ToUtf8());
	ast->Params.push_back(newVar);

	// Adding this extra "true" parameter causes ST_Area and friends to use the WGS84 spheroid
	// when computing these metrics
	newVar = new SqlAST();
	newVar->SetValue(true);
	ast->Params.push_back(newVar);

	return true;
}

SqlAST* SqlTranslator::AddTypeCast(const TxInput& in, SqlAST* var_ast, FieldType castTo) {
	if (!(var_ast->IsVariable() || var_ast->IsValue()))
		return nullptr;
	ISqlAdaptor* adapt      = SqlAdaptor(in);
	int          tableIndex = -1;
	Field*       fieldPtr   = nullptr;
	XStringA     fieldName  = "";

	if (var_ast->IsVariable()) {
		if (!FindField(in, var_ast->Variable.c_str(), tableIndex, fieldPtr))
			return nullptr;
		fieldName = fieldPtr->Name.ToUtf8();
	}

	SqlStr fieldTypeStr = adapt->Str();
	adapt->FormatFieldType(castTo, 0, false, false, fieldTypeStr);
	SqlAST* cast = new SqlAST();
	cast->SetFunction("CAST");
	cast->Params.push_back(new SqlAST());
	cast->Params.push_back(new SqlAST());
	if (var_ast->IsVariable())
		cast->Params[0]->SetVariable(in.Tables[tableIndex] + "." + fieldName);
	else
		cast->Params[0]->SetValue(var_ast->Value);
	cast->Params[1]->SetValue((const char*) fieldTypeStr);

	return cast;
}

int SqlTranslator::TableIndexFromModTableField(const TxInput& in, int fieldIndexInModTable) {
	if (in.ModTab->GetJoinTable() == nullptr)
		return 0;

	// We're only looking for real fields here. Things we can use in an SQL statements executed on the actual DB.
	ASSERT(!in.ModTab->GetFieldPtr(fieldIndexInModTable)->IsDynamic());

	int       raw_tab    = -1;
	int       raw_field  = -1;
	ModTable* raw_tabPtr = nullptr;
	VERIFY(in.ModTab->GetJoinTable()->TranslateField(fieldIndexInModTable, raw_field, raw_tabPtr, raw_tab));
	return raw_tab + 1;
}
} // namespace adb

#endif