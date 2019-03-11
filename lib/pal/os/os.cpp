#include "pch.h"
#include "os.h"
#include "File.h"
#include "io/io.h"
#include "../path.h"
#include "../strings/strings.h"
#include "../strings/utf.h"

#include <fcntl.h>

#ifdef _WIN32
static const char SYSTEM_PATH_SPLITTER = ';';
#include <io.h>
#elif MACOS
static const char SYSTEM_PATH_SPLITTER = ':';
#define O_BINARY (0)
#include <sys/param.h>
#include <sys/sysctl.h>
#else
static const char SYSTEM_PATH_SPLITTER = ':';
#define O_BINARY (0)
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
// struct timespec st_mtim;  /* time of last modification */
#define STAT_TIME(st, x) (st.st_##x##tim.tv_sec) + ((st.st_##x##tim.tv_nsec) * (1.0 / 1000000000))
#endif

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4996) // deprecated POSIX API names (write vs _write, etc)
#endif

namespace imqs {
namespace os {

IMQS_PAL_API StaticError ErrEACCESS("Tried to open a read-only file for writing, file's sharing mode does not allow the specified operations, or the given path is a directory");
IMQS_PAL_API StaticError ErrEEXIST("File already exists");
IMQS_PAL_API StaticError ErrEINVAL("Invalid oflag or pmode argument");
IMQS_PAL_API StaticError ErrEMFILE("No more file descriptors are available (too many files are open)");
IMQS_PAL_API StaticError ErrENOENT("File or path not found");

std::string FindFileItem::FullPath() const {
	return std::string(Root) + PathSeparator + Name;
}

#ifdef _WIN32
static int64_t FileTimeTo100NanoSeconds(const FILETIME& ft) {
	uint64_t time  = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	int64_t  stime = (int64_t) time; // 100-nanoseconds
	return stime;
}
#endif

IMQS_PAL_API void Sleep(imqs::time::Duration d) {
#ifdef _WIN32
	::Sleep((DWORD) d.Milliseconds());
#else
	int64_t  nano = (int64_t) d.Nanoseconds();
	timespec t;
	t.tv_nsec = nano % 1000000000;
	t.tv_sec  = (nano - t.tv_nsec) / 1000000000;
	nanosleep(&t, nullptr);
#endif
}

IMQS_PAL_API Error ErrorFrom_errno() {
	return ErrorFrom_errno(errno);
}

IMQS_PAL_API Error ErrorFrom_errno(int errno_) {
	switch (errno_) {
	case EACCES: return ErrEACCESS;
	case EEXIST: return ErrEEXIST;
	case EINVAL: return ErrEINVAL;
	case EMFILE: return ErrEMFILE;
	case ENOENT: return ErrENOENT;
	}
	return Error(tsf::fmt("OS errno = %v", errno_));
}

#ifdef _WIN32
IMQS_PAL_API Error ErrorFrom_GetLastError() {
	return ErrorFrom_GetLastError(GetLastError());
}

#pragma warning(push)
#pragma warning(disable : 6031) // snprintf return value ignored
IMQS_PAL_API Error ErrorFrom_GetLastError(DWORD err) {
	switch (err) {
	case ERROR_ACCESS_DENIED: return ErrEACCESS;
	case ERROR_ALREADY_EXISTS:
	case ERROR_FILE_EXISTS: return ErrEEXIST;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_NO_MORE_FILES: return ErrENOENT;
	default: break;
	}

	char   szBuf[1024];
	LPVOID lpMsgBuf;

	FormatMessageA(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER |
	        FORMAT_MESSAGE_FROM_SYSTEM,
	    nullptr,
	    err,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    (LPSTR) &lpMsgBuf,
	    0, nullptr);

	snprintf(szBuf, sizeof(szBuf), "(%u) %s", err, (const char*) lpMsgBuf);
	szBuf[sizeof(szBuf) - 1] = 0;
	LocalFree(lpMsgBuf);

	// chop off trailing carriage returns
	std::string r = szBuf;
	while (r.length() > 0 && (r[r.length() - 1] == 10 || r[r.length() - 1] == 13))
		r.resize(r.length() - 1);

	return Error(r);
}
#pragma warning(pop)
#endif

IMQS_PAL_API Error Stat(const std::string& path, FileAttributes& attribs) {
#ifdef _WIN32
	// FILE_FLAG_BACKUP_SEMANTICS is necessary for opening a directory
	HANDLE h = CreateFileW(towide(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		return ErrorFrom_GetLastError(GetLastError());
	}
	BY_HANDLE_FILE_INFORMATION inf;
	if (!GetFileInformationByHandle(h, &inf)) {
		CloseHandle(h);
		return ErrorFrom_GetLastError(GetLastError());
	}
	attribs.IsDir      = !!(inf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	attribs.TimeCreate = time::Time::FromEpoch1601(FileTimeTo100NanoSeconds(inf.ftCreationTime));
	attribs.TimeModify = time::Time::FromEpoch1601(FileTimeTo100NanoSeconds(inf.ftLastWriteTime));
	attribs.Size       = (uint64_t) inf.nFileSizeHigh << 32 | (uint64_t) inf.nFileSizeLow;
	CloseHandle(h);
	return Error();
#else
	struct stat s;
	if (stat(path.c_str(), &s) != 0)
		return ErrorFrom_errno(errno);
	attribs.IsDir      = S_ISDIR(s.st_mode);
	attribs.TimeCreate = time::Time::FromUnix(STAT_TIME(s, c)); // st.st_mtim.tv_sec + st.st_mtim.tv_nsec * (1.0 / 1000000000);
	attribs.TimeModify = time::Time::FromUnix(STAT_TIME(s, m)); // st.st_mtim.tv_sec + st.st_mtim.tv_nsec * (1.0 / 1000000000);
	attribs.Size       = s.st_size;
	return Error();
#endif
}

IMQS_PAL_API bool PathExists(const std::string& path) {
	FileAttributes a;
	return Stat(path, a).OK();
}

IMQS_PAL_API bool IsDir(const std::string& path) {
	FileAttributes a;
	return Stat(path, a).OK() && a.IsDir;
}

IMQS_PAL_API bool IsFile(const std::string& path) {
	FileAttributes a;
	return Stat(path, a).OK() && !a.IsDir;
}

IMQS_PAL_API bool IsExist(Error err) {
	return err == ErrEEXIST;
}

IMQS_PAL_API bool IsNotExist(Error err) {
	return err == ErrENOENT;
}

IMQS_PAL_API Error MkDir(const std::string& dir) {
#ifdef _WIN32
	return CreateDirectoryA(dir.c_str(), NULL) ? Error() : ErrorFrom_GetLastError(GetLastError());
#else
	if (mkdir(dir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0)
		return Error();
	else
		return ErrorFrom_errno(errno);
#endif
}

IMQS_PAL_API Error MkDirAll(const std::string& dir) {
	auto err = MkDir(dir);
	if (err.OK() || IsExist(err))
		return Error();

	std::string buf;
	bool        end = false;
	for (size_t i = 0; !end; i++) {
		if (i == dir.length())
			end = true;
		if (!end)
			buf += dir[i];
		if (end || path::IsSeparator(dir[i])) {
#ifdef _WIN32
			// skip drive letters
			if (buf.length() == 3 && buf[1] == ':')
				continue;
#endif
			err = MkDir(buf);
			if (err.OK() || IsExist(err))
				continue;
			else
				return err;
		}
	}

	return Error();
}

IMQS_PAL_API Error Remove(const std::string& path) {
#ifdef _WIN32
	DWORD attribs = GetFileAttributes(path.c_str());
	if (attribs == INVALID_FILE_ATTRIBUTES)
		return os::ErrorFrom_GetLastError(GetLastError());

	if (!!(attribs & FILE_ATTRIBUTE_DIRECTORY))
		return ::RemoveDirectoryA(path.c_str()) ? Error() : os::ErrorFrom_GetLastError(GetLastError());
	else
		return ::DeleteFileA(path.c_str()) ? Error() : os::ErrorFrom_GetLastError(GetLastError());
#else
	if (remove(path.c_str()) == 0)
		return Error();
	else
		return ErrorFrom_errno(errno);
#endif
}

IMQS_PAL_API Error RemoveAll(const std::string& path) {
	auto eRemove = Remove(path);
	if (eRemove.OK() || IsNotExist(eRemove))
		return Error();

	FileAttributes st;
	auto           err = Stat(path, st);
	if (!err.OK()) {
		// Race between Remove and Stat - file was deleted in between
		if (IsNotExist(err))
			return Error();
		return err;
	}

	// If it's a file, then return the original error from Remove
	if (!st.IsDir)
		return eRemove;

	// Recurse into directory
	Error errInsideFind;
	err = FindFiles(path, [&errInsideFind](const FindFileItem& item) -> bool {
		if (item.IsDir) {
			errInsideFind = RemoveAll(item.FullPath());
			// Tell FindFiles not to descend into this directory, because we have just deleted it.
			return false;
		} else {
			errInsideFind = Remove(item.FullPath());
			return errInsideFind.OK();
		}
	});
	if (err.OK())
		err = errInsideFind;
	if (!err.OK())
		return err;

	// Delete path
	err = Remove(path);
	if (IsNotExist(err))
		return Error();
	return err;
}

// NOTE: Initially, ReadWholeFile_Internal was shared between unix and Win32, but I kept getting crashes in MSVCRT
// in the Windows version, which I could never understand. So I rewrote the Win32 version using native Win32 APIs.

#ifdef _WIN32
// This takes either buf_target or str_target.
Error ReadWholeFile_Internal(const std::string& filename, void** buf_target, std::string* str_target, size_t& len) {
	if (buf_target)
		*buf_target = nullptr;
	len       = 0;
	HANDLE fh = CreateFileW(towide(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE)
		return ErrorFrom_GetLastError();

	int64_t flen = 0;
	if (!GetFileSizeEx(fh, (PLARGE_INTEGER) &flen)) {
		auto err = GetLastError();
		CloseHandle(fh);
		return ErrorFrom_GetLastError(err);
	}

	// +1 for our null terminator. Refuse to read more than 2GB on a 32-bit system, which is reasonable (ie 4GB would be crazy, you need space for kernel, etc).
	if ((flen + 1) > (int64_t) INTPTR_MAX) {
		CloseHandle(fh);
		return Error(tsf::fmt("File (%v) is too large (%v MB) to read into memory", filename, flen / (1024 * 1024)));
	}

	void* buf = nullptr;
	if (buf_target) {
		size_t bufSize = (size_t)(flen + 1);
		*buf_target    = malloc(bufSize);
		if (*buf_target == nullptr) {
			CloseHandle(fh);
			return Error(tsf::fmt("Out of memory allocating %v bytes, to read file (%v)", bufSize, filename));
		}
		buf = *buf_target;
	} else {
		try {
			str_target->resize(flen);
		} catch (std::bad_alloc& e) {
			CloseHandle(fh);
			return Error(tsf::fmt("Out of memory allocating %v bytes, to read file (%v). Error = '%v'", flen, filename, e.what()));
		}
		buf = &str_target->at(0);
	}

	const size_t CHUNK = 64 * 1024 * 1024;
	size_t       pos   = 0;
	while (pos < (size_t) flen) {
		size_t chunk = std::min(flen - pos, CHUNK);
		DWORD  n     = 0;
		if (!ReadFile(fh, (uint8_t*) buf + pos, (DWORD) chunk, &n, NULL)) {
			auto err = GetLastError();
			if (buf_target) {
				free(*buf_target);
				*buf_target = nullptr;
			} else {
				str_target->resize(0);
			}
			CloseHandle(fh);
			return ErrorFrom_GetLastError(err);
		} else if (n == 0) {
			// file has been truncated since we read it's length
			break;
		}
		pos += n;
	}
	len = pos;
	if (len != flen && str_target)
		str_target->resize(len);

	if (buf_target) {
		// add null terminator
		auto b8 = (uint8_t*) *buf_target;
		b8[len] = 0;
	}
	CloseHandle(fh);
	return Error();
}
#else
// This takes either buf_target or str_target.
Error ReadWholeFile_Internal(const std::string& filename, void** buf_target, std::string* str_target, size_t& len) {
	if (buf_target)
		*buf_target = nullptr;
	len    = 0;
	int fd = open(filename.c_str(), O_BINARY | O_RDONLY, 0);
	if (fd == -1)
		return os::ErrorFrom_errno(errno);

	// Measure file length
	int64_t flen = lseek64(fd, 0, SEEK_END);
	if (flen == -1) {
		auto err = errno;
		close(fd);
		return os::ErrorFrom_errno(err);
	}

	// Seek back to start
	int64_t seek_start = lseek64(fd, 0, SEEK_SET);
	if (seek_start == -1) {
		auto err = errno;
		close(fd);
		return os::ErrorFrom_errno(err);
	}

	// +1 for our null terminator. Refuse to read more than 2GB on a 32-bit system, which is reasonable (ie 4GB would be crazy, you need space for kernel, etc).
	if ((flen + 1) > (int64_t) INTPTR_MAX) {
		close(fd);
		return Error(tsf::fmt("File (%v) is too large (%v MB) to read into memory", filename, flen / (1024 * 1024)));
	}

	void* buf = nullptr;
	if (buf_target) {
		size_t bufSize = (size_t)(flen + 1);
		*buf_target    = malloc(bufSize);
		if (*buf_target == nullptr) {
			close(fd);
			return Error(tsf::fmt("Out of memory allocating %v bytes, to read file (%v)", bufSize, filename));
		}
		buf = *buf_target;
	} else {
		try {
			str_target->resize(flen);
		} catch (std::bad_alloc& e) {
			close(fd);
			return Error(tsf::fmt("Out of memory allocating %v bytes, to read file (%v). Error = '%v'", flen, filename, e.what()));
		}
		buf = &str_target->at(0);
	}

	const size_t CHUNK = 64 * 1024 * 1024;
	len                = flen;
	for (size_t pos = 0; pos < len;) {
		size_t chunk = std::min(len - pos, CHUNK);
		auto   n     = read(fd, (uint8_t*) buf + pos, (unsigned int) chunk);
		if (n == -1 || n == 0) {
			len = 0;
			if (buf_target) {
				free(*buf_target);
				*buf_target = nullptr;
			} else {
				str_target->resize(0);
			}
			close(fd);
			return os::ErrorFrom_errno(errno);
		}
		pos += n;
	}

	if (buf_target) {
		// add null terminator
		auto b8 = (uint8_t*) *buf_target;
		b8[len] = 0;
	}
	close(fd);
	return Error();
}
#endif

IMQS_PAL_API Error ReadWholeFile(const std::string& filename, void*& buf, size_t& len) {
	return ReadWholeFile_Internal(filename, &buf, nullptr, len);
}

IMQS_PAL_API Error ReadWholeFile(const std::string& filename, std::string& buf) {
	size_t len;
	return ReadWholeFile_Internal(filename, nullptr, &buf, len);
}

IMQS_PAL_API Error WriteWholeFile(const std::string& filename, const void* buf, size_t len) {
#ifdef _WIN32
	int fd = open(filename.c_str(), O_BINARY | O_CREAT | O_RDWR | O_TRUNC, _S_IREAD | _S_IWRITE);
#else
	int         fd    = open(filename.c_str(), O_BINARY | O_CREAT | O_RDWR | O_TRUNC, 0664);
#endif
	if (fd == -1)
		return os::ErrorFrom_errno(errno);

	size_t remain = len;
	while (remain) {
		size_t bytes_to_write = std::min(remain, (size_t) 20 * 1024 * 1024);
		int    written        = write(fd, ((char*) buf) + (len - remain), (unsigned int) bytes_to_write);
		if (written == -1) {
			auto err = os::ErrorFrom_errno(errno);
			close(fd);
			return err;
		}
		remain -= written;
	}

	close(fd);
	return Error();
}

IMQS_PAL_API Error WriteWholeFile(const std::string& filename, const std::string& buf) {
	return WriteWholeFile(filename, buf.c_str(), buf.length());
}

IMQS_PAL_API Error FileLength(const std::string& filename, uint64_t& len) {
	File f;
	auto err = f.Open(filename);
	if (!err.OK())
		return err;

	int64_t pos = -1;
	err         = f.SeekWithResult(0, io::SeekWhence::End, pos);
	if (!err.OK())
		return err;

	len = pos;
	return Error();
}

IMQS_PAL_API Error FindFiles(const std::string& _dir, std::function<bool(const FindFileItem& item)> callback) {
	if (_dir.length() == 0)
		return Error("Empty directory for FindFiles");

#ifdef _WIN32
	std::string fixed = _dir;
	if (_dir[_dir.length() - 1] == '\\')
		fixed.pop_back();

	Error            err;
	WIN32_FIND_DATAA fd;
	HANDLE           handle = FindFirstFileA((fixed + "\\*").c_str(), &fd);
	// FindFirstFile will never return 0, but /analyze doesn't seem to know that.
	if (handle == INVALID_HANDLE_VALUE || handle == 0) {
		// Distinguish between _dir not existing, and _dir being empty
		auto lastErr = GetLastError();
		if (lastErr == ERROR_PATH_NOT_FOUND)
			return ErrENOENT;
		// empty
		err = ErrorFrom_GetLastError(lastErr);
		if (IsNotExist(err))
			return Error();
		return err;
	}
	FindFileItem item;
	item.Root = fixed;
	while (true) {
		bool ignore = strcmp(fd.cFileName, ".") == 0 ||
		              strcmp(fd.cFileName, "..") == 0;
		if (!ignore) {
			item.IsDir      = !!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
			item.Name       = fd.cFileName;
			item.TimeCreate = time::Time::FromEpoch1601(FileTimeTo100NanoSeconds(fd.ftCreationTime));
			item.TimeModify = time::Time::FromEpoch1601(FileTimeTo100NanoSeconds(fd.ftLastWriteTime));
			bool res        = callback(item);
			if (item.IsDir) {
				if (res) {
					err = FindFiles(fixed + "\\" + item.Name, callback);
					if (!err.OK())
						break;
				}
			} else {
				// File
				if (!res)
					break;
			}
		}
		if (FALSE == FindNextFileA(handle, &fd)) {
			err = os::ErrorFrom_GetLastError(GetLastError());
			break;
		}
	}
	FindClose(handle);
	if (IsNotExist(err))
		return Error();
	return err;
#else
	std::string fixed = _dir;
	if (_dir[_dir.length() - 1] == '/')
		fixed.pop_back();

	DIR* d = opendir(fixed.c_str());
	if (!d)
		return ErrorFrom_errno(errno);

	FindFileItem item;
	item.Root           = fixed;
	struct dirent* iter = nullptr;
	Error          err;
	while (true) {
		iter = readdir(d);
		// NULL iter signals end of iteration
		if (iter == nullptr)
			break;
		if (strcmp(iter->d_name, ".") == 0)
			continue;
		if (strcmp(iter->d_name, "..") == 0)
			continue;
		item.IsDir = iter->d_type == DT_DIR;
		item.Name  = iter->d_name;
		struct stat st;
		if (stat((fixed + "/" + iter->d_name).c_str(), &st) != 0) {
			err = ErrorFrom_errno(errno);
			break;
		}
		item.TimeCreate = time::Time::FromUnix(STAT_TIME(st, c)); // st.st_mtim.tv_sec + st.st_mtim.tv_nsec * (1.0 / 1000000000);
		item.TimeModify = time::Time::FromUnix(STAT_TIME(st, m)); // st.st_mtim.tv_sec + st.st_mtim.tv_nsec * (1.0 / 1000000000);
		bool res        = callback(item);
		if (item.IsDir) {
			if (res) {
				err = FindFiles(fixed + "/" + item.Name, callback);
				if (!err.OK())
					break;
			}
		} else {
			// File
			if (!res)
				break;
		}
	}
	closedir(d);
	return err;
#endif
}

IMQS_PAL_API OS GetOS() {
#ifdef _WIN32
	return OS::Windows;
#else
	return OS::Linux;
#endif
}

IMQS_PAL_API bool CmdLineHasOption(int argc, char** argv, const char* option) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], option) == 0)
			return true;
	}
	return false;
}

IMQS_PAL_API const char* CmdLineGetOption(int argc, char** argv, const char* option) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], option) == 0 && i + 1 < argc)
			return argv[i + 1];
	}
	return nullptr;
}

