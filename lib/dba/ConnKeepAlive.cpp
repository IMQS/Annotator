#include "pch.h"
#include "ConnKeepAlive.h"
#include "Conn.h"
#include "Global.h"

using namespace std;

namespace imqs {
namespace dba {

ConnKeepAlive::ConnKeepAlive() {
}

ConnKeepAlive::~ConnKeepAlive() {
	ReleaseAll();
}

Error ConnKeepAlive::Open(const ConnDesc& desc, Conn*& conn) {
	lock_guard<mutex> lock(Lock);
	auto              c = Conns.get(desc);
	if (c) {
		// Return cached entry, so that we don't increase the ref count on it
		conn = c;
		return Error();
	}
	auto err = Glob.Open(desc, conn);
	if (!err.OK())
		return err;
	Conns.insert(desc, conn);
	return Error();
}

void ConnKeepAlive::Release(const ConnDesc& desc) {
	lock_guard<mutex> lock(Lock);
	auto              c = Conns.get(desc);
	if (c)
		c->Close();
}

void ConnKeepAlive::ReleaseAll() {
	lock_guard<mutex> lock(Lock);
	for (const auto& p : Conns)
		p.second->Close();
	Conns.clear();
}

} // namespace dba
} // namespace imqs