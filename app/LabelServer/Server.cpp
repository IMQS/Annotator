#include "pch.h"
#include "Server.h"

using namespace std;

/*
Some debug tests
curl "localhost:8080/api/db/get_labels?image=2019/2019-03-08/100GOPRO/G0013117.JPG"

Example response from /api/db/get_labels:
{
	"regions": {
		"0": {
			"dims": {
				"gravel_base_stones": "3"
			}
		},
		"1": {
			"region": "[[12,13,56,34,89,23]]",
			"dims": {
				"traffic_sign": "stop",
				"traffic_sign_quality": "2",
			}
		}
	}
}

*/

namespace imqs {
namespace label {

Error Server::Initialize(uberlog::Logger* log, std::string photoDir, std::string dimensionsFile) {
	Log = log;
	DB.Close();

	// figure out static file directory
	StaticRoot           = ".";
	vector<string> sDirs = {
	    path::Join(path::Dir(os::ProcessPath()), "dist"),
	    // go from Annotator/t2-output/linux-clang-debug-default to Annotator/apps/photo-label/dist
	    path::Join(path::Dir(path::Dir(path::Dir(os::ProcessPath()))), "app", "photo-label", "dist"),
	};
	for (auto s : sDirs) {
		if (os::IsDir(s)) {
			StaticRoot = s;
			break;
		}
	}
	if (!os::IsDir(StaticRoot))
		return Error::Fmt("Can't find static files directory for serving up the Vue app. Searched in [%v]", strings::Join(sDirs, ","));

	// chop trailing slash
	if (strings::EndsWith(photoDir, "/") || strings::EndsWith(photoDir, "\\"))
		photoDir.erase(photoDir.end() - 1);
	PhotoRoot = photoDir;

	Log->Info("Scanning photos in %v", photoDir);

	// Find all directories inside PhotoRoot. Each one of these is a 'dataset'
	// We go one level deep.
	AllDatasets.clear();
	auto err = os::FindFiles(photoDir, [&](const os::FindFileItem& item) -> bool {
		if (item.IsDir) {
			auto relDir = item.FullPath().substr(photoDir.size() + 1);
			relDir      = strings::Replace(relDir, "\\", "/");
			auto depth  = strings::Split(relDir, '/').size();
			//tsf::print("%-30v %v\n", relDir, item.FullPath());
			AllDatasets.push_back(relDir);
			return depth <= 1;
		} else {
			return true;
		}
	});
	if (!err.OK())
		return err;

	err = DB.Open(log, photoDir);
	if (!err.OK())
		return err;

	err = LoadDimensionsFile(dimensionsFile);
	if (!err.OK())
		return err;

	AllPhotos.clear();
	err = os::FindFiles(photoDir, [&](const os::FindFileItem& item) -> bool {
		if (!item.IsDir) {
			auto lc = strings::tolower(item.Name);
			if (strings::EndsWith(lc, ".jpg") || strings::EndsWith(lc, ".jpeg") || strings::EndsWith(lc, ".png")) {
				auto relPath = item.FullPath().substr(photoDir.size() + 1);
				AllPhotos.push_back(relPath);
			}
		}
		return true;
	});
	if (!err.OK())
		return err;
	sort(AllPhotos.begin(), AllPhotos.end());
	Log->Info("Found %v photos in %v", AllPhotos.size(), photoDir);
	return Error();
}

void Server::ListenAndRun(int port) {
	phttp::Server server;
	server.Log = make_shared<PHttpLogBridge>(Log);
	server.ListenAndRun("*", port, [&](phttp::Response& w, phttp::RequestPtr r) {
		if (r->Path == "/api/dimensions") {
			SendJson(w, DimensionsRaw);
		} else if (r->Path == "/api/datasets") {
			SendJson(w, AllDatasets);
		} else if (r->Path == "/api/list_images") {
			SendJson(w, AllPhotos);
		} else if (r->Path == "/api/get_image") {
			SendFile(w, path::SafeJoin(PhotoRoot, r->QueryVal("image")));
		} else if (strings::StartsWith(r->Path, "/api/db/")) {
			dba::Tx* tx  = nullptr;
			auto     err = DB.DB->Begin(tx);
			if (!err.OK()) {
				w.SetStatusAndBody(500, err.Message());
				return;
			}
			dba::TxAutoCloser txCloser(tx);
			if (r->Path == "/api/db/set_label" && r->Method == "POST") {
				err = ApiSetLabel(w, r, tx);
			} else if (r->Path == "/api/db/get_labels") {
				err = ApiGetLabels(w, r, tx);
			} else {
				w.Status = 404;
				return;
			}

			if (err.OK())
				err = tx->Commit();

			if (!err.OK() && w.Body == "" && w.Status == 0) {
				w.SetStatusAndBody(500, err.Message());
				return;
			}
			if (err.OK() && w.Body == "" && w.Status == 0)
				w.SetStatusAndBody(200, "OK");
		} else {
			ServeStatic(w, r);
		}
	});
}

static Error FindAllExtensionsInPath(string dir, ohash::set<string>& allExt) {
	auto err = os::FindFiles(dir, [&](const os::FindFileItem& item) -> bool {
		if (item.IsDir)
			return true;
		allExt.insert(strings::tolower(path::Extension(item.Name)));
		return true;
	});
	return err;
}

Error Server::Export(std::string exportDir) {
	// Check that the export directory contains ONLY jpegs, to safeguard the user against sending
	// the wrong path here, and we end up wiping his OS or something.
	ohash::set<string> allExt;
	auto               err = FindAllExtensionsInPath(exportDir, allExt);
	if (os::IsNotExist(err))
		err = Error();
	else if (!err.OK())
		return err;
	for (auto ext : allExt) {
		if (ext != ".jpg" && ext != ".jpeg")
			return Error::Fmt("Unexpected file extensions (%v) found in %v. You must delete the directory manually before exporting");
	}

	err = os::RemoveAll(exportDir);
	if (!err.OK())
		return err;
	err = os::MkDirAll(exportDir);
	if (!err.OK())
		return err;

	dba::Tx* tx = nullptr;
	err         = DB.DB->Begin(tx);
	if (!err.OK())
		return err;
	dba::TxAutoCloser txCloser(tx);
	auto              rows = tx->Query("SELECT sample.image_path, label.dimension, label.value FROM sample INNER JOIN label ON sample.id = label.sample_id");
	size_t            nth  = 0;
	for (auto row : rows) {
		nth++;
		string path, dim, lab;
		err = row.Scan(path, dim, lab);
		if (!err.OK())
			return err;
		auto dir = path::Join(exportDir, dim, lab);
		err      = os::MkDirAll(dir);
		if (!err.OK())
			return err;
		auto shortName = path::ChangeExtension(path::Filename(path), "");
		auto extension = path::Extension(path);
		auto pathHash  = tsf::fmt("%08x", XXH64(path.c_str(), path.size(), 0));
		// PhotoRoot example: /stuff/mldata
		// path example:      2019/2019-02-27/148GOPRO/G0024551.JPG
		// shortName:         G0024551
		// pathHash:          deadbeefdeadbeef
		// dir example:       /stuff/train/road_type/gravel
		// desired output:    ln -s /stuff/mldata/2019/2019-02-27/148GOPRO/G0024551.JPG /stuff/train/road_type/gravel/G0024551_deadbeefdeadbeef.JPG
		auto cmd = tsf::fmt("ln -s '%v' '%v'", path::Join(PhotoRoot, path), path::Join(dir, shortName) + "_" + pathHash + extension);
		if (nth % 100 == 0)
			tsf::print("%v, %v, %v\n", path, dim, lab);
		//tsf::print("%v\n", cmd);
		int ret = system(cmd.c_str());
		if (ret != 0)
			return Error::Fmt("Failed to create symlink: '%v': %d", cmd, ret);
	}
	if (!rows.OK())
		return rows.Err();
	return Error();
}

void Server::SendJson(phttp::Response& w, const nlohmann::json& j) {
	w.SetHeader("Content-Type", "application/json");
	w.Body = j.dump();
}

void Server::SendFile(phttp::Response& w, std::string filename) {
	string ext = strings::tolower(path::Extension(filename));
	if (ext == ".jpeg" || ext == ".jpg") {
		w.SetHeader("Content-Type", "image/jpeg");
	} else if (ext == ".png") {
		w.SetHeader("Content-Type", "image/png");
	} else {
		w.SetStatusAndBody(400, "Unknown file type " + filename);
		return;
	}
	auto err = os::ReadWholeFile(filename, w.Body);
	if (!err.OK()) {
		w.SetStatusAndBody(404, tsf::fmt("Error reading %v: %v", filename, err.Message()));
		return;
	}
}

// See example in readme.md
Error Server::LoadDimensionsFile(std::string dimensionsFile) {
	nlohmann::json j;
	auto           err = nj::ParseFile(dimensionsFile, j);
	if (!err.OK())
		return err;
	DimensionsRaw = j;
	//Dimensions.clear();
	//for (const auto& jd : j.items()) {
	//	if (!jd.value().is_array())
	//		return Error::Fmt("Expected array for dimension %v", jd.key());
	//	Dimension dim;
	//	for (const auto& val : jd.value()) {
	//		if (val.is_number())
	//			dim.Values.push_back(tsf::fmt("%.0f", val.get<double>()));
	//		else
	//			dim.Values.push_back(val.get<string>());
	//	}
	//	Dimensions.insert(jd.key(), dim);
	//}
	return Error();
}

// Possible region combinations and their meanings
// |-----------|-----------|------------------------------------------|
// | region_id | region    | action                                   |
// |-----------|-----------|------------------------------------------|
// | non-zero  | empty     | Delete this region and all labels for it |
// | non-zero  | non-empty | Update an existing region                |
// | zero      | empty     | Label applies to the entire image        |
// | zero      | non-empty | Create new region                        |
// |-----------|-----------|------------------------------------------|
// Why do we insist on using zero as a null value for RegionID, instead of
// just using SQL NULL?
// The reason is because the UNIQUE constraint on (image_path, region_id) doesn't
// get enforced if region_id is NULL. Specifically, you can insert multiple records
// of the form (img_path_x, null).
// From the SQLite docs:
//     For the purposes of unique indices, all NULL values are considered different from
//     all other NULL values and are thus unique. This is one of the two possible
//     interpretations of the SQL-92 standard (the language in the standard is ambiguous)
//     and is the interpretation followed by PostgreSQL, MySQL, Firebird, and Oracle.
//     Informix and Microsoft SQL Server follow the other interpretation of the standard.
// Return: The ID of the region.
Error Server::ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	auto image    = r->QueryVal("image");
	auto regionID = r->QueryInt64("region_id");
	auto region   = r->QueryVal("region");
	auto dim      = r->QueryVal("dimension");
	auto val      = r->QueryVal("value");
	auto author   = r->QueryVal("author");
	if (image == "")
		return Error("image not be empty");

