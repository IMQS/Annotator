#include "pch.h"
#include "Perspective.h"
#include "Speed.h"
#include "Stitcher.h"

using namespace imqs::gfx;
using namespace std;

namespace imqs {
namespace roadproc {

const string ApiBase = "http://roads.imqs.co.za/api";

static Error Login(http::Connection& con, string username, string password, string& sessionCookie) {
	http::Connection client;
	http::HeaderMap  headers = {{"Authorization", "BASIC "}};
	http::Request    req;
	req.SetBasicAuth(username, password);
	auto resp = client.Post(ApiBase + "/auth/login", headers);
	if (!resp.Is200())
		return resp.ToError();

	http::Cookie cookie;
	if (!resp.FirstSetCookie("session", cookie))
		return Error("No session cookie in login response");
	sessionCookie = cookie.Value;
	return Error();
}

static Error GetTracks(http::Connection& con, string& sessionCookie, string speedFile, string trackFile) {
	//nlohmann::json speed;
	//auto           err = imqs::nj::ParseFile(speedFile, speed);
	string speed;
	auto   err = os::ReadWholeFile(speedFile, speed);
	if (!err.OK())
		return Error::Fmt("Error reading speed file %v: %v", speedFile, err.Message());

	auto req = http::Request::POST(ApiBase + "/tracks/align");
	req.SetHeader("Content-Type", "application/json");
	req.Body = speed;
	req.AddCookie("session", sessionCookie);
	auto resp = con.Perform(req);
	if (!resp.Is200())
		return Error::Fmt("Error fetching GPS track: %v", resp.ToError().Message());

	return os::WriteWholeFile(trackFile, resp.Body);
}

Error DoAuto(argparse::Args& args) {
	Error err;
	auto  username       = args.Params[0];
	auto  password       = args.Params[1];
	auto  storageSpec    = args.Params[2];
	auto  videoFiles     = strings::Split(args.Params[3], ',');
	auto  flattenStr     = args.Get("flatten");
	auto  speedFile      = args.Get("speed");
	float metersPerPixel = args.GetDouble("mpp");

	string datasetName = path::ChangeExtension(path::Filename(videoFiles[0]), "");
	string sessionCookie;

	FlattenParams fp;
	if (flattenStr != "") {
		err = fp.ParseJson(flattenStr);
		if (!err.OK())
			return err;
	} else {
		err = DoPerspective(videoFiles, true, fp);
		if (!err.OK())
			return err;
	}

	if (speedFile == "") {
		speedFile = tsf::fmt("speed-%v.json", datasetName);
		err       = DoSpeed(videoFiles, fp, 0, SpeedOutputMode::JSON, speedFile);
		if (!err.OK())
			return err;
	}

	http::Connection client;
	err = Login(client, username, password, sessionCookie);
	if (!err.OK())
		return err;

	auto trackFile = tsf::fmt("track-%v.json", datasetName);
	err            = GetTracks(client, sessionCookie, speedFile, trackFile);
	if (!err.OK())
		return err;

	if (metersPerPixel == 0) {
		Stitcher s;
		err = s.DoMeasureScale(videoFiles, speedFile, fp);
		if (!err.OK())
			return err;
	}

	Stitcher s;
	s.MetersPerPixel = metersPerPixel;
	s.BaseZoomLevel  = 25;
	err              = s.DoStitch(storageSpec, videoFiles, trackFile, fp, 0, -1);
	if (!err.OK())
		return err;

	return Error();
}

int Auto(argparse::Args& args) {
	auto start = time::Now();
	auto err   = DoAuto(args);
	if (!err.OK()) {
		tsf::print("Error: %v\n", err.Message());
		return 1;
	}
	auto dur   = time::Now() - start;
	int  tsec  = dur.Seconds();
	int  hours = int(tsec / 3600);
	int  mins  = int((tsec - hours * 3600) / 60);
	int  sec   = tsec - hours * 3600 - mins * 60;
	tsf::print("Total time: %2d:%02d:%02d\n", hours, mins, sec);
	return 0;
}

} // namespace roadproc
} // namespace imqs