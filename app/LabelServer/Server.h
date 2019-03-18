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
	std::vector<std::string> AllPhotos;
	nlohmann::json           DimensionsRaw;
	//ohash::map<std::string, Dimension> Dimensions; // Key is the name of dimension (eg road_type)
	uberlog::Logger* Log = nullptr;

	Error Initialize(uberlog::Logger* log, std::string photoDir, std::string dimensionsFile);
	void  ListenAndRun(int port = 8080);

	static void SendJson(phttp::Response& w, const nlohmann::json& j);
	static void SendFile(phttp::Response& w, std::string filename);

private:
	Error LoadDimensionsFile(std::string dimensionsFile);
	Error ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
	Error ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
};

} // namespace label
} // namespace imqs