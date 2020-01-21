#pragma once

#include "../Schema/DB.h"

namespace imqs {
namespace dba {
class Executor;
class Conn;

/* A SchemaReader can read the schema out of a database.
The 'tableSpace' referred to here is what most databases call a "schema".
We use the term "table space" to avoid confusion with the term "schema" that we already use a lot.
If tableSpace is empty or null, then we use the "default" tablespace. For Postgres, this is "public".
For Microsoft SQL Server, this is "dbo".

A common need is to read only a subset of tables, and that is why restrictTables is provided as an optional
parameter. If this doesn't seem sufficient, because you want to filter tables based on a regex or something
like that, then the following pattern should work well enough:

* Read all tables, but don't read fields or indexes. This is generally very fast - it is just one database query.
* Produce your own filtered list of tables that you're really interested in.
* Read just that filtered list, this time including the Fields and Indexes flags.

If you want to read only a few tables inside a specific tableSpace, then specify that inside restrictTables.
For example: ["water.pipes", "water.nodes"], to read only those two tables inside the "water" schema.

*/
class IMQS_DBA_API SchemaReader {
public:
	enum ReadFlags {
		ReadFlagFields  = 1,
		ReadFlagIndexes = 2,
	};

	// Read the tables, fields, indexes, etc.
	// If restrictTables is not null, then limit the query to only those tables specified.
	virtual Error ReadSchema(uint32_t readFlags, Executor* ex, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace) = 0;

	// Start a transaction, then read the schema inside that transaction
	Error ReadSchemaInTx(uint32_t readFlags, Conn* con, schema::DB& db, const std::vector<std::string>* restrictTables, std::string tableSpace);
};
} // namespace dba
} // namespace imqs