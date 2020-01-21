#pragma once

namespace imqs {
namespace dba {
namespace sqlparser {
class SqlAST;

/* Verifies the safety of an SQL expression (which appears behind a WHERE statement).

A note on the "1=1 vulnerability". This code originally disallowed the construct "1=1",
on the premise that it allows an attacker to execute a query that will scan the entire table.
This is a concept that you can never truly guard against. Your attacker knows
a lot about the data that he is querying, so he could simply use an equivalent condition
that he knows is true for the entire table. For example, Pipe_Diameter >= 0.
Alternatively, he could use any of our APIs which already allow one to fetch arbitrary
data from a table. [BMH 2015-20-27].

*/
class IMQS_DBA_API Verifier {
public:
	static bool IsSafeToExecuteExpression(const char* input);

private:
	static bool IsSafeToExecuteExpressionAST(const SqlAST* node);

	static const ohash::set<std::string>  Exact_WhiteList;
	static const std::vector<std::string> Wildcard_WhiteList;
};
} // namespace sqlparser
} // namespace dba
} // namespace imqs
