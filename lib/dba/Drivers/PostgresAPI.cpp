#include "pch.h"
#include "PostgresAPI.h"
#include "../SqlStr.h"
#include "../Conn.h"
#include "../Stmt.h"

namespace imqs {
namespace dba {
namespace postgres {

IMQS_DBA_API Error CopyIn(Executor* ex, const std::string& table, const std::vector<std::string>& fields, Stmt& stmt) {
	auto s = ex->Sql();
	s.Fmt("COPY %Q (", table);
	for (const auto& f : fields)
		s.Fmt("%Q,", f);
	s.Chop();
	s += ") FROM STDIN WITH (FORMAT BINARY)";
	return ex->Prepare(s, fields.size(), nullptr, stmt);
}

} // namespace postgres
} // namespace dba
} // namespace imqs