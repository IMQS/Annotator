#include "pch.h"
#include "Server.h"
#include "CircleSolver.h"

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
			"region": "[[[12,13,56,34,89,23]]]",
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

	// testing code
	bool testCircleSolver = true;
	if (testCircleSolver) {
		string                                 pts = "";
		int                                    np  = 4;
		std::mt19937_64                        gen;
		std::uniform_real_distribution<double> disRad(-0.001, 0.001); // how much each radius is out by
		std::uniform_real_distribution<double> disTh(-0.1, 0.1);      // how much each theta is out by
		double                                 sx = 10;
		double                                 sy = 10;
		double                                 tx = -300;
		double                                 ty = -200;

		for (int i = 0; i < np; i++) {
			double th = ((double) i + disTh(gen)) * IMQS_PI * 2 / (double) np;
			double x  = sx * (cos(th) + disRad(gen)) + tx;
			double y  = sy * (sin(th) + disRad(gen)) + ty;
			//double x = sx * cos(th) + tx;
			//double y = sy * sin(th) + ty;
			pts += tsf::fmt("%.3f %.3f,", x, y);
		}
		pts.erase(pts.end() - 1, pts.end());
		//auto            r = phttp::Request::MockRequest("GET", "/api/solve", {{"circle_points", "-11.2 0,-9.1 0,-10 -1.1,-10 1.05"}});
		auto            r = phttp::Request::MockRequest("GET", "/api/solve", {{"circle_points", pts}});
		phttp::Response w;
		Solve(w, r);
	}

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

	Log->Info("Scanning datasets in %v", photoDir);

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
	Log->Info("Found %v datasets", AllDatasets.size());

	Log->Info("Opening database");
	err = DB.Open(log, photoDir);
	if (!err.OK())
		return err;

	Log->Info("Loading dimensions file");
	err = LoadDimensionsFile(dimensionsFile);
	if (!err.OK())
		return err;

	Log->Info("Scanning photos in %v", photoDir);
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

	//err = FindRectanglesOutsideImageBounds();
	//if (!err.OK())
	//	return err;

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
		} else if (r->Path == "/api/report") {
			Report(w, r);
		} else if (r->Path == "/api/solve") {
			Solve(w, r);
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
			} else if (r->Path == "/api/db/get_folder_summary") {
				err = ApiGetFolderSummary(w, r, tx);
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

void Server::SendJson(phttp::Response& w, const nlohmann::json& j) {
	w.SetHeader("Content-Type", "application/json");
	w.Body = j.dump(4);
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
	auto image     = r->QueryVal("image");
	auto regionID  = r->QueryInt64("region_id");
	auto region    = r->QueryVal("region");
	auto dim       = r->QueryVal("dimension");
	auto category  = r->QueryVal("category");
	auto intensity = r->QueryDbl("intensity");
	auto author    = r->QueryVal("author");
	if (image == "")
		return Error("image may not be empty");

	if (dim == "")
		return Error("dimension may not be empty");

	if (author == "")
		return Error("author may not be empty");

	if (region != "") {
		// If it's a rectangular region, then make sure it fits inside the image bounds
		vector<gfx::Vec2d> pts;
		auto               err = DecodeRegion(region, pts);
		if (!err.OK())
			return err;
		if (IsRegionRectangular(pts)) {
			auto           bounds = RegionBounds(pts);
			pair<int, int> imgSize;
			err = GetImageSize(image, imgSize);
			if (!err.OK())
				return err;
			if (bounds.x1 < 0 || bounds.y1 < 0 || bounds.x2 >= imgSize.first || bounds.y2 >= imgSize.second)
				return Error::Fmt("Rectangular region (%f,%f - %f,%f) extends beyond the image bounds  (%f,%f - %f,%f)",
				                  bounds.x1, bounds.y1, bounds.x2, bounds.y2, 0, 0, imgSize.first, imgSize.second);
		}
	}

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
	bool    deleteThisLabel = category == ""; // delete this label (but not necessarily the region)

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
			err = tx->Exec("INSERT OR REPLACE INTO label (sample_id, dimension, category, intensity, author, modified_at) VALUES (?, ?, ?, ?, ?, ?)",
			               {sampleID, dim, category, intensity, author, time::Now()});
		if (!err.OK())
			return err;
	}

	w.SetHeader("Content-Type", "text/plain");
	w.Body = tsf::fmt("%v", regionID);

	return Error();
}

