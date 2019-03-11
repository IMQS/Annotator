#include "pch.h"
#include "DriverUtils.h"

namespace imqs {
namespace dba {

IMQS_DBA_API size_t CountUtf8Bytes(size_t bufBytes, const char* buf, size_t numChars) {
	auto s   = buf;
	auto end = buf + bufBytes;
	for (size_t nchars = 0; nchars < numChars; nchars++) {
		int seq_len = 0;
		int ch      = utfz::decode(s, end, seq_len);
		if (ch == utfz::replace) {
			// Just terminate right here, because we don't want to try and insert invalid UTF8 into the database anyway
			break;
		}
		s += seq_len;
	}
	return s - buf;
}

} // namespace dba
} // namespace imqs