#include "pch.h"
#include "logging.h"
#include "os/os.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4091)
#include <Dbghelp.h>
#pragma warning(pop)
#else
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#include <signal.h>
#include <execinfo.h>
#endif

namespace imqs {
namespace logging {

static uberlog::Logger* CrashLogger;

IMQS_PAL_API void CreateLogger(const char* module, uberlog::Logger& logger) {
#ifdef _WIN32
	std::string logRootPath = "c:/imqsvar/logs/";
#else
	std::string logRootPath = "/var/log/imqs/";
#endif

	/*
	auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logRootPath + module, SPDLOG_FILENAME_T("log"), 10 * 1024 * 1024, 3);
	// Disable libc buffering of logs. This is bad for performance, because you get a kernel Write() call with every
	// log message. However, we do not want to lose a log message, if that log message is immediately succeeded by
	// a crash. A better solution is to have a child process which is doing the writes, and we communicate with that
	// child process via shared memory. Until we have such a system available, we do this.
	// spdlog also has an "asynchronous" mode, but that suffers from the same buffering problem.
	setvbuf(sink->file_handle(), nullptr, _IONBF, 0);
	auto log = std::make_shared<spdlog::logger>(module, sink);
	log->set_pattern("%Y-%m-%dT%H:%M:%S.%f%z [%L] %t %v");
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::off);
	return log;
	*/

	// Uberlog solves the above problem, and does so without any cost in performance.
	logger.SetArchiveSettings(30 * 1024 * 1024, 3);
	logger.Open((logRootPath + module + ".log").c_str());

	if (os::IsRunningInContainer()) {
		// When running in docker, we use Datadog to scrape logs from stdout, from all containers.
		logger.TeeStdOut = true;
	}
}

IMQS_PAL_API void SetCrashLogger(uberlog::Logger* logger) {
	CrashLogger = logger;
}

#ifdef _WIN32

namespace exception_handler {

static const size_t MaxModuleNameLength   = 30;
static const size_t MaxArchivedCrashDumps = 10;
static bool         IsBusy                = false;
static char         ModuleName[MaxModuleNameLength];

// We need to be careful during exception handling, not to do too much, because we
// risk causing a double exception.
// sprintf, for the case we use here, does not allocate memory. MSVC 2015 x64 Update 3.

static void NextCrashDumpFilename(char (&name)[256]) {
	char     oldest[256]  = {0};
	uint64_t oldest_mtime = UINT64_MAX;

	for (int i = 0; i < MaxArchivedCrashDumps; i++) {
		sprintf(name, "%s\\%s-%d.mdmp", CrashDumpDir(), ModuleName, i + 1);
		WIN32_FILE_ATTRIBUTE_DATA data;
		DWORD                     res = GetFileAttributesExA(name, GetFileExInfoStandard, &data);
		if (res == 0) {
			// assume an error means the file does not exist
			return;
		}
		uint64_t mtime = ((uint64_t) data.ftLastWriteTime.dwHighDateTime << 32) | (uint64_t) data.ftLastWriteTime.dwLowDateTime;
		if (mtime <= oldest_mtime) {
			oldest_mtime = mtime;
			strcpy(oldest, name);
		}
	}

	strcpy(name, oldest);
}

static LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exPointers) {
	if (IsBusy)
		return EXCEPTION_EXECUTE_HANDLER;
	IsBusy = true;

	char dumpFilename[256];
	NextCrashDumpFilename(dumpFilename);
	dumpFilename[arraysize(dumpFilename) - 1] = 0;

	// First location searched for by CreateProcess is the path of the current executable
	char crashHelperExe[512] = {0};
	strcat(crashHelperExe, "CrashHelper.exe ");
	strcat(crashHelperExe, ModuleName);
	strcat(crashHelperExe, " ");
	strcat(crashHelperExe, dumpFilename);

	CreateDirectoryA(CrashDumpDir(), NULL);

	HANDLE hFile = CreateFileA(dumpFilename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mExInfo;
		mExInfo.ThreadId          = GetCurrentThreadId();
		mExInfo.ExceptionPointers = exPointers;
		mExInfo.ClientPointers    = TRUE;
		// It's tempting to try things such as MiniDumpWithIndirectlyReferencedMemory, but until I can get some real-world statistics,
		// I'm too afraid to include it, for fear that the size might be too large.
		MINIDUMP_TYPE type    = MiniDumpNormal;
		bool          writeOK = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, type, &mExInfo, NULL, NULL) == TRUE;
		CloseHandle(hFile);
		if (writeOK) {
			// Launch a child process that will send the crash dump over the network
			STARTUPINFO si;
			memset(&si, 0, sizeof(si));
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi;
			bool                childOK = CreateProcessA(NULL, crashHelperExe, NULL, NULL, false, 0, NULL, NULL, &si, &pi) == TRUE;
			if (!childOK)
				printf("Unable to launch CrashHelper.exe\n");
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		}
	}