Error Server::ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	string queryImage     = r->QueryVal("image");
	string queryDimension = r->QueryVal("dimension");

	string          sampleQuery = "SELECT id, image_path, region_id, region FROM sample";
	dba::AttribList sampleQueryParams;
	if (queryImage != "") {
		sampleQuery += " WHERE image_path = ?";
		sampleQueryParams.AddV(queryImage);
	}

	string                       idList = "(";
	auto                         rows   = tx->Query(sampleQuery.c_str(), sampleQueryParams.Size(), sampleQueryParams.ValuesPtr());
	int64_t                      nID    = 0;
	ohash::map<int64_t, int64_t> sampleIDToRegionID;
	ohash::map<int64_t, string>  sampleIDToRegion;
	ohash::map<int64_t, string>  sampleIDToImagePath;
	for (auto row : rows) {
		int64_t sampleID = 0;
		string  imagePath;
		int64_t regionID = 0;
		string  region;
		auto    err = row.Scan(sampleID, imagePath, regionID, region);
		if (!err.OK())
			return err;
		sampleIDToRegionID.insert(sampleID, regionID);
		sampleIDToRegion.insert(sampleID, region);
		sampleIDToImagePath.insert(sampleID, imagePath);
		idList += tsf::fmt("%v,", sampleID);
		nID++;
	}
	if (!rows.OK())
		return rows.Err();
	if (nID != 0)
		idList.erase(idList.end() - 1);
	idList += ")";

	if (nID == 0) {
		SendJson(w, nlohmann::json::object());
		return Error();
	}

	string          labelQuery = tsf::fmt("SELECT sample_id, dimension, category, intensity FROM label WHERE sample_id IN %v", idList);
	dba::AttribList labelQueryParams;
	if (queryDimension != "") {
		labelQuery += " AND dimension = ?";
		labelQueryParams.AddV(queryDimension);
	}
	nlohmann::json resp;
	rows           = tx->Query(labelQuery.c_str(), labelQueryParams.Size(), labelQueryParams.ValuesPtr());
	resp["images"] = nlohmann::json::object();
	auto& jImages  = resp["images"];
	for (auto row : rows) {
		int64_t sampleID = 0;
		string  dim;
		string  category;
		double  intensity = 0;
		auto    err       = row.Scan(sampleID, dim, category, intensity);
		if (!err.OK())
			return err;
		auto imagePath  = sampleIDToImagePath.get(sampleID);
		auto regionID64 = sampleIDToRegionID.get(sampleID);
		auto region     = sampleIDToRegion.get(sampleID);
		auto regionID   = tsf::fmt("%v", regionID64);
		if (jImages.find(imagePath) == jImages.end()) {
			jImages[imagePath] = nlohmann::json::object();
		}
		auto& jRegions = jImages[imagePath]["regions"];
		if (jRegions.find(regionID) == jRegions.end() && region != "") {
			jRegions[regionID]["region"] = region;
		}
		auto&          jRegion = jRegions[regionID];
		nlohmann::json jVal;
		jVal["category"]     = category;
		jVal["intensity"]    = intensity;
		jRegion["dims"][dim] = move(jVal);
	}
	if (!rows.OK())
		return rows.Err();
	SendJson(w, resp);
	return Error();
}