IMQS_PAL_API int NumberOfCPUCores() {
#ifdef WIN32
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#elif MACOS
	int      nm[2];
	size_t   len = 4;
	uint32_t count;

	nm[0] = CTL_HW;
	nm[1] = HW_AVAILCPU;
	sysctl(nm, 2, &count, &len, NULL, 0);

	if (count < 1) {
		nm[1] = HW_NCPU;
		sysctl(nm, 2, &count, &len, NULL, 0);
		if (count < 1) {
			count = 1;
		}
	}
	return count;
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

IMQS_PAL_API bool IsDebuggerPresent() {
#ifdef _WIN32
	return ::IsDebuggerPresent() == TRUE;
#else
	// This code is courtesy of http://stackoverflow.com/questions/3596781/how-to-detect-if-the-current-process-is-being-run-by-gdb
	char buf[1024];
	bool debugger_present = false;

	int status_fd = open("/proc/self/status", O_RDONLY);
	if (status_fd == -1)
		return false;

	ssize_t num_read = read(status_fd, buf, sizeof(buf));

	if (num_read > 0) {
		static const char TracerPid[] = "TracerPid:";
		char*             tracer_pid;

		buf[std::min((size_t) num_read, sizeof(buf) - 1)] = 0;
		tracer_pid                                        = strstr(buf, TracerPid);
		if (tracer_pid)
			debugger_present = !!atoi(tracer_pid + sizeof(TracerPid) - 1);
	}

	return debugger_present;
#endif
}

IMQS_PAL_API bool IsRunningInContainer() {
#ifdef _WIN32
	return false;
#else
	// we're just relying on the C++ to emit the necessary conservative release fences here, which I think it does
	static volatile int32_t initialized = 0;
	static volatile int32_t inside      = 0;
	if (!initialized) {
		int fd = open("/proc/1/cgroup", O_RDONLY);
		if (fd == -1) {
			// unexpected
			return false;
		}
		char buf[512];
		auto n       = read(fd, buf, sizeof(buf) - 1);
		n            = n < sizeof(buf) ? n : sizeof(buf) - 1;
		buf[n]       = 0;
		bool _inside = strstr(buf, "/docker/") != nullptr || strstr(buf, "/lxc/") != nullptr;
		close(fd);
		inside      = _inside;
		initialized = 1;
	}
	return inside;
#endif
}

// New API added in Windows 10, version 1607 (but I haven't managed to get it to work with GetProcAddress)
#ifdef _WIN32
static HRESULT(WINAPI* _SetThreadDescription)(
    _In_ HANDLE hThread,
    _In_ PCWSTR lpThreadDescription);

// Old method that works with VS 2015 and any version of Windows
const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
	DWORD  dwType;     // Must be 0x1000.
	LPCSTR szName;     // Pointer to name (in user addr space).
	DWORD  dwThreadID; // Thread ID (-1=caller thread).
	DWORD  dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
static void _SetThreadName(DWORD dwThreadID, const char* threadName) {
	THREADNAME_INFO info;
	info.dwType     = 0x1000;
	info.szName     = threadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags    = 0;
#pragma warning(push)
#pragma warning(disable : 6320 6322)
	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*) &info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
#pragma warning(pop)
}
#endif

