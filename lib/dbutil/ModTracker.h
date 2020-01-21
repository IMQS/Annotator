#pragma once

namespace imqs {
namespace dbutil {

struct ModStamp {
	union {
		uint8_t  Bytes[16];
		uint32_t DWords[4];
		uint64_t QWords[2];
	};

	ModStamp() {
		QWords[0] = 0;
		QWords[1] = 0;
	}
	ModStamp(uint64_t q1, uint64_t q2) {
		QWords[0] = q1;
		QWords[1] = q2;
	}

	bool IsNull() const {
		return QWords[0] == 0 && QWords[1] == 0;
	}

	bool operator==(const ModStamp& b) const { return QWords[0] == b.QWords[0] && QWords[1] == b.QWords[1]; }
	bool operator!=(const ModStamp& b) const { return !(*this == b); }

	bool operator==(const hash::Sig16& b) const { return QWords[0] == b.QWords[0] && QWords[1] == b.QWords[1]; }
	bool operator!=(const hash::Sig16& b) const { return !(*this == b); }

	operator hash::Sig16() const { return {QWords[0], QWords[1]}; }

	// Returns a 32 character hex string
	std::string ToHex() const { return strings::ToHex(Bytes, sizeof(Bytes)); }

	static hash::Sig16 Combine16(const std::vector<ModStamp>& stamps, uint64_t seed1 = 0, uint64_t seed2 = 0);
};

/* Modification Tracker

This embodies a system that we use mostly for GIS-related operations, where caching is
vital. The ModTracker is just two tables inside a database. The first is a "meta" table,
which stores a GUID for that database. It has only one row inside it.
The second is the more interesting table, and it contains one row for every table that
it is tracking. There are no triggers or anything of that nature. It is up to every
data writer to keep the modtrack tables up to date (preferably inside transactions).
Every tracked table has two integers associated with it. The first is the "createcount",
which is intended to record the number of times that the table has been recreated
(or wiped). The second is the stamp, which is the number of times the table has been
inserted, updated, or deleted. I don't remember now why we needed the create count,
but basically you can treat those two numbers as monotonically increasing, and all
we really do with them is compute a hash on them, to return a 16-byte modstamp that
is supposed to represent the state of the table at that particular point in time.
The table name is also baked into the modstamp for that table.

Almost all of our read operations need to combine the unique stamp of the database
with the integer stamp of the table(s) in question. Since these are stored in different
tables, we need to combine them with the following hackish query. This hack is well
worth it, because it means we only need to execute a single query, and we get to remain
100% stateless.

select 0 as type, identity, 0 as stamp from modtrack_meta
union
select 1 as type, 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa' as identity, stamp from modtrack_tables where tablename = 'SewerAuxMyCity';

*/
class ModTracker {
public:
	static StaticError ErrNotInstalled;

	// This is a special modstamp that is returned by GetTableStamp()/GetTableStamps() if that table has no entry in the ModTracker table.
	// Bytes are 1337C0D3DEADBEEF BAAAAAADF000000D
	static const ModStamp NotFound;

	// If the database supports SchemaReader, then this is safe to call inside a transaction.
	// If the database does not support SchemaReader, and ModTracker is not installed, and you
	// call this inside a transaction, then you'll get a failed statement (and thus probably,
	// depending on your DB, a failed transaction), because we had no other way of determining
	// whether ModTracker is active.
	static Error Install(dba::Executor* ex);

	// Uses SchemaReader to check whether ModTracker is installed in the DB.
	// This makes it possible to run this operation inside a transaction.
	// If we used a more crude method, such as relying on an error from a statement such as "SELECT * from modtrack_meta",
	// and ModTrack was not installed, then the failed statement would cause the transaction to fail.
	static Error CheckInstallStatus(dba::Executor* ex, bool& isInstalled, int& version);

	static Error GetTableStamp(dba::Executor* ex, const std::string& table, ModStamp& stamp);

	// Upon return, len(stamps) = len(tables), and stamps[i] is filled with NotFound, if that table has no modstamp.
	// The order of the stamps inside 'stamps' is identical to the order of the tables inside 'tables'.
	// If there is no ModTracker installed in the DB, then ErrNotInstalled is returned.
	static Error GetTablesStamps(dba::Executor* ex, const std::vector<std::string>& tables, std::vector<ModStamp>& stamps);

	// Get the stamps for all tables. The resulting 'tables' and 'stamps' vectors are parallel.
	static Error GetAllStamps(dba::Executor* ex, std::vector<std::string>& tables, std::vector<ModStamp>& stamps);

	static Error IncrementTableStamp(dba::Executor* ex, const std::string& table);
	static Error IncrementTableStamps(dba::Executor* ex, const std::vector<std::string>& tables);

private:
	static Error GetStampsInternal(dba::Executor* ex, const std::vector<std::string>& limitTables, std::vector<std::string>* outTables, std::vector<ModStamp>& stamps);
};
} // namespace dbutil
} // namespace imqs
