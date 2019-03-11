#pragma once

namespace imqs {
namespace dba {
namespace sqlparser {

enum class SqlStatementType {
	Null,
	Insert,
	Select,
	Expression, // Not strictly a 'statement'
};

IMQS_DBA_API SqlStatementType DetectStatementType(const char* sql);
}
}
}
