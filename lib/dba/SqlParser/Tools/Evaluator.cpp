#include "pch.h"
#include "Evaluator.h"

namespace imqs {
namespace dba {
namespace sqlparser {

Attrib Evaluator::Evaluate(const SqlAST* ast, std::function<Attrib(const char*)> variableResolver) {
	Attrib left, right;
	if (ast->Params.size() == 2) {
		left  = Evaluate(ast->Params[0], variableResolver);
		right = Evaluate(ast->Params[1], variableResolver);
	}

	if (ast->IsValue()) {
		if (ast->IsStringValue())
			return ast->Value.StrVal.c_str();
		else if (ast->IsBoolValue())
			return ast->Value.BoolVal;
		else if (ast->IsNumericValue())
			return ast->Value.NumberVal;
		else if (ast->IsNullValue())
			return Attrib();
	} else if (ast->IsCase()) {
		Attrib base;
		bool   haveBase = !ast->Params[SqlAST::Case_Base]->IsNullValue();
		if (haveBase)
			base = Evaluate(ast->Params[SqlAST::Case_Base], variableResolver);

		for (size_t i = 2; i < ast->Params.size(); i += 2) {
			Attrib when  = Evaluate(ast->Params[i], variableResolver);
			bool   match = haveBase ? CompareAny(base, when) == 0 : when.ToBool();
			if (match)
				return Evaluate(ast->Params[i + 1], variableResolver);
		}

		if (!ast->Params[SqlAST::Case_Else]->IsNullValue())
			return Evaluate(ast->Params[SqlAST::Case_Else], variableResolver);
		else
			return Attrib();
	} else if (ast->IsVariable()) {
		return variableResolver(ast->Variable.c_str());
	} else if (ast->FuncName == "=") {
		return CompareAny(left, right) == 0;
	} else if (ast->FuncName == "<") {
		return CompareAny(left, right) < 0;
	} else if (ast->FuncName == ">") {
		return CompareAny(left, right) > 0;
	} else if (ast->FuncName == "<=") {
		return CompareAny(left, right) <= 0;
	} else if (ast->FuncName == ">=") {
		return CompareAny(left, right) >= 0;
	} else if (ast->FuncName == "!=") {
		return CompareAny(left, right) != 0;
	} else if (ast->FuncName == "*") {
		return left.ToDouble() * right.ToDouble();
	} else if (ast->FuncName == "OR") {
		return left.ToBool() || right.ToBool();
	} else if (ast->FuncName == "AND") {
		return left.ToBool() && right.ToBool();
	} else if (ast->FuncName == "abs") {
		return fabs(Evaluate(ast->Params[0], variableResolver).ToDouble());
	} else if (ast->FuncName == "lower") {
		if (ast->Params.size() != 1)
			return "";
		left = Evaluate(ast->Params[0], variableResolver);
		return strings::tolower(left.ToString());
	} else if (ast->FuncName == "upper") {
		if (ast->Params.size() != 1)
			return "";
		left = Evaluate(ast->Params[0], variableResolver);
		return strings::toupper(left.ToString());
	}
	return false;
}

int Evaluator::CompareAny(const Attrib& a, const Attrib& b) {
	if (a.IsNull() && !b.IsNull())
		return -1;
	if (!a.IsNull() && b.IsNull())
		return 1;
	if (a.IsNull() && b.IsNull())
		return 0;

	if (a.IsNumeric() || b.IsNumeric()) {
		return a.CompareAsNum(b);
	} else if (a.IsText() && b.IsText()) {
		return a.Compare(b);
	} else if (a.IsText() || b.IsText()) {
		// this case is dubious. surely this is handled by the first case.
		return a.CompareAsNum(b);
	} else if (a.IsBool() && b.IsBool()) {
		return (a.Value.Bool ? 1 : -1) - (b.Value.Bool ? 1 : -1);
	}

	return 0;
}
}
}
}