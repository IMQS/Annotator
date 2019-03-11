#pragma once
#include "../Generated/Parser.h"
#include "../../Attrib.h"

namespace imqs {
namespace dba {
namespace sqlparser {

// Tools for evaluating SQL expressions
class IMQS_DBA_API Evaluator {
public:
	// Evaluate one record against the given AST, recursively calling itself while walking through the AST as and when needed.
	static Attrib Evaluate(const SqlAST* ast, std::function<Attrib(const char*)> variableResolver);

private:
	static int CompareAny(const Attrib& a, const Attrib& b);
};
}
}
}