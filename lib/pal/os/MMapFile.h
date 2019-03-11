#pragma once

#include "../io/io.h"
#include "File.h"

namespace imqs {
namespace os {

// Memory mapped file
// This provides a similar interface to File, but uses a memory
// mapped file underneath.
// When writing, we grow the file on demand, in chunks of up to 64 MB.
// When closing, we truncate the file to the maximum written size.
class IMQS_PAL_API MMapFile : public io::Writer, public io::Reader, public io::Seeker {
public:
	MMapFile();
	~MMapFile();

	Error Write(const void* buf, size_t len) override;
	Error Read(void* buf, size_t& len) override;
	Error SeekWithResult(int64_t offset, io::SeekWhence whence, int64_t& newPosition) override;

	Error ReadAt(int64_t pos, void* buf, size_t& len);       // Does not alter the seek position
	Error WriteAt(int64_t pos, const void* buf, size_t len); // Does not alter the seek position

	Error ReadExactly(void* buf, size_t len);                // Returns ErrEOF if the precise number of bytes could not be read
	Error ReadExactlyAt(int64_t pos, void* buf, size_t len); // Returns ErrEOF if the precise number of bytes could not be read. Does not alter seek position.

	Error Open(const std::string& filename);   // Open a file for read-only access
	Error Create(const std::string& filename); // Create a new file, or truncate an existing file

	int64_t  Length();
	int64_t  Position() const { return Pos; }
	bool     IsOpen() const;
	Error    Close();
	uint8_t* MemBase() const { return Base; }

private:
	bool        IsWrite = false;
	std::string Filename;
	int64_t     Pos          = 0;
	int64_t     CachedLength = 0;
	int64_t     MappedSize   = 0;
	uint8_t*    Base         = nullptr;
#ifdef _WIN32
	HANDLE HFile    = INVALID_HANDLE_VALUE;
	HANDLE HMapping = nullptr;
#else
	int FD = -1;
#endif

	Error OpenInternal(const std::string& filename, bool create);
	Error RecreateMapping(int64_t minSize = 0);
};

} // namespace os
} // namespace imqs