#include "pch.h"
#include "LabelDB.h"

using namespace std;

namespace imqs {
namespace label {

// I added the label.intensity column in order to represent the severity of tar defects.
// I thought that "intensity" is a little more generic than the word "severity".
// At the same time, I thought it good to rename 'value' to 'category', so that every
// label has (category, intensity). The old word 'value' now feels too vague, after
// adding intensity too.

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
    "",
    "ALTER TABLE label ADD COLUMN intensity REAL",
    "",
    "ALTER TABLE label RENAME COLUMN value TO category",
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

// This was once-off code for merging two databases.
// I tried, but couldn't figure out how to do it in the sqlite app.
// This was used on 2019-05-15. The plan going forward is to make the labeller work
// over the web, so that we never have to do this again.
// NOTE: This code won't work since 2019-01-08, when I renamed label.value to label.category.
Error LabelDB::MergeOnceOff() {
	dba::ConnDesc d1;
	dba::Conn*    mergeDB;
	dba::Conn*    srcDB;
	auto          err = dba::Glob.Open("sqlite3:_:0:/home/ben/tmp/ai-sync/merged.sqlite:user:password", mergeDB);
	if (!err.OK())
		return err;
	err = dba::Glob.Open("sqlite3:_:0:/home/ben/tmp/ai-sync/joburg.sqlite:user:password", srcDB);
	if (!err.OK())
		return err;
	dba::ConnAutoCloser closer1(mergeDB);
	dba::ConnAutoCloser closer2(srcDB);

	struct Label {
		string ImagePath;
		string Value;
		string ModifiedAt;
	};

	auto          rows = srcDB->Query("SELECT image_path, value, label.modified_at FROM sample INNER JOIN label ON sample.id = label.sample_id WHERE dimension = 'tar_vci' AND label.author = 'Adhnaan'");
	vector<Label> labels;
	for (auto row : rows) {
		Label lab;
		err = row.Scan(lab.ImagePath, lab.Value, lab.ModifiedAt);
		if (!err.OK())
			return err;
		labels.push_back(move(lab));
	}
	if (!rows.OK())
		return rows.Err();

	dba::Tx* tx;
	err = mergeDB->Begin(tx);
	if (!rows.OK())
		return rows.Err();
	dba::TxAutoCloser autoCloser(tx);

	size_t nSamplesCreated = 0;

	for (auto lab : labels) {
		int64_t id;
		err = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = $1 AND region_id = 0", {lab.ImagePath}, id);
		if (err == ErrEOF) {
			// create sample
			nSamplesCreated++;
			err = tx->Exec("INSERT INTO sample (image_path, region_id, author) VALUES ($1, 0, 'Adhnaan')", {lab.ImagePath});
			if (!err.OK())
				return err;
			err = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = $1 AND region_id = 0", {lab.ImagePath}, id);
			if (!err.OK())
				return err;
		} else if (!err.OK()) {
			tsf::print("WHAT %v, %v\n", err.Message(), lab.ImagePath);
			return err;
		}

		err = tx->Exec("INSERT OR REPLACE INTO label (sample_id, dimension, value, author, modified_at) VALUES (?, ?, ?, ?, ?)",
		               {id, "tar_vci", lab.Value, "Adhnaan", lab.ModifiedAt});
		if (!err.OK())
			return err;
	}

	tsf::print("Created %v samples\n", nSamplesCreated);

	return tx->Commit();
}

} // namespace label
} // namespace imqs