	if (dim == "")
		return Error("dimension may not be empty");

	if (author == "")
		return Error("author may not be empty");

	// See the truth table in the comments to this function
	enum class RegionMode {
		Delete,
		Update,
		WholeImage,
		Create,
	} regionMode;
	if (regionID != 0 && region == "")
		regionMode = RegionMode::Delete;
	else if (regionID != 0 && region != "")
		regionMode = RegionMode::Update;
	else if (regionID == 0 && region == "")
		regionMode = RegionMode::WholeImage;
	else if (regionID == 0 && region != "")
		regionMode = RegionMode::Create;
	else
		IMQS_DIE(); // this should be unreachable

	int64_t sampleID        = 0;
	bool    deleteThisLabel = val == ""; // delete this label (but not necessarily the region)

	if (regionMode == RegionMode::WholeImage) {
		auto err = tx->Exec("INSERT OR IGNORE INTO sample (image_path, region_id) VALUES (?, 0)", {image});
		if (!err.OK())
			return err;
	} else if (regionMode == RegionMode::Create) {
		int64_t maxRegionID = 0;
		auto    err         = dba::CrudOps::Query(tx, "SELECT max(region_id) FROM sample WHERE image_path = ?", {image}, maxRegionID);
		if (!err.OK())
			return err;
		regionID = maxRegionID + 1;
		err      = tx->Exec("INSERT INTO sample (image_path, region_id, region, author, modified_at) VALUES (?, ?, ?, ?, ?)", {image, regionID, region, author, time::Now()});
		if (!err.OK())
			return err;
	} else if (regionMode == RegionMode::Update) {
		auto err = tx->Exec("UPDATE sample SET region = ?, author = ?, modified_at = ? WHERE image_path = ? AND region_id = ?", {region, author, time::Now(), image, regionID});
		if (!err.OK())
			return err;
	} else if (regionMode == RegionMode::Delete) {
		// Nothing to do here, we'll take action later
	}

