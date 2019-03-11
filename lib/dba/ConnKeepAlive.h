#pragma once

#include "ConnDesc.h"

namespace imqs {
namespace dba {

// Database connection keep-alive helper
//
// This was built to speed up the generation of map tiles. From an API perspective, our map service is
// stateless, and our initial implementation was built simply in this manner, which meant that
// every request for a tile would make a new connection to the Postgres DB, and then once the tile
// was finished generating, the connection's ref count would drop to zero, and the libpq/tcp connection
// to Postgres would be dropped. This is expensive, because each time you make a new connection, it
// costs about 50ms (measured on my Windows 10 machine in 2018).
//
// We debated whether to build this connection pool logic directly into dba::Global, and in the end
// we decided rather to leave it as a layer above, that you can opt-in to.
//
// The operation of ConnPool is actually very simple. The first time you request a new DB, it will get
// opened, and saved inside 'Conns'. Subsequent requests to Open will serve back that same DB. You
// never call "Close". However, if you want to free unused items in the pool, then you can call Release
// or ReleaseAll, which will call Close on those connections, and erase them from our hash table.
class IMQS_DBA_API ConnKeepAlive {
public:
	ConnKeepAlive();
	~ConnKeepAlive();

	Error Open(const ConnDesc& desc, Conn*& conn);

	// Release the keepalive connection, so that if there are no active users
	// of this connection, then all actual connection will be dropped.
	void Release(const ConnDesc& desc);

	// This is called by the destructor
	void ReleaseAll();

private:
	std::mutex                  Lock;
	ohash::map<ConnDesc, Conn*> Conns;
};

} // namespace dba
} // namespace imqs
