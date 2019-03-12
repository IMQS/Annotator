#pragma once

#include "SchemaDiff.h"

namespace imqs {
namespace dbutil {

// This is written only to emit Postgres schema changes, but it shouldn't be hard
// to adapt it to other databases. There was some temptation to try and reuse the logic
// inside the SchemaWriter classes of dba, but I've chosen instead to rebuild the code
// separately here, to maintain clarity. Database syntax doesn't change often, so there
// will not be a lot of churn on this code.
class SchemaDiffOutputSql : public ISchemaDiffOutput {
public:
	dba::SqlStr Output;

	// Pass this a dba driver name, such as "postgres" (as written above, postgres is the only supported syntax right now)
	SchemaDiffOutputSql(std::string driver);

	Error CreateTable(std::string tableSpace, const dba::schema::Table& table) override;
	Error CreateField(std::string tableSpace, std::string table, const dba::schema::Field& field) override;
	Error CreateIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override;
	Error AlterFieldType(std::string tableSpace, std::string table, const dba::schema::Field& field) override;
	Error AlterFieldName(std::string tableSpace, std::string table, std::string oldName, std::string newName) override;
	Error DropTable(std::string tableSpace, std::string table) override;
	Error DropField(std::string tableSpace, std::string table, std::string field) override;
	Error DropIndex(std::string tableSpace, std::string table, const dba::schema::Index& idx) override;
};

} // namespace dbutil
} // namespace imqs