#pragma once

#include "../io/io.h"
#include "../io/Buffer.h"

namespace imqs {
namespace csv {

// Decoder decodes one CSV line at a time
class IMQS_PAL_API Decoder {
public:
	char Separator = ',';
	char Quote     = '"';

	Decoder(io::Reader* r = nullptr);
	~Decoder();

	// Reset our buffer to zero. This is necessary if you are seeking around inside the CSV file
	void ResetBuffer();

	void SetReader(io::Reader* r);

	// Read the next line, appending the cells to 'decoded' and 'starts'.
	// The cell data is in
	// [starts[0], starts[1])
	// [starts[1], starts[2])
	//        ...  starts[N])
	// A terminal is always added to starts, so that the number of cells is starts.size() - 1.
	// An empty line produces a single empty cell, so starts.size() is never less than 2 (1 for the empty cell + 1 for the terminal).
	Error ReadLine(std::string& decoded, std::vector<size_t>& starts);

	// Clear 'decoded' and 'starts', and then ReadLine()
	Error ClearAndReadLine(std::string& decoded, std::vector<size_t>& starts);

	// Set the size of the buffer. This is exposed primarily for unit tests, but you might want to raise
	// the default buffer size (4096) if doing bulk reads.
	void SetBufferSize(size_t cap);

	// This can be used to determine the exact position of the reader. If you take the reader's current position
	// and add it to GetBufferPosBehindReader(), then you have the current decode position.
	int64_t GetBufferPosBehindReader() const { return (int64_t) BufPos - (int64_t) BufLen; }

private:
	io::Reader* Reader = nullptr;
	char*       Buf    = nullptr;
	size_t      BufPos = 0;
	size_t      BufLen = 0;
	size_t      BufCap = 4096;
};

// Encoder encodes a single CSV cell.
class IMQS_PAL_API Encoder {
public:
	Error Write(io::Writer* w, const char* str, size_t len = -1);          // Write only the cell - do not add the comma
	Error WriteWithComma(io::Writer* w, const char* str, size_t len = -1); // Write the cell and add a comma afterwards

private:
	std::vector<char> CellBuf; // This avoids many tiny writes for escaped cells.
};

} // namespace csv
} // namespace imqs
