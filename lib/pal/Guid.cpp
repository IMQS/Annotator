#include "pch.h"
#include "Guid.h"
#ifdef _WIN32
#include <Rpc.h>
#else
#include <uuid/uuid.h>
#endif

namespace imqs {

static Guid ZeroGuid = {0};

Guid Guid::CreateInsecure() {
	return InternalCreate(false);
}

Guid Guid::Create() {
	return InternalCreate(true);
}

Guid Guid::InternalCreate(bool secure) {
	Guid g;
#ifdef _WIN32
	RPC_STATUS st = secure ? UuidCreate(&g.MS) : UuidCreateSequential(&g.MS);

	// Errors
	if (st != RPC_S_OK && st != RPC_S_UUID_LOCAL_ONLY) {
		IMQS_DIE_MSG("Unable to generate GUID");
		return g;
	}
#else
	if (secure)
		uuid_generate((unsigned char*) &g);
	else
		uuid_generate_time((unsigned char*) &g);
#endif
	return g;
}

Guid Guid::FromString(const char* buf) {
	Guid g;
	if (!g.ParseString(buf))
		return ZeroGuid;
	return g;
}

Guid Guid::FromBytes(const void* buf) {
	Guid g;
	memcpy(&g, buf, 16);
	return g;
}

Guid Guid::Null() {
	return ZeroGuid;
}

void Guid::ToString(char* buf) const {
	sprintf(buf, "%08X-%04hX-%04hX-%02hX%02hX-%02hX%02hX%02hX%02hX%02hX%02hX",
	        MS.Data1, MS.Data2, MS.Data3,
	        (unsigned short) MS.Data4[0], (unsigned short) MS.Data4[1], (unsigned short) MS.Data4[2], (unsigned short) MS.Data4[3],
	        (unsigned short) MS.Data4[4], (unsigned short) MS.Data4[5], (unsigned short) MS.Data4[6], (unsigned short) MS.Data4[7]);
}

std::string Guid::ToString() const {
	char buf[37];
	ToString(buf);
	return buf;
}

bool Guid::ParseString(const char* buf) {
	unsigned short v[8];
	int            cv = sscanf(buf, "%08X-%04hX-%04hX-%02hX%02hX-%02hX%02hX%02hX%02hX%02hX%02hX",
                    &MS.Data1, &MS.Data2, &MS.Data3,
                    &v[0], &v[1], &v[2], &v[3],
                    &v[4], &v[5], &v[6], &v[7]);
	if (cv != 11)
		return false;
	for (int i = 0; i < 8; i++)
		MS.Data4[i] = (unsigned char) v[i];
	return true;
}

bool Guid::operator==(const Guid& b) const {
	return memcmp(this, &b, sizeof(*this)) == 0;
}

bool Guid::operator!=(const Guid& b) const {
	return !(*this == b);
}

bool Guid::operator<(const Guid& b) const {
	return memcmp(this, &b, sizeof(*this)) < 0;
}

uint32_t Guid::Hash32() const {
	return Dwords[0] ^ Dwords[1] ^ Dwords[2] ^ Dwords[3];
}
} // namespace imqs