// Return a list of all images that contain at least one label matching the given criteria
Error Server::ApiGetFolderSummary(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx) {
	string prefix    = r->QueryVal("prefix");
	string dimension = r->QueryVal("dimension");
	//if (r->QueryVal("prefix") == "") {
	//	w.SetStatusAndBody(400, "You must set the 'prefix' query parameter");
	//	return Error();
	//}
	dba::AttribList whereParams;
	string          where = "";
	if (prefix != "") {
		where = "image_path LIKE ?";
		whereParams.AddV(prefix + "%");
	}
	if (dimension != "") {
		if (where != "")
			where += " AND ";
		where += "dimension = ?";
		whereParams.AddV(dimension);
	}

	vector<string> images;
	dba::Rows      rows;
	if (where == "")
		rows = tx->Query("SELECT image_path FROM sample");
	else
		rows = tx->Query(("SELECT image_path FROM sample INNER JOIN label ON sample.id = label.sample_id WHERE " + where).c_str(), whereParams.Size(), whereParams.ValuesPtr());

	for (auto row : rows) {
		string image_path;
		auto   err = row.Scan(image_path);
		if (!err.OK())
			return err;
		images.push_back(image_path);
	}
	if (!rows.OK())
		return rows.Err();

	SendJson(w, images);
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

	if (clean == "/" || clean == "" || clean.find("/label") == 0 || clean.find("/report") == 0)
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

void Server::Report(phttp::Response& w, phttp::RequestPtr r) {
	auto           rows = DB.DB->Query("SELECT count(*),min(dimension),min(category) FROM label GROUP BY dimension,category");
	nlohmann::json jDoc;
	for (auto row : rows) {
		int64_t count = 0;
		string  dim;
		string  category;
		row.Scan(count, dim, category);
		jDoc["dimensions"][dim][category]["count"] = count;
	}
	rows = DB.DB->Query("SELECT count(*),min(author) FROM label GROUP BY author ORDER BY count(*) DESC");
	for (auto row : rows) {
		int64_t count = 0;
		string  author;
		row.Scan(count, author);
		jDoc["authors"][author]["count"] = count;
	}
	SendJson(w, jDoc);
}

void Server::Solve(phttp::Response& w, phttp::RequestPtr r) {
	auto               ptsStr = strings::Split(r->QueryVal("circle_points"), ',');
	vector<gfx::Vec2f> circlePts;
	vector<float>      circlePtAngles;
	for (size_t i = 0; i < ptsStr.size(); i++) {
		auto ps   = ptsStr[i];
		auto pair = strings::Split(ps, ' ');
		if (pair.size() != 2) {
			w.SetStatusAndBody(400, tsf::fmt("Coordinates must have 2 dimensions (%v)", ps));
			return;
		}
		circlePts.push_back(gfx::Vec2f((float) atof(pair[0].c_str()), (float) atof(pair[1].c_str())));
		double th = (double) i * IMQS_PI * 2 / (double) ptsStr.size();
		circlePtAngles.push_back((float) th);
	}

	gfx::Mat4d world2View;
	auto       err = SolveCircle(circlePts, circlePtAngles, world2View);
	if (!err.OK()) {
		w.SetStatusAndBody(400, err.Message());
		return;
	}

	// 15 points seems like a good balance between a reasonably defined circle, and a circle
	// that can be manipulated by a user after it has been generated.
	nlohmann::json jDoc;
	size_t         nPts = 15;
	for (size_t i = 0; i < nPts; i++) {
		double th = (double) i * (IMQS_PI * 2) / (double) nPts;
		//double     th = bestModelTheta[i].item().toDouble();
		gfx::Vec4d sample(cos(th), sin(th), (double) AssumedCircleZ, 1);
		auto       ph   = world2View * sample;
		auto       p    = ph * (1.0 / ph.w);
		double     dist = gfx::Vec2d(p.x, p.y).distance(gfx::Vec2d(circlePts[i].x, circlePts[i].y));
		//tsf::print("%6.3f: [%6.3f] %6.3f %6.3f -> (%6.3f %6.3f) (%6.3f %6.3f %6.3f %6.3f)\n", th, dist, sample.x, sample.y, p.x, p.y, ph.x, ph.y, ph.z, ph.w);
		jDoc.push_back({p.x, p.y});
	}

	SendJson(w, jDoc);
}

Error Server::GetImageSize(const std::string& image, std::pair<int, int>& size) {
	{
		lock_guard<mutex> lock(PhotoSizeLock);
		if (PhotoSize.contains(image)) {
			size = PhotoSize.get(image);
			return Error();
		}
	}

	gfx::ImageIO io;
	string       fullpath = path::SafeJoin(PhotoRoot, image);
	auto         err      = io.LoadJpegFileHeader(fullpath, &size.first, &size.second);
	if (!err.OK())
		return Error::Fmt("Failed to get size of %v: %v", fullpath, err.Message());

	lock_guard<mutex> lock(PhotoSizeLock);
	PhotoSize.insert(image, size);

	return Error();
}

Error Server::DecodeRegion(const std::string& region, std::vector<gfx::Vec2d>& pts) {
	if (region.size() < 9)
		return Error::Fmt("Invalid region: '%v' (too short)", region);

	if (region.substr(0, 3) != "[[[" || region.substr(region.size() - 3, 3) != "]]]")
		return Error::Fmt("Invalid region: '%v' (expected [[[...]]])", region);

	auto numbers = strings::Split(region.substr(3, region.size() - 6), ',');
	if (numbers.size() % 2 != 0 || numbers.size() < 6)
		return Error::Fmt("Invalid region: '%v' (there must be at least 3 pairs of numbers)");

	for (size_t i = 0; i < numbers.size(); i += 2) {
		gfx::Vec2d v;
		v.x = atof(numbers[i].c_str());
		v.y = atof(numbers[i + 1].c_str());
		pts.push_back(v);
	}

	return Error();
}

bool Server::IsRegionRectangular(const std::vector<gfx::Vec2d>& region) {
	if (region.size() != 4)
		return false;
	// For every pair of adjacent vertices, there must be a difference in exactly one
	// of the two dimensions x and y. We can use XOR to check this.
	size_t j = region.size() - 1;
	for (size_t i = 0; i < region.size(); i++) {
		int xdiff = region[i].x != region[j].x ? 1 : 0;
		int ydiff = region[i].y != region[j].y ? 1 : 0;
		if ((xdiff ^ ydiff) != 1)
			return false;
		j = i;
	}
	return true;
}

gfx::RectD Server::RegionBounds(const std::vector<gfx::Vec2d>& region) {
	gfx::RectD r = gfx::RectD::Inverted();
	for (const auto& v : region)
		r.ExpandToFit(v.x, v.y);
	return r;
}

// I had to run this once, after deciding that we would not longer allow rectangular
// regions to extend beyond the dimensions of the image. By adding this constraint,
// we speed up training, because the system generating the training patches doesn't
// need to know the image size. If we allow rectangular patches to extend beyond the
// image bounds, then we'd be trying to generate patches outside of the actual image.
Error Server::FindRectanglesOutsideImageBounds() {
	Log->Info("Identifying rectangular regions outside of image bounds");
	//dba::Tx* tx  = nullptr;
	//auto     err = DB.DB->Begin(tx);
	//if (!err.OK())
	//	return Error::Fmt("Error starting transaction: %v", err.Message());
	auto rows = DB.DB->Query("SELECT image_path, region FROM sample WHERE region NOT NULL AND region <> ''");
	for (auto row : rows) {
		string image_path;
		string region;
		auto err = row.Scan(image_path, region);
		if (!err.OK())
			return err;
		if (region == "")
			continue;
		vector<gfx::Vec2d> pts;
		err = DecodeRegion(region, pts);
		if (!err.OK())
			return err;
		if (IsRegionRectangular(pts)) {
			pair<int, int> imgSize;
			err = GetImageSize(image_path, imgSize);
			if (!err.OK())
				return err;
			auto bounds = RegionBounds(pts);
			if (bounds.x1 < 0 || bounds.y1 < 0 || bounds.x2 >= imgSize.first || bounds.y2 >= imgSize.second) {
				tsf::print("Rectangle out of image bounds: %v\n", image_path);
			}
		}
	}
	if (!rows.OK())
		return rows.Err();
	//tx->Rollback();
	return Error();
}

} // namespace label
} // namespace imqs