	IsBusy = false;
	return EXCEPTION_EXECUTE_HANDLER;
}

//static LONG WINAPI VectoredExceptionHandler(EXCEPTION_POINTERS* exPointers) {
//	DWORD code = exPointers->ExceptionRecord->ExceptionCode;
//	if (code == 0x406D1388) {
//		// This is the _SetThreadName function, which uses a special exception to inform the debugger of a thread name
//		// Once we have Windows 10 Creator's Edition, we can get rid of this mechanism, because they added SetThreadDescription
//		// to the kernel with that Windows version.
//		return EXCEPTION_CONTINUE_SEARCH;
//	}
//
//	return UnhandledExceptionHandler(exPointers);
//}

static LONG WINAPI HeapExceptionHandler(EXCEPTION_POINTERS* exPointers) {
	DWORD code = exPointers->ExceptionRecord->ExceptionCode;
	if (code == 0xC0000374)
		return UnhandledExceptionHandler(exPointers);

	return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace exception_handler

IMQS_PAL_API void SetupCrashHandler(const char* appName) {
	strncpy(exception_handler::ModuleName, appName, arraysize(exception_handler::ModuleName));
	exception_handler::ModuleName[arraysize(exception_handler::ModuleName) - 1] = 0;

	SetUnhandledExceptionFilter(&exception_handler::UnhandledExceptionHandler);

	// This is necessary to catch heap corruption.
	// http://stackoverflow.com/questions/19656946/why-setunhandledexceptionfilter-cannot-capture-some-exception-but-addvectoredexc
	// Basically, the heap catches this itself, and calls NtTerminateProcess. But we most
	// definitely want to be notified of it, so we also listen for it.
	// The exception code for this is 0xC0000374
	AddVectoredExceptionHandler(1, &exception_handler::HeapExceptionHandler);
}

IMQS_PAL_API const char* CrashDumpDir() {
	return "c:\\imqsvar\\crashdumps";
}

#else

static std::string PrintStackTrace(bool withAddr2Line, bool saveString) {
	unw_cursor_t cursor;
	unw_context_t context;

	// Initialize cursor to current frame for local unwinding.
	unw_getcontext(&context);
	unw_init_local(&cursor, &context);

	std::string str;
	if (!saveString)
		fprintf(stderr, "------------------------------------------------------------------\n");

	std::string self = "";
	if (withAddr2Line)
		self = os::ProcessPath();

	// Unwind frames one by one, going up the frame stack.
	while (unw_step(&cursor) > 0) {
		unw_word_t offset, pc;
		unw_get_reg(&cursor, UNW_REG_IP, &pc);
		if (pc == 0) {
			break;
		}

		if (saveString)
			str += tsf::fmt("0x%lx: ", pc);
		else
			fprintf(stderr, "0x%lx: ", pc);

		if (withAddr2Line) {
			char addr[500];
			sprintf(addr, "addr2line 0x%lx -e %s", pc, self.c_str());
			FILE* f = popen(addr, "r");
			if (f) {
				char buf[512];
				size_t n = 0;
				while ((n = fread(buf, 1, sizeof(buf), f)) != 0) {
					if (saveString)
						str.append(buf, n);
					else
						fwrite(buf, 1, n, stderr);
				}
				pclose(f);
			} else {
				if (saveString)
					str.append("addr2line failed\n");
				else
					fprintf(stderr, "addr2line failed\n");
			}
		} else {
			char sym[256];
			if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
				if (saveString)
					str += tsf::fmt("(%s+0x%lx)\n", sym, offset);
				else
					fprintf(stderr, "(%s+0x%lx)\n", sym, offset);
			} else {
				if (saveString)
					str += "-- error: unable to obtain symbol name for this frame\n";
				else
					fprintf(stderr, "-- error: unable to obtain symbol name for this frame\n");
			}
		}
	}
	return str;
}

static void AbortHandler(int sig, siginfo_t* siginfo, void* context) {
	const char* name = "unknown";
	switch (sig) {
	case SIGABRT: name = "SIGABRT"; break;
	case SIGSEGV: name = "SIGSEGV"; break;
	case SIGBUS: name = "SIGBUS"; break;
	case SIGILL: name = "SIGILL"; break;
	case SIGFPE: name = "SIGFPE"; break;
	}

	fprintf(stderr, "Caught signal %d (%s)\n", sig, name);

	// First print stacktrace without shelling out to addr2line, in case the shelling out somehow kills us.
	PrintStackTrace(false, false);

	// Second stack trace is the "risky" one, where we might end up killing ourselves.
	// But it's very nice to have the line numbers, obviously.
	PrintStackTrace(true, false);

	// Try outputting to log. Here we also do the risky option, which uses addr2line.
	if (CrashLogger) {
		auto str = PrintStackTrace(true, true);
		CrashLogger->Error("Caught signal %v (%v). Stack Trace:\n%v", sig, name, str);
	}

	exit(sig);
}

IMQS_PAL_API void SetupCrashHandler(const char* appName) {
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = AbortHandler;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGABRT, &sa, nullptr);
	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGBUS, &sa, nullptr);
	sigaction(SIGILL, &sa, nullptr);
	sigaction(SIGFPE, &sa, nullptr);
	sigaction(SIGPIPE, &sa, nullptr);
}

IMQS_PAL_API const char* CrashDumpDir() {
	return "/var/log/imqs/crashdumps";
}
#endif
} // namespace logging
} // namespace imqs
