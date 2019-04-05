#include "pch.h"
#include "Server.h"

using namespace std;

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

Error Server::ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	auto image  = r->QueryVal("image");
	auto dim    = r->QueryVal("dimension");
	auto val    = r->QueryVal("value");
	auto author = r->QueryVal("author");
	if (image == "")
		return Error("image not be empty");

	if (dim == "")
		return Error("dimension may not be empty");

	if (author == "")
		return Error("author may not be empty");

	bool isDelete = val == "";

	auto err = tx->Exec("INSERT OR IGNORE INTO sample (image_path) VALUES (?)", {image});
	if (!err.OK())
		return err;
	int64_t id = 0;
	err        = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = ?", {image}, id);
	if (!err.OK())
		return err;

	if (isDelete)
		err = tx->Exec("DELETE FROM label WHERE sample_id = ? AND dimension = ?", {id, dim});
	else
		err = tx->Exec("INSERT OR REPLACE INTO label (sample_id, dimension, value, author, modified_at) VALUES (?, ?, ?, ?, ?)",
		               {id, dim, val, author, time::Now()});

	if (!err.OK())
		return err;

	return Error();
}

Error Server::ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	int64_t id  = 0;
	auto    err = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = ?", {r->QueryVal("image")}, id);
	if (err == ErrEOF) {
		SendJson(w, nlohmann::json::object());
		return Error();
	}
	nlohmann::json resp;
	auto           rows = tx->Query("SELECT dimension, value FROM label WHERE sample_id = ?", {id});
	for (auto row : rows) {
		string dim;
		string val;
		err = row.Scan(dim, val);
		if (!err.OK())
			return err;
		resp[dim] = val;
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
	else if (ext == ".js")
		w.SetHeader("Content-Type", "text/javascript");
	else if (ext == ".js.map")
		w.SetHeader("Content-Type", "application/json");
	w.Status = 200;
}

} // namespace label
} // namespace imqs