	auto err = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = ? AND region_id = ?", {image, regionID}, sampleID);
	if (!err.OK())
		return err;

	if (regionMode == RegionMode::Delete) {
		err = tx->Exec("DELETE FROM label WHERE sample_id = ?", {sampleID});
		if (!err.OK())
			return err;
		err = tx->Exec("DELETE FROM sample WHERE id = ?", {sampleID});
		if (!err.OK())
			return err;
	} else {
		Error err;
		if (deleteThisLabel)
			err = tx->Exec("DELETE FROM label WHERE sample_id = ? AND dimension = ?", {sampleID, dim});
		else
			err = tx->Exec("INSERT OR REPLACE INTO label (sample_id, dimension, value, author, modified_at) VALUES (?, ?, ?, ?, ?)",
			               {sampleID, dim, val, author, time::Now()});
		if (!err.OK())
			return err;
	}

	w.SetHeader("Content-Type", "text/plain");
	w.Body = tsf::fmt("%v", regionID);

	return Error();
}

Error Server::ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	string                       idList = "(";
	auto                         rows   = tx->Query("SELECT id, region_id, region FROM sample WHERE image_path = ?", {r->QueryVal("image")});
	int64_t                      nID    = 0;
	ohash::map<int64_t, int64_t> sampleIDToRegionID;
	ohash::map<int64_t, string>  sampleIDToRegion;
	for (auto row : rows) {
		int64_t sampleID = 0;
		int64_t regionID = 0;
		string  region;
		auto    err = row.Scan(sampleID, regionID, region);
		if (!err.OK())
			return err;
		sampleIDToRegionID.insert(sampleID, regionID);
		sampleIDToRegion.insert(sampleID, region);
		idList += tsf::fmt("%v,", sampleID);
		nID++;
	}
	if (!rows.OK())
		return rows.Err();
	idList.erase(idList.end() - 1);
	idList += ")";

