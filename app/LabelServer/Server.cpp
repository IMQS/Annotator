#include "pch.h"
#include "Server.h"

using namespace std;

namespace imqs {
namespace label {

Error Server::Initialize(uberlog::Logger* log, std::string photoDir, std::string dimensionsFile) {
	Log = log;
	DB.Close();

	// chop trailing slash
	if (strings::EndsWith(photoDir, "/") || strings::EndsWith(photoDir, "\\"))
		photoDir.erase(photoDir.end());
	PhotoRoot = photoDir;

	auto err = DB.Open(log, photoDir);
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
	Log->Info("Found %v photos in %v", AllPhotos.size(), photoDir);
	return Error();
}

void Server::ListenAndRun(int port) {
	phttp::Server server;
	server.Log = make_shared<PHttpLogBridge>(Log);
	server.ListenAndRun("*", port, [&](phttp::Response& w, phttp::RequestPtr r) {
		if (r->Path == "/") {
			w.Body = "Hello " + r->Path;
		} else if (r->Path == "/api/dimensions") {
			SendJson(w, DimensionsRaw);
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
			w.Status = 404;
		}
	});
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
	/*
	Dimensions.clear();
	for (const auto& jd : j.items()) {
		if (!jd.value().is_array())
			return Error::Fmt("Expected array for dimension %v", jd.key());
		Dimension dim;
		for (const auto& val : jd.value()) {
			if (val.is_number())
				dim.Values.push_back(tsf::fmt("%.0f", val.get<double>()));
			else
				dim.Values.push_back(val.get<string>());
		}
		Dimensions.insert(jd.key(), dim);
	}
	*/
	return Error();
}

Error Server::ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	auto image = r->QueryVal("image");
	auto dim   = r->QueryVal("dimension");
	auto val   = r->QueryInt("value");
	if (image == "")
		return Error("image not be empty");

	if (dim == "")
		return Error("dimension may not be empty");

	if (r->QueryVal("value") == "") {
		// in future, we could allow this to be a label delete operation
		return Error("value may not be empty");
	}

	auto err = tx->Exec("INSERT OR IGNORE INTO sample (image_path) VALUES (?)", {r->QueryVal("image")});
	if (!err.OK())
		return err;
	int64_t id = 0;
	err        = dba::CrudOps::Query(tx, "SELECT id FROM sample WHERE image_path = ?", {r->QueryVal("image")}, id);
	if (!err.OK())
		return err;

	err = tx->Exec("INSERT OR REPLACE INTO label (sample_id, dimension, value) VALUES (?, ?, ?)", {id, r->QueryVal("dimension"), r->QueryVal("value")});
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
		int    val;
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

} // namespace label
} // namespace imqs