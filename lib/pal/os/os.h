#pragma once

#include <functional>
#include "../Time_.h"

namespace imqs {
namespace os {

#ifdef _WIN32
const char PathSeparator = '\\';
#else
const char PathSeparator = '/';
#endif

extern IMQS_PAL_API StaticError ErrEACCESS; // Access denied, or similar
extern IMQS_PAL_API StaticError ErrEEXIST;  // Object already exists
extern IMQS_PAL_API StaticError ErrEINVAL;  // Invalid input value to function
extern IMQS_PAL_API StaticError ErrEMFILE;
extern IMQS_PAL_API StaticError ErrENOENT; // Not found

enum class OS {
	Windows,
	Linux,
};

struct FileAttributes {
	imqs::time::Time TimeCreate; // Creation time
	imqs::time::Time TimeModify; // Last modification time
	uint64_t         Size  = 0;
	bool             IsDir = false;
};

IMQS_PAL_API Error ErrorFrom_errno();
IMQS_PAL_API Error ErrorFrom_errno(int errno_);
#ifdef _WIN32
IMQS_PAL_API Error ErrorFrom_GetLastError();
IMQS_PAL_API Error ErrorFrom_GetLastError(DWORD err);
#endif

IMQS_PAL_API void Sleep(imqs::time::Duration d);

IMQS_PAL_API Error Stat(const std::string& path, FileAttributes& attribs);
IMQS_PAL_API bool  PathExists(const std::string& path);
IMQS_PAL_API bool  IsDir(const std::string& path);     // Returns true if Stat(path) reveals that this is a directory
IMQS_PAL_API bool  IsFile(const std::string& path);    // Returns true if Stat(path) reveals that this is a file
IMQS_PAL_API bool  IsExist(Error err);                 // Returns true if the error indicates that a path already exists
IMQS_PAL_API bool  IsNotExist(Error err);              // Returns true if the error indicates that a path does not exist
IMQS_PAL_API Error MkDir(const std::string& dir);      // Create a directory
IMQS_PAL_API Error MkDirAll(const std::string& dir);   // Create a directory and all ancestors. Returns OK if the directory already exists.
IMQS_PAL_API Error Remove(const std::string& path);    // Delete the file or directory (if empty)
IMQS_PAL_API Error RemoveAll(const std::string& path); // Delete directory or file. If directory, deletes all contents, recursively. Returns the first error it encounters, or nil if no error, or path does not exist.

// Read the whole file.
// If successful, buf contains the file contents, allocated with malloc.
// The actual allocated size of 'buf' will always be one byte greater than 'len', and that
// one extra byte will be zero. This allows you to treat the input directly as a
// null terminated string.
IMQS_PAL_API Error ReadWholeFile(const std::string& filename, void*& buf, size_t& len);

// Read whole file into std::string
IMQS_PAL_API Error ReadWholeFile(const std::string& filename, std::string& buf);

IMQS_PAL_API Error WriteWholeFile(const std::string& filename, const void* buf, size_t len);
IMQS_PAL_API Error WriteWholeFile(const std::string& filename, const std::string& buf);

// Read file length
IMQS_PAL_API Error FileLength(const std::string& filename, uint64_t& len);

struct IMQS_PAL_API FindFileItem {
	std::string      Root;       // Parent directory
	std::string      Name;       // Name of file or directory
	imqs::time::Time TimeCreate; // Creation time
	imqs::time::Time TimeModify; // Last modification time
	bool             IsDir : 1;
	std::string      FullPath() const; // Returns Root + PathSeparator + Name
};

/*
Enumerate files and directories inside the given directory.
The directory name must be pure, without any wildcards (this function appends a '*' itself).

The callback must respond in the following ways:

- If 'item' is a directory
  Return true to cause the directory to be entered
  Return false to skip entering the directory

- If 'item' is a file
  Return true to cause iteration to continue
  Return false to stop iteration of this directory

This function returns an error if any error other than "no files found" was encountered.
If the directory does not exist, then the function returns an error.
*/
IMQS_PAL_API Error FindFiles(const std::string& dir, std::function<bool(const FindFileItem& item)> callback);

IMQS_PAL_API OS          GetOS();
IMQS_PAL_API bool        CmdLineHasOption(int argc, char** argv, const char* option); // Returns true if -option or --option exists on the command-line
IMQS_PAL_API const char* CmdLineGetOption(int argc, char** argv, const char* option); // Get value of the form "option value". Returns value or null. Typically "--option value".
IMQS_PAL_API int         NumberOfCPUCores();
IMQS_PAL_API bool        IsDebuggerPresent();
IMQS_PAL_API bool        IsRunningInContainer();          // Returns true if running under docker
IMQS_PAL_API void        SetThreadName(const char* name); // Useful for debugging. Only implemented on Windows 10 with SetThreadDescription
IMQS_PAL_API std::string ProcessPath();                   // Get the path of the currently executing process (eg c:\programs\foo.exe, or /usr/bin/foo)
IMQS_PAL_API std::string HostName();
IMQS_PAL_API std::string UserName();                                             // Not usable for security purposes on Linux (trivial for a user to spoof)
IMQS_PAL_API ohash::map<std::string, std::string> GetAllEnv();                   // Retrieve environment variables
IMQS_PAL_API std::string GetEnv(const char* var);                                // Retrieve environment variable
IMQS_PAL_API bool        SetEnv(const std::string& var, const std::string& val); // Set environment variable (always overwrite, even if already exists). To delete a variable, set 'val' to an empty string.
IMQS_PAL_API std::string FindInSystemPath(const std::string& filename);          // Search the system PATH for the given filename
IMQS_PAL_API std::string ExecutableExtension();                                  // ".exe" on windows, and "" on other platforms
IMQS_PAL_API std::string SharedLibraryExtension();                               // ".dll" on windows, and ".so" on other platforms
IMQS_PAL_API void        TraceStr(const char* str);                              // Trace to OutputDebugString, or puts(stdout) on linux

inline bool IsWindows() { return GetOS() == OS::Windows; }
inline bool IsLinux() { return GetOS() == OS::Linux; }

// Write to OutputDebugString, or stdout on linux
template <typename... Args>
void Trace(const char* format_str, const Args&... args) {
	TraceStr(tsf::fmt(format_str, args...).c_str());
}
} // namespace os
} // namespace imqs