IMQS_PAL_API void SetThreadName(const char* name) {
#ifdef _WIN32
	_SetThreadName(GetCurrentThreadId(), name);
// This doesn't work, and I don't know why. GetProcAddress returns null, despite my Windows 10 version being 1607.
/*
	if (!_SetThreadDescription) {
		auto kernel = GetModuleHandleW(L"kernel32.dll");
		if (kernel) {
			auto                  func     = reinterpret_cast<decltype(_SetThreadDescription)>(GetProcAddress(kernel, "SetThreadDescription"));
			std::atomic_intptr_t* ptr      = (std::atomic_intptr_t*) &_SetThreadDescription;
			intptr_t              expected = 0;
			ptr->compare_exchange_strong(expected, (intptr_t) func);
			//_SetThreadDescription = func;
		}
	}
	if (_SetThreadDescription)
		_SetThreadDescription(GetCurrentThread(), towide(name).c_str());
	*/
#else
	return;
#endif
}

IMQS_PAL_API std::string ProcessPath() {
#ifdef _WIN32
	wchar_t buf[2048];
	GetModuleFileNameW(NULL, buf, arraysize(buf));
	buf[arraysize(buf) - 1] = 0;
	return toutf8(buf);
#else
	char buf[2048];
	buf[0] = 0;
	int r  = readlink("/proc/self/exe", buf, arraysize(buf) - 1);
	if (r < 0)
		return buf;

	if (r < sizeof(buf))
		buf[r] = 0;
	else
		buf[sizeof(buf) - 1] = 0;
	return buf;
#endif
}

