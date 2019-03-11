#include "pch.h"
#include "Verifier.h"
#include "ASTCache.h"
#include "../../Global.h"

namespace imqs {
namespace dba {
namespace sqlparser {

const ohash::set<std::string> Verifier::Exact_WhiteList = {
    "MIN",
    "MAX",
    "AVG",
    "COUNT",
    "SUM",
    "MID",
    "LEN",
    "ROUND",
    "NOW",
    "FIRST",
    "LAST",
    "LOWER",
    "UPPER",
    "ABS",
    "ROUND"};

const std::vector<std::string> Verifier::Wildcard_WhiteList = {
    "ST_*"};

bool Verifier::IsSafeToExecuteExpression(const char* input) {
	bool          ret = true;
	std::string   errorStr;
	const SqlAST* ast = Glob.ASTCache->GetAST_Expression(input, errorStr);
	if (ast) {
		ret = IsSafeToExecuteExpressionAST(ast);
		Glob.ASTCache->ReleaseAST(ast);
	} else {
		ret = false;
	}
	return ret;
}

bool Verifier::IsSafeToExecuteExpressionAST(const SqlAST* node) {
	for (SqlAST* child : node->Params) {
		bool ok = IsSafeToExecuteExpressionAST(child);
		if (!ok)
			return false;
	}

	if (node->IsFunction()) {
		std::string ucase   = strings::toupper(node->FuncName);
		bool        isWhite = Exact_WhiteList.contains(ucase);
		if (!isWhite) {
			for (const auto& wildcard : Wildcard_WhiteList) {
				if (strings::MatchWildcardNoCase(ucase, wildcard)) {
					isWhite = true;
					break;
				}
			}
		}
		return isWhite;
	}

	return true;
}
}
}
}