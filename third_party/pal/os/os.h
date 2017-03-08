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

extern IMQS_PAL_API StaticError ErrEACCESS;
extern IMQS_PAL_API StaticError ErrEEXIST;
extern IMQS_PAL_API StaticError ErrEINVAL;
extern IMQS_PAL_API StaticError ErrEMFILE;
extern IMQS_PAL_API StaticError ErrENOENT;

struct FileAttributes {
	bool IsDir = false;
};

IMQS_PAL_API Error ErrorFrom_errno(int errno_);
#ifdef _WIN32
IMQS_PAL_API Error ErrorFrom_GetLastError(DWORD err);
#endif

IMQS_PAL_API void Sleep(imqs::time::Duration d);

IMQS_PAL_API Error Stat(const std::string& path, FileAttributes& attribs);
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
	std::string      Root; // Parent directory
	std::string      Name; // Name of file or directory
	imqs::time::Time TimeModify;
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

IMQS_PAL_API bool        CmdLineHasOption(int argc, char** argv, const char* option); // Returns true if -option or --option exists on the command-line
IMQS_PAL_API const char* CmdLineGetOption(int argc, char** argv, const char* option); // Get value of the form "option value". Returns value or null. Typically "--option value".
IMQS_PAL_API int         GetNumberOfCores();
IMQS_PAL_API bool        IsDebuggerPresent();
IMQS_PAL_API std::string HostName();
}
}