#pragma once

namespace imqs {
namespace label {

class LabelDB {
public:
	static const char* Migrations[];
	dba::Conn*         DB = nullptr;

	~LabelDB();
	void  Close();
	Error Open(uberlog::Logger* log, std::string rootDir);

	static Error MergeOnceOff();
};

} // namespace label
} // namespace imqs