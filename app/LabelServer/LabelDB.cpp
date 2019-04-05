#include "pch.h"
#include "LabelDB.h"

using namespace std;

namespace imqs {
namespace label {

const char* LabelDB::Migrations[] = {
    "CREATE TABLE sample (id INTEGER PRIMARY KEY, image_path TEXT NOT NULL)",
    "CREATE UNIQUE INDEX idx_sample_image_path ON sample (image_path)",
    "CREATE TABLE label (sample_id INTEGER NOT NULL, dimension TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY(sample_id, dimension))",
    "",
    "ALTER TABLE label ADD COLUMN author TEXT",
    "ALTER TABLE label ADD COLUMN modified_at TIMESTAMP",
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