IMQS_PAL_API std::string HostName() {
	char name[512];
	name[0] = 0;
#ifdef _WIN32
	DWORD size = arraysize(name);
	if (GetComputerNameEx(ComputerNameDnsFullyQualified, name, &size) == TRUE)
		name[size] = 0;
#else
	gethostname(name, arraysize(name));
#endif
	name[arraysize(name) - 1] = 0;
	return name;
}

IMQS_PAL_API std::string UserName() {
#ifdef _WIN32
	wchar_t name[256];
	name[0]    = 0;
	DWORD size = arraysize(name);
	if (GetUserNameW(name, &size) == TRUE)
		name[size] = 0;
	name[arraysize(name) - 1] = 0;
	return toutf8(name);
#else
	char name[512];
	name[0] = 0;
	getlogin_r(name, arraysize(name));
	name[arraysize(name) - 1] = 0;
	return name;
#endif
}

IMQS_PAL_API ohash::map<std::string, std::string> GetAllEnv() {
	ohash::map<std::string, std::string> vars;
#ifdef _WIN32
	wchar_t* env = GetEnvironmentStringsW();
	for (size_t i = 0; env[i];) {
		size_t eq = i;
		for (; env[eq] != '='; eq++) {
		}
		vars.insert(toutf8(env + i, eq - i), toutf8(env + eq + 1));
		for (; env[i]; i++) {
		}
		i++;
	}
	FreeEnvironmentStringsW(env);
#else
	for (size_t i = 0; environ[i]; i++) {
		const char* var = environ[i];
		size_t      eq  = 0;
		for (; var[eq] != '='; eq++) {
		}
		vars.insert(std::string(var, eq), std::string(var + eq + 1));
	}
#endif
	return vars;
}

