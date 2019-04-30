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
	auto           rows = DB.DB->Query("SELECT count(*),min(dimension),min(value) FROM label GROUP BY dimension,value");
	nlohmann::json jDoc;
	for (auto row : rows) {
		int64_t count = 0;
		string  dim;
		string  val;
		row.Scan(count, dim, val);
		jDoc["dimensions"][dim][val]["count"] = count;
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Circle Solver
// Summary:
// The only thing that seems to train reliably with backprop is the 4x4 matrix, without perspective divide.
// When we add in the perspective divide, the loss gets worse.
// I tried training camera positions instead of the raw 4x4 matrix, but that doesn't work (although
// I didn't try training it without the perspective divide).
// I also tried training the theta positions, after training the 4x4 matrix, but that only seems to work
// in the most trivial test case. In the real world cases, it just explodes the loss.
// I'd love to know why these things happen. It may simply be that the learning rate is too large,
// but I think there's more to it than that.
// It may be that the system is underdetermined.
// It may be local optimimums.
// It may be imbalanced variables, i.e. their magnitudes are so high that they destroy the gradient signal
// for the other variables.

const bool  Use3x3         = false;
const float AssumedCircleZ = 0.0f;
const auto  TorchDevice    = torch::kCPU;

at::Tensor MatTranslate(at::Tensor m, at::Tensor trans) {
	auto tm  = torch::eye(4).to(TorchDevice);
	tm[0][3] = trans[0];
	tm[1][3] = trans[1];
	tm[2][3] = trans[2];
	return at::matmul(m, tm);
}

at::Tensor MatLookAt(at::Tensor eyeV, at::Tensor centerV, at::Tensor upV) {
	// Make rotation matrix

	// Z vector
	auto z   = eyeV - centerV;
	auto mag = at::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
	if (mag.item().toFloat() != 0) {
		z = z.clone() / mag;
	}

	// X vector = Up cross Z
	auto x = torch::zeros({3}).to(TorchDevice);
	x[0]   = upV[1] * z[2] - upV[2] * z[1];
	x[1]   = -upV[0] * z[2] + upV[2] * z[0];
	x[2]   = upV[0] * z[1] - upV[1] * z[0];

	// Y = Z cross X
	auto y = torch::zeros({3}).to(TorchDevice);
	y[0]   = z[1] * x[2] - z[2] * x[1];
	y[1]   = -z[0] * x[2] + z[2] * x[0];
	y[2]   = z[0] * x[1] - z[1] * x[0];

	//cout << "x:\n" << x << endl;
	//cout << "y:\n" << y << endl;

	// mpichler, 19950515
	// cross product gives area of parallelogram, which is < 1.0 for
	//  non-perpendicular unit-length vectors; so normalize x, y here

	mag = at::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
	if (mag.item().toFloat() != 0) {
		x = x.clone() / mag;
	}

	mag = at::sqrt(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	if (mag.item().toFloat() != 0) {
		y = y.clone() / mag;
	}

	auto m = torch::zeros({4, 4}).to(TorchDevice);
	//auto ma = m.accessor<float, 2>();
	//cout << "m:\n" << m << endl;
	//cout << "slice part level 1:\n" << m.slice(0, 0, 1) << endl;
	//cout << "slice part level 2:\n" << m.slice(0, 0, 1).slice(1, 0, 3) << endl;
	m.slice(0, 0, 1).slice(1, 0, 3) = x;
	m.slice(0, 1, 2).slice(1, 0, 3) = y;
	m.slice(0, 2, 3).slice(1, 0, 3) = z;

	m[3][3] = 1.0f;

	//cout << "m:\n" << m << endl;

	// Translate Eye to Origin
	//imat.Translate(-eyex, -eyey, -eyez);
	m = MatTranslate(m, -eyeV);

	//cout << "m:\n" << m << endl;

	return m;
}

at::Tensor MatFrustum(at::Tensor left, at::Tensor right, at::Tensor bottom, at::Tensor top, at::Tensor zNear, at::Tensor zFar) {
	auto A = (right + left) / (right - left);
	auto B = (top + bottom) / (top - bottom);
	auto C = -1 * (zFar + zNear) / (zFar - zNear);
	auto D = -1 * (2 * zFar * zNear) / (zFar - zNear);
	auto E = (2 * zNear) / (right - left);
	auto F = (2 * zNear) / (top - bottom);
	auto m = torch::zeros({4, 4}).to(TorchDevice);
	//cout << "A:\n" << A << endl;
	//cout << "m[0]:\n" << m[0] << endl;
	//cout << "m[0][0]:\n" << m[0][0] << endl;
	m.slice(0, 0, 1).slice(1, 2, 3) = A;
	m.slice(0, 1, 2).slice(1, 2, 3) = B;
	m.slice(0, 2, 3).slice(1, 2, 3) = C;
	m.slice(0, 3, 4).slice(1, 2, 3) = -1;
	m.slice(0, 2, 3).slice(1, 3, 4) = D;
	m.slice(0, 0, 1).slice(1, 0, 1) = E;
	m.slice(0, 1, 2).slice(1, 1, 2) = F;
	//m[0][2] = A;
	//m[1][2] = B;
	//m[2][2] = C;
	//m[3][2] = -1;
	//m[2][3] = D;
	//m[0][0] = E;
	//m[1][1] = F;
	return m;
}

at::Tensor MatPerspective(at::Tensor fovDegrees, at::Tensor aspect, at::Tensor zNear, at::Tensor zFar) {
	auto top    = zNear * at::tan(fovDegrees * IMQS_PI / 360.0);
	auto bottom = -top;
	auto left   = bottom * aspect;
	auto right  = top * aspect;
	return MatFrustum(left, right, bottom, top, zNear, zFar);
}

struct World2ViewNet : torch::nn::Module {
	torch::Tensor EyeV;
	torch::Tensor LookAtV;
	torch::Tensor UpV;
	torch::Tensor CameraFOV;
	torch::Tensor CameraAspect;
	torch::Tensor ZNear;
	torch::Tensor ZFar;

	torch::Tensor M3; // 3x3 matrix

	torch::Tensor M4;    // 4x4 matrix
	torch::Tensor Theta; // Every row[i] records the angle of sample point[i]

	bool Camera      = false;
	bool Perspective = false;

	World2ViewNet(size_t nPts) {
		//EyeV = register_parameter("EyeV", torch::tensor({0.f, 0.f, AssumedCircleZ - 30.0f}));
		//LookAtV = register_parameter("LookAtV", torch::tensor({0.f, 0.f, AssumedCircleZ}));
		EyeV         = register_parameter("EyeV", torch::tensor({-300.f, -200.f, AssumedCircleZ - 30.0f}));
		LookAtV      = register_parameter("LookAtV", torch::tensor({-300.f, -200.f, AssumedCircleZ}));
		UpV          = register_parameter("UpV", torch::tensor({0.f, 1.f, 0.f}));
		CameraFOV    = register_parameter("CameraFOV", torch::tensor({90.0f}));
		CameraAspect = register_parameter("CameraAspect", torch::tensor({1.0f}));
		ZNear        = register_parameter("ZNear", torch::tensor({1.0f}));
		ZFar         = register_parameter("ZFar", torch::tensor({1000.0f}));

		M3 = register_parameter("M3", torch::eye(3));
		M4 = register_parameter("M4", torch::eye(4));

		vector<float> angles;
		for (size_t i = 0; i < nPts; i++) {
			angles.push_back((float) i * (float) IMQS_PI * 2.0f / (float) nPts);
		}
		Theta = register_parameter("T", torch::tensor(angles));
		//Theta = torch::tensor(angles);
		//cout << Theta << endl;
	}
	//torch::Tensor forward(int iSample, torch::Tensor input) {
	torch::Tensor forward(int iSample) {
		if (Use3x3) {
			auto input = torch::empty({3}).to(TorchDevice);
			input[0]   = at::cos(Theta[iSample]);
			input[1]   = at::sin(Theta[iSample]);
			input[2]   = 1.0f;
			auto p     = at::matmul(M3, input);
			//p          = p.clone() / p[2];
			//cout << "M3\n"
			//     << M3 << endl;
			//cout << "p:\n"
			//     << p << endl;
			return p;
		}

		// build up the 4x4 transformation matrix
		if (Camera) {
			auto modelView  = MatLookAt(EyeV, LookAtV, UpV);
			auto projection = MatPerspective(CameraFOV, CameraAspect, ZNear, ZFar);
			modelView       = modelView.to(TorchDevice);
			projection      = projection.to(TorchDevice);
			auto mvProj     = at::matmul(modelView, projection);
			//auto mvProj = at::matmul(projection, modelView);
			//auto mvProj = modelView;
			M4 = mvProj;
		}
		//cout << "modelView\n" << modelView << endl;
		//cout << "projection\n" << projection << endl;
		//cout << "mvProj\n" << mvProj << endl;
		//exit(0);

		//cout << M << endl;
		//cout << input << endl;
		//auto p = M * input;
		//cout << p << endl;
		//auto input = torch::tensor({at::cos(Theta[iSample]), at::sin(Theta[iSample]), 0.0f, 1.0f});
		//auto input = torch::empty({4,1}).to(TorchDevice);
		auto input = torch::empty({4}).to(TorchDevice);
		input[0]   = at::cos(Theta[iSample]);
		input[1]   = at::sin(Theta[iSample]);
		input[2]   = AssumedCircleZ;
		input[3]   = 1.0f;
		//cout << input << endl;
		//auto p = at::matmul(M, input);
		auto p = at::matmul(M4, input);
		//cout << "input:\n" << input << endl;
		//cout << "ph:\n" << p << endl;
		//p /= p[3];
		if (Perspective) {
			//cout << "ph:\n"
			//     << p << endl;
			p = p.clone() / p[3];
			//cout << "p:\n"
			//     << p << endl;
			//exit(1);
		}
		//cout << "p:\n" << p << endl;
		return p;
	}
};

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

	auto model = World2ViewNet(circlePts.size());
	model.to(TorchDevice);
	auto bestModelM4    = torch::zeros({1});
	auto bestModelM3    = torch::zeros({1});
	auto bestModelTheta = torch::zeros({1});
	//float             lr             = 8.0f;
	float             lr        = 0.2f;
	float             initialLR = lr;
	float             bestLoss  = 1e9f;
	torch::optim::SGD optim(model.parameters(), torch::optim::SGDOptions(lr));
	//torch::optim::Adam optim(model.parameters(), torch::optim::AdamOptions(lr));
	for (int cycle = 0; cycle < 2; cycle++) {
		if (cycle == 1) {
			model.Perspective = true;
			optim.options.learning_rate(initialLR * 1e-6f);
			//optim.options.learning_rate(initialLR * 0.01f);
		}
		for (int epoch = 0; epoch < 40; epoch++) {
			model.zero_grad();
			//model.EyeV.set_requires_grad(false);
			//model.LookAtV.set_requires_grad(false);
			model.UpV.set_requires_grad(false);
			model.CameraAspect.set_requires_grad(false);
			model.CameraFOV.set_requires_grad(false);
			model.ZNear.set_requires_grad(false);
			model.ZFar.set_requires_grad(false);
			//model.M4.set_requires_grad(cycle == 0);
			model.Theta.set_requires_grad(false);
			//model.Theta.set_requires_grad(cycle == 1);
			//auto loss = torch::Scalar(0.0f);
			auto loss = torch::zeros({1}).to(TorchDevice);
			for (size_t i = 0; i < circlePts.size(); i++) {
				auto viewPt = circlePts[i];
				//float th = circlePtAngles[i];
				//float x = cos(th);
				//float y = sin(th);
				//auto inp = torch::tensor({pt.x, pt.y, 0.f, 1.f});
				//auto inp = torch::tensor({x, y, 0.f, 1.f});
				//inp                           = inp.to(TorchDevice);
				auto out    = model.forward((int) i);
				auto dx     = out[0] - viewPt.x;
				auto dy     = out[1] - viewPt.y;
				auto distSQ = dx * dx + dy * dy;
				loss += distSQ;
				//if (model.Perspective)
				//	tsf::print("%3d: %6.3f %6.3f -> %6.3f %6.3f (%6.3f)\n", i, viewPt.x, viewPt.y, out[0].item().toDouble(), out[1].item().toDouble(), distSQ.item().toDouble());
				//tsf::print("%3d %6.3f %6.3f\n", i, dx.item().toDouble(), dx.item().toDouble());
				// force two left-most parameters on the bottom row to zero
				//loss += at::abs(model.M[3][0]);
				//loss += at::abs(model.M[3][1]);
				//auto len                      = at::sqrt(out[0] * out[0] + out[1] * out[1]);
				//auto distanceFromUnitCircleSQ = at::pow(len - 1, 2);
				//tsf::print("%3d %3d\n");
				//cout << "  distance: " << distanceFromUnitCircleSQ << endl;
				//loss += distanceFromUnitCircleSQ;
				//loss = out[0];
				//cout << "  loss: " << loss << endl;
				//break;
			}
			loss = loss.clone() / (float) circlePts.size();
			//cout << "loss:\n"
			//     << loss << endl;
			//cout << "matrix:\n"
			//     << model.M << endl;
			float lossVal = loss[0].item().toFloat();
			if (lossVal < bestLoss) {
				bestLoss = lossVal;
				if (Use3x3)
					bestModelM3 = model.M3.to(torch::kCPU);
				else
					bestModelM4 = model.M4.to(torch::kCPU);
				bestModelTheta = model.Theta.to(torch::kCPU);
			}
			//if (epoch % 5 == 0)
			//	tsf::print("%3d, loss: %.6f\n", epoch, lossVal);
			loss.backward();
			optim.step();
			//exit(1);
			//if (iter != 0 && iter % 10 == 0)
			//	lr *= 0.1f;
			if (lossVal > bestLoss && lr != initialLR) {
				//lr *= 0.1f;
				//optim.options.learning_rate(lr);
			}
		}
	}

	if (Use3x3) {
		cout << "M3:\n"
		     << bestModelM3 << endl;
	}

	// We need libmkl_avx2.so for inverse()
	//auto inv = at::inverse(model.M);
	//auto inv = model.M.inverse();
	//gfx::Mat4d view2World;
	gfx::Mat4d world2View;
	auto       ac = bestModelM4.accessor<float, 2>();
	for (int r = 0; r < 4; r++) {
		for (int c = 0; c < 4; c++) {
			//view2World.row[r][c] = ac[r][c];
			world2View.row[r][c] = ac[r][c];
		}
	}
	//auto world2View = view2World.Inverted();
	auto view2World = world2View.Inverted();

	//cout << "eye:\n"
	//     << model.EyeV << endl;
	//cout << "lookat:\n"
	//     << model.LookAtV << endl;
	//cout << "fov:\n"
	//     << model.CameraFOV << endl;

	//tsf::print("World2View:\n");
	//for (int i = 0; i < 4; i++)
	//	tsf::print("  %7.4f %7.4f %7.4f %7.4f\n", world2View.row[i][0], world2View.row[i][1], world2View.row[i][2], world2View.row[i][3]);

	//tsf::print("View2World:\n");
	//for (int i = 0; i < 4; i++)
	//	tsf::print("  %7.4f %7.4f %7.4f %7.4f\n", view2World.row[i][0], view2World.row[i][1], view2World.row[i][2], view2World.row[i][3]);

	//tsf::print("Sample points view -> world:\n");
	//for (auto pt : circlePts) {
	//	auto s = view2World * gfx::Vec4d(pt.x, pt.y, 0, 1);
	//	s = s * (1.0 / s.w);
	//	double len = gfx::Vec2d(s.x, s.y).size();
	//	tsf::print("  %7.4f %7.4f -> %7.4f %7.4f %7.4f (%.4f)\n", pt.x, pt.y, s.x, s.y, s.z, len);
	//}

	//tsf::print("Sample points world -> view:\n");
	//for (size_t i = 0; i < circlePts.size(); i++) {
	//	auto pt = circlePts[i];
	//	auto s = world2View * gfx::Vec4d(pt.x, pt.y, 0, 1);
	//	s = s * (1.0 / s.w);
	//	double len = gfx::Vec2d(s.x, s.y).size();
	//	tsf::print("  %7.4f %7.4f %7.4f -> %7.4f %7.4f %7.4f (%.4f)\n", pt.x, pt.y, s.x, s.y, s.z, len);
	//}

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

} // namespace label
} // namespace imqs