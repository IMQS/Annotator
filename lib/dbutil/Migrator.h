#pragma once

namespace imqs {
namespace dbutil {

/* Migrate a database.

The migrations are a list of SQL statements, separated by an empty string, and terminated
with nullptr.

Example:

	const char* migrations[] = {

		// migration 1
		"CREATE TABLE house (id BIGSERIAL PRIMARY KEY, value BIGINT)",
		"CREATE TABLE suburb (id BIGSERIAL PRIMARY KEY, name VARCHAR)",
		"",

		// migration 2
		"ALTER TABLE house ADD COLUMN num_rooms INT",
		"",

		// terminal
		nullptr,
	};

	err = MigrateDB(db, migrations);

Formally:
* Each migration is one or more strings of SQL statements
* Use an empty string to signify the end of a migration
* Use nullptr to signify the end of all migrations

All migrations are run inside a single transaction.

The system uses a table called migration_version, with one row inside it,
to record the current migration version.

It is legal to call MigrateDB with migrations = {nullptr} (ie just one nullptr entry, which is the terminal entry).
Doing this will cause the system to initialize itself, by creating the migration_version table,
and inserting a record into it, marking that we are at migration 0. This can be useful for 
abnormal cases, where you need to manipulate the migration version manually. It was created
to bootstrap a set of new migrations, which replaced an older, different, migration system.

*/
Error MigrateDB(dba::Conn* db, const char** migrations, uberlog::Logger* log);

// Create the DB if it does not already exist. Then, connect to it, and make sure it is up to date
enum class CreateFlags {
	None    = 0,
	Spatial = 1, // When creating a postgres database, install PostGIS
};

inline uint32_t    operator&(CreateFlags a, CreateFlags b) { return (uint32_t) a & (uint32_t) b; }
inline CreateFlags operator|(CreateFlags a, CreateFlags b) { return (CreateFlags)((uint32_t) a | (uint32_t) b); }

Error CreateAndMigrateDB(const dba::ConnDesc& desc, const char** migrations, uberlog::Logger* log, CreateFlags flags, dba::Conn*& db);

} // namespace dbutil
} // namespace imqs