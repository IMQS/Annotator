#include "pch.h"
#include "io.h"

namespace imqs {

IMQS_PAL_API StaticError ErrEOF("EOF");

namespace io {
Writer::~Writer() {}
Reader::~Reader() {}

Seeker::~Seeker() {}

Error Seeker::Seek(int64_t offset, SeekWhence whence) {
	int64_t newPosition;
	return SeekWithResult(offset, whence, newPosition);
}

Error Seeker::Length(int64_t& len) {
	int64_t pos;
	auto    err = SeekWithResult(0, io::SeekWhence::Current, pos);
	if (!err.OK())
		return err;
	err = SeekWithResult(0, io::SeekWhence::End, len);
	if (!err.OK())
		return err;
	return SeekWithResult(0, io::SeekWhence::Begin, pos);
}

Error Reader::ReadExactly(void* buf, size_t len) {
	while (len != 0) {
		size_t nread = len;
		auto   err   = Read(buf, nread);
		if (!err.OK())
			return err;
		len -= nread;
		(char*&) buf += nread;
	}
	return Error();
}

} // namespace io
} // namespace imqs
