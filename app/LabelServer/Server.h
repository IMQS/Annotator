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

class Server {
public:
	LabelDB                  DB;
	std::string              PhotoRoot;
	std::vector<std::string> AllPhotos;
	uberlog::Logger*         Log = nullptr;

	Error Initialize(uberlog::Logger* log, std::string photoDir);
	void  ListenAndRun(int port = 8080);

	static void SendJson(phttp::Response& w, const nlohmann::json& j);

private:
	Error ApiSetLabel(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
	Error ApiGetLabels(phttp::Response& w, phttp::RequestPtr r, dba::Tx* tx);
};

} // namespace label
} // namespace imqs