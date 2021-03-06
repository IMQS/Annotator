#pragma once

#include "LabelDB.h"

namespace imqs {
namespace label {

class PHttpLogBridge : public phttp::Logger {
public:
	uberlog::Logger* ULog = nullptr;

	PHttpLogBridge(uberlog::Logger* ulog) : ULog(ulog) {}

	void Log(const char* msg) override {
		ULog->Info("phttp: %v", msg);
	}
};

// A trainable dimension
//class Dimension {
//public:
//	std::vector<std::string> Values; // Allowed values (eg [1,2,3,4,5] or [gravel,tar,jeep_track])
//};

class Server {
public:
	LabelDB                  DB;
	std::string              PhotoRoot;
	std::string              StaticRoot; // root of static files (ie the /dist directory that is output by "npm run build" from the /app/photo-label project)
	std::vector<std::string> AllPhotos;
	std::vector<std::string> AllDatasets;
	nlohmann::json           DimensionsRaw;
	//ohash::map<std::string, Dimension> Dimensions; // Key is the name of dimension (eg road_type)
	uberlog::Logger* Log = nullptr;

	Error Initialize(uberlog::Logger* log, std::string photoDir, std::string dimensionsFile);
	void  ListenAndRun(int port = 8080);

	static void SendJson(phttp::Response& w, const nlohmann::json& j);
	static void SendFile(phttp::Response& w, std::string filename);

private:
	std::mutex                                   PhotoSizeLock; // Guards access to PhotoSize
	ohash::map<std::string, std::pair<int, int>> PhotoSize;     // Cache of photo sizes

	Error      LoadDimensionsFile(std::string dimensionsFile);
	Error      ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
	Error      ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
	Error      ApiGetFolderSummary(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
	void       ServeStatic(phttp::Response& w, phttp::RequestPtr r);
	void       Report(phttp::Response& w, phttp::RequestPtr r);
	void       Solve(phttp::Response& w, phttp::RequestPtr r);
	Error      GetImageSize(const std::string& image, std::pair<int, int>& size);
	Error      DecodeRegion(const std::string& region, std::vector<gfx::Vec2d>& pts);
	bool       IsRegionRectangular(const std::vector<gfx::Vec2d>& region);
	gfx::RectD RegionBounds(const std::vector<gfx::Vec2d>& region);
	Error      FindRectanglesOutsideImageBounds();
};

} // namespace label
} // namespace imqs