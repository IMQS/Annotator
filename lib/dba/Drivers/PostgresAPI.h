#pragma once

namespace imqs {
namespace dba {
class SqlStr;
class Stmt;
class Executor;
namespace postgres {

// Start a COPY IN statement
IMQS_DBA_API Error CopyIn(Executor* ex, const std::string& table, const std::vector<std::string>& fields, Stmt& stmt);

} // namespace postgres
} // namespace dba
} // namespace imqs