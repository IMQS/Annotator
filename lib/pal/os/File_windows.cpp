#include "pch.h"
#include "File.h"
#include "../strings/utf.h"
#include "os.h"

namespace imqs {
namespace os {

File::File() {
}

File::~File() {
	Close();
}

Error File::Write(const void* buf, size_t len) {
	IMQS_ASSERT(len < UINT32_MAX);

	DWORD written = 0;
	if (!WriteFile(FH, buf, (DWORD) len, &written, nullptr))
		return ErrorFrom_GetLastError();

	if (written != len)
		return Error::Fmt("Wrote %v bytes to file, but expected to write %v", written, len);

	return Error();
}

Error File::Read(void* buf, size_t& len) {
	IMQS_ASSERT(len < UINT32_MAX);

	DWORD nread = 0;
	if (!ReadFile(FH, buf, (DWORD) len, &nread, nullptr))
		return ErrorFrom_GetLastError();

	len = nread;
	return Error();
}

Error File::Open(const std::string& filename, uint32_t openFlags) {
	DWORD desiredAccess = GENERIC_READ;
	if (!!(openFlags & OpenFlagModify))
		desiredAccess |= GENERIC_WRITE;
	return WinCreateFile(filename, desiredAccess, FILE_SHARE_READ, OPEN_EXISTING, 0);
}

Error File::Create(const std::string& filename) {
	return WinCreateFile(filename, GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS, 0);
}

Error File::SeekWithResult(int64_t offset, io::SeekWhence whence, int64_t& newPosition) {
	LARGE_INTEGER woffset;
	woffset.QuadPart = offset;
	int m            = 0;
	switch (whence) {
	case io::SeekWhence::Begin: m = FILE_BEGIN; break;
	case io::SeekWhence::Current: m = FILE_CURRENT; break;
	case io::SeekWhence::End: m = FILE_END; break;
	}
	if (!SetFilePointerEx(FH, woffset, (LARGE_INTEGER*) &newPosition, m))
		return ErrorFrom_GetLastError();
	return Error();
}

void File::Close() {
	if (FH != INVALID_HANDLE_VALUE)
		CloseHandle(FH);
	FH = INVALID_HANDLE_VALUE;
}

Error File::WinCreateFile(const std::string& filename, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition, DWORD flagsAndAttributes) {
	FH = CreateFileW(towide(filename).c_str(), desiredAccess, shareMode, nullptr, creationDisposition, flagsAndAttributes, nullptr);
	if (FH == INVALID_HANDLE_VALUE)
		return ErrorFrom_GetLastError();
	return Error();
}
} // namespace os
} // namespace imqs
