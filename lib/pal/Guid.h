#pragma once

#ifdef _WIN32
#include <guiddef.h>
#endif

namespace imqs {

class IMQS_PAL_API Guid {
public:
	union {
#ifdef _WIN32
		GUID MS;
#else
		struct
		{
			unsigned int   Data1;
			unsigned short Data2;
			unsigned short Data3;
			unsigned char  Data4[8];
		} MS;
#endif
		unsigned char Bytes[16];
		uint64_t      Qwords[2];
		uint32_t      Dwords[4];
	};

	static Guid Create();                    // Use entropy
	static Guid CreateInsecure();            // Use MAC and time
	static Guid FromString(const char* buf); // Parses the 36 character format FE154985-8F0D-4E32-8E2A-853521ACDBEC
	static Guid FromBytes(const void* buf);  // Copies 16 bytes directly, without any endian translation
	static Guid Null();                      // Return a zeroed-out GUID

	// Writes 36 characters in the form FE154985-8F0D-4E32-8E2A-853521ACDBEC.
	// Includes null terminator, for a total of 37 bytes written.
	void ToString(char* buf) const;

	// Writes 36 characters in the form FE154985-8F0D-4E32-8E2A-853521ACDBEC.
	std::string ToString() const;

	bool ParseString(const char* buf); // Parses the 36 character format FE154985-8F0D-4E32-8E2A-853521ACDBEC

	bool operator==(const Guid& b) const;
	bool operator!=(const Guid& b) const;
	bool operator<(const Guid& b) const;

	uint32_t Hash32() const;
	bool     IsNull() const { return Qwords[0] == 0 && Qwords[1] == 0; }

private:
	static Guid InternalCreate(bool secure);
};
} // namespace imqs

namespace ohash {
template <>
inline hashkey_t gethashcode(const imqs::Guid& g) {
	return (hashkey_t) g.Hash32();
}
} // namespace ohash
