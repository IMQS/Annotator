#include "pch.h"
#include "LabelDB.h"

using namespace std;

namespace imqs {
namespace label {

// We can't use a null region_id to indicate "whole image", because if we do that,
// then our unique constraint on (image_path, region_id) will not be enforced for
// multiple copies of the same image_path, with region_id being null. Basically,
// if you want a unique constraint to work, then the values in all of the fields
// in that constraint must always be non-null.
const char* LabelDB::Migrations[] = {
    "CREATE TABLE sample (id INTEGER PRIMARY KEY, image_path TEXT NOT NULL)",
    "CREATE UNIQUE INDEX idx_sample_image_path ON sample (image_path)",
    "CREATE TABLE label (sample_id INTEGER NOT NULL, dimension TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(sample_id, dimension))",
    "",
    "ALTER TABLE label ADD COLUMN author TEXT",
    "ALTER TABLE label ADD COLUMN modified_at TIMESTAMP",
    "",
    "ALTER TABLE sample ADD COLUMN region_id INTEGER",
    "ALTER TABLE sample ADD COLUMN region TEXT",
    "ALTER TABLE sample ADD COLUMN author TEXT",
    "ALTER TABLE sample ADD COLUMN modified_at TIMESTAMP",
    "UPDATE sample SET region_id = 0",
    "CREATE TABLE sample2 (id INTEGER PRIMARY KEY, image_path TEXT NOT NULL, region_id INTEGER NOT NULL, region TEXT, author TEXT, modified_at TIMESTAMP)",
    "INSERT INTO sample2 SELECT * FROM sample",
    "DROP TABLE sample",
    "ALTER TABLE sample2 RENAME TO sample",
    "CREATE UNIQUE INDEX idx_sample_image_path_region_id ON sample (image_path, region_id)",
    nullptr,
};

LabelDB::~LabelDB() {
	Close();
}

void LabelDB::Close() {
	if (DB) {
		DB->Close();
		DB = nullptr;
	}
}

Error LabelDB::Open(uberlog::Logger* log, std::string rootDir) {
	dba::ConnDesc d;
	d.Driver   = "sqlite3";
	d.Database = path::Join(rootDir, "labels.sqlite");
	return dbutil::CreateAndMigrateDB(d, Migrations, log, dbutil::CreateFlags::None, DB);
}

} // namespace label
} // namespace imqs