IMQS_PAL_API std::string GetEnv(const char* var) {
#ifdef _WIN32
	const size_t staticSize = 512;
	wchar_t      buf[staticSize];
	buf[0]    = 0;
	DWORD len = (DWORD) staticSize;
	DWORD r   = GetEnvironmentVariableW(towide(var).c_str(), buf, len);
	if (r < staticSize)
		return toutf8(buf);
	wchar_t* dbuf = new wchar_t[r];
	dbuf[0]       = 0;
	r             = GetEnvironmentVariableW(towide(var).c_str(), dbuf, r);
	auto res      = toutf8(dbuf);
	delete[] dbuf;
	return res;
#else
	char* e = getenv(var);
	return e ? e : "";
#endif
}

IMQS_PAL_API bool SetEnv(const std::string& var, const std::string& val) {
#ifdef _WIN32
	if (val == "")
		return SetEnvironmentVariableW(towide(var).c_str(), nullptr) == TRUE;
	else
		return SetEnvironmentVariableW(towide(var).c_str(), towide(val).c_str()) == TRUE;
#else
	if (val == "")
		return unsetenv(var.c_str()) == 0;
	else
		return setenv(var.c_str(), val.c_str(), 1) == 0;
#endif
}

IMQS_PAL_API std::string FindInSystemPath(const std::string& filename) {
	auto path = GetEnv("PATH");
	if (path == "")
		return "";
	auto paths = strings::Split(path, SYSTEM_PATH_SPLITTER);
	for (const auto& p : paths) {
		auto           full = path::Join(p, filename);
		FileAttributes attrib;
		if (Stat(full, attrib).OK())
			return full;
	}
	return "";
}

IMQS_PAL_API std::string ExecutableExtension() {
#ifdef _WIN32
	return ".exe";
#else
	return "";
#endif
}

IMQS_PAL_API std::string SharedLibraryExtension() {
#ifdef _WIN32
	return ".dll";
#else
	return ".so";
#endif
}

IMQS_PAL_API void TraceStr(const char* str) {
#ifdef _WIN32
	OutputDebugStringW(towide(str).c_str());
#else
	write(fileno(stdout), str, strlen(str));
#endif
}

} // namespace os
} // namespace imqs

#ifdef _WIN32
#pragma warning(pop)
#endif