	if (nID == 0) {
		SendJson(w, nlohmann::json::object());
		return Error();
	}

	nlohmann::json resp;
	rows           = tx->Query(tsf::fmt("SELECT sample_id, dimension, value FROM label WHERE sample_id IN %v", idList).c_str());
	auto& jRegions = resp["regions"];
	for (auto row : rows) {
		int64_t sampleID = 0;
		string  dim;
		string  val;
		auto    err = row.Scan(sampleID, dim, val);
		if (!err.OK())
			return err;
		auto regionID64 = sampleIDToRegionID.get(sampleID);
		auto region     = sampleIDToRegion.get(sampleID);
		auto regionID   = tsf::fmt("%v", regionID64);
		if (jRegions.find(regionID) == jRegions.end() && region != "") {
			jRegions[regionID]["region"] = region;
		}
		jRegions[regionID]["dims"][dim] = val;
	}
	if (!rows.OK())
		return rows.Err();
	SendJson(w, resp);
	return Error();
}

void Server::ServeStatic(phttp::Response& w, phttp::RequestPtr r) {
	auto clean = r->Path;

	// sanitize
	while (true) {
		size_t oldLen = clean.size();
		clean         = strings::Replace(clean, "..", "");
		clean         = strings::Replace(clean, "//", "/");
		if (clean.size() == oldLen)
			break;
	}

	if (clean == "/" || clean == "" || clean.find("/label/") == 0)
		clean = "index.html";

	auto file = path::Join(StaticRoot, clean);
	auto err  = os::ReadWholeFile(file, w.Body);
	if (!err.OK()) {
		if (os::IsNotExist(err))
			w.Status = 404;
		else
			w.SetStatusAndBody(400, err.Message());
		return;
	}

	auto ext = strings::tolower(path::Extension(clean));
	if (ext == ".jpg" || ext == ".jpeg")
		w.SetHeader("Content-Type", "image/jpeg");
	else if (ext == ".html")
		w.SetHeader("Content-Type", "text/html");
	else if (ext == ".ico")
		w.SetHeader("Content-Type", "image/icon");
	else if (ext == ".css")
		w.SetHeader("Content-Type", "text/css");
	else if (ext == ".svg")
		w.SetHeader("Content-Type", "image/svg+xml");
	else if (ext == ".js")
		w.SetHeader("Content-Type", "text/javascript");
	else if (ext == ".js.map")
		w.SetHeader("Content-Type", "application/json");
	w.Status = 200;
}

} // namespace label
} // namespace imqs