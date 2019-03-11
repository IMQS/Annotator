#include "pch.h"
#include "ParserUtils.h"

namespace imqs {
namespace dba {
namespace sqlparser {

IMQS_DBA_API SqlStatementType DetectStatementType(const char* sql) {
	char buf[15];

	// eat up opening parens and spaces
	size_t offset = 0;
	for (; sql[offset] == '(' || sql[offset] == ' '; offset++) {
	}

	// place the first few meaningful letters into 'buf'
	size_t len = 0;
	for (; sql[offset + len] && len < arraysize(buf) - 1; len++) {
	}
	modp_toupper_copy(buf, sql + offset, len);

	if (memcmp(buf, "INSERT", 6) == 0)
		return SqlStatementType::Insert;

	if (memcmp(buf, "SELECT", 6) == 0)
		return SqlStatementType::Select;

	return SqlStatementType::Expression;
}
} // namespace sqlparser
} // namespace dba
} // namespace imqs
