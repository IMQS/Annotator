#include "pch.h"
#include "Migrator.h"

namespace imqs {
namespace dbutil {

Error MigrateDB(dba::Conn* db, const char** migrations, uberlog::Logger* log) {
	int  version = 0;
	auto rows    = db->Query("SELECT version FROM migration_version");
	if (!rows.OK() && dba::IsTableNotFound(rows.Err())) {
		// empty dB
	} else if (!rows.OK()) {
		return rows.Err();
	} else {
		for (auto row : rows)
			version = row[0].ToInt32();
		if (!rows.OK())
			return rows.Err();
	}

	dba::Tx* tx;
	auto     err = db->Begin(tx);
	if (!err.OK())
		return err;

	if (version == 0) {
		// bootstrap - install our migration tracking tables. This is migration version '0'
		err |= tx->Exec("CREATE TABLE migration_version (version INTEGER)");
		err |= tx->Exec("INSERT INTO migration_version VALUES (0)");
		if (log)
			log->Info("Installing migration_version tables into %v: %v", db->Connection().Database, err.OK() ? "OK" : err.Message());
	}

	int  nrun      = 0;
	int  atversion = 0;
	int  lastlog   = 0;
	bool onblank   = true;
	for (size_t i = 0; migrations[i] && err.OK(); i++) {
		auto len = strlen(migrations[i]);
		if (len == 0) {
			// empty string: end of migration
			onblank = true;
			continue;
		} else {
			if (onblank) {
				// start of a new migration
				atversion++;
			}
			onblank = false;
		}

		if (version < atversion) {
			nrun++;
			if (log && lastlog != atversion) {
				lastlog = atversion;
				log->Info("Running migration %v %v", db->Connection().Database, atversion);
			}
			err = tx->Exec(migrations[i]);
		}
	}

	if (err.OK() && nrun != 0)
		err = tx->Exec("UPDATE migration_version SET version = $1", {atversion});

	if (err.OK())
		err = tx->Commit();
	else
		tx->Rollback();

	if (log && nrun != 0)
		log->Log(err.OK() ? uberlog::Level::Info : uberlog::Level::Error, "Migration of %v from %v to %v, status: %v", db->Connection().Database, version, atversion, err.OK() ? "OK" : err.Message());

	return err;
}

Error CreateAndMigrateDB(const dba::ConnDesc& desc, const char** migrations, uberlog::Logger* log, CreateFlags flags, dba::Conn*& db) {
	auto err = dba::Glob.Open(desc, db);
	if (!err.OK()) {
		if (desc.Driver == "postgres") {
			// For postgres, try connecting to the 'postgres' DB, and create the DB from there.
			auto base      = desc;
			base.Database  = "postgres";
			auto errCreate = dba::Glob.Open(base, db);
			if (!errCreate.OK())
				return err;
			errCreate = db->Exec(tsf::fmt("CREATE DATABASE %v OWNER %v", desc.Database, desc.Username).c_str());
			log->Info("Creating database %v: %v", desc.Database, errCreate.OK() ? "OK" : errCreate.Message());
			db->Close();
			db = nullptr;
			if (!errCreate.OK())
				return errCreate;

			// connect to the new DB
			err = dba::Glob.Open(desc, db);
			if (!err.OK())
				return err;
			if ((flags & CreateFlags::Spatial) != 0)
				err = db->Exec("CREATE EXTENSION POSTGIS");
			if (!err.OK()) {
				db->Close();
				db = nullptr;
				return err;
			}
		} else {
			return err;
		}
	}

	err = MigrateDB(db, migrations, log);
	if (!err.OK()) {
		db->Close();
		db = nullptr;
		return err;
	}

	return Error();
}

} // namespace dbutil
} // namespace imqs