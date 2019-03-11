#pragma once

#include "../io/io.h"

namespace imqs {
namespace io {

// Memory buffer
class IMQS_PAL_API Buffer : public Writer {
public:
	uint8_t* Buf = nullptr;
	size_t   Cap = 0;
	size_t   Len = 0;

	Buffer();
	~Buffer();

	Error Write(const void* buf, size_t len) override;

	void     Free();
	void     Add(const void* buf, size_t bytes);
	void     AddStr(const char* str, size_t len = -1);
	void     Ensure(size_t additionalBytes);        // If necessary, grow to accommodate an additional number of bytes
	void     WriteNull(size_t len);                 // Increase capacity and length, but do not fill new data with anything specific (just take whatever malloc gives us)
	size_t   Remain() const { return Cap - Len; }   // Number of bytes available in buffer
	uint8_t* WritePos() const { return Buf + Len; } // Write position

	// Big Endian
	void WriteUint16BE(uint16_t v);
	void WriteInt16BE(int16_t v);
	void WriteUint32BE(uint32_t v);
	void WriteInt32BE(int32_t v);
	void WriteUint64BE(uint64_t v);
	void WriteInt64BE(int64_t v);

	// Little Endian (assume we're on a little endian machine)
	void WriteUint8(uint8_t v) { Add(&v, sizeof(v)); }
	void WriteUint16(uint16_t v) { Add(&v, sizeof(v)); }
	void WriteUint32(uint32_t v) { Add(&v, sizeof(v)); }
	void WriteUint64(uint64_t v) { Add(&v, sizeof(v)); }
	void WriteInt8(int8_t v) { Add(&v, sizeof(v)); }
	void WriteInt16(int16_t v) { Add(&v, sizeof(v)); }
	void WriteInt32(int32_t v) { Add(&v, sizeof(v)); }
	void WriteInt64(int64_t v) { Add(&v, sizeof(v)); }
	void WriteFloat(float v) { Add(&v, sizeof(v)); }
	void WriteDouble(double v) { Add(&v, sizeof(v)); }
};

// Wraps a memory buffer in a Reader
class IMQS_PAL_API ByteReader : public Reader {
public:
	bool           OwnBuf = false;
	const uint8_t* Buf    = nullptr;
	size_t         Len    = 0;
	size_t         Pos    = 0;

	ByteReader() {}
	ByteReader(const void* buf, size_t len) : Buf((const uint8_t*) buf), Len(len) {}

	Error Read(void* buf, size_t& len) override;
};

// Wraps a string in a Reader
class IMQS_PAL_API StringReader : public Reader {
public:
	std::string Buf;
	size_t      Pos = 0;

	StringReader() {}
	StringReader(const void* buf, size_t len) { Buf.assign((const char*) buf, len); }
	StringReader(const std::string& buf) : Buf(buf) {}
	StringReader(std::string&& buf) : Buf(std::move(buf)) {}

	Error Read(void* buf, size_t& len) override;
};

// Wraps a string in a Writer
class IMQS_PAL_API StringWriter : public Writer {
public:
	std::string* Str = nullptr;

	StringWriter(std::string& target) : Str(&target) {}
	StringWriter() : Str(&OwnStr) {}

	Error Write(const void* buf, size_t len) override;

private:
	std::string OwnStr;
};

// Helper that is made for decoding binary streams
class IMQS_PAL_API Decoder {
public:
	Decoder(io::Reader* reader);
	Decoder(const void* buf, size_t len);
	~Decoder();

	int8_t   ReadInt8() { return ReadT<int8_t>(); }
	uint8_t  ReadUint8() { return ReadT<uint8_t>(); }
	int16_t  ReadInt16() { return ReadT<int16_t>(); }
	uint16_t ReadUint16() { return ReadT<uint16_t>(); }
	int32_t  ReadInt32() { return ReadT<int32_t>(); }
	uint32_t ReadUint32() { return ReadT<uint32_t>(); }
	int64_t  ReadInt64() { return ReadT<int64_t>(); }
	uint64_t ReadUint64() { return ReadT<uint64_t>(); }
	char     ReadChar() { return ReadT<char>(); }
	float    ReadFloat() { return ReadT<float>(); }
	double   ReadDouble() { return ReadT<double>(); }

	Error Read(void* buf, size_t& len);

	template <typename V>
	V ReadT() {
		V      v   = 0;
		size_t len = sizeof(v);
		Reader->Read(&v, len); // discard error
		Pos += len;
		return v;
	}

	// Returns the number of bytes decoded
	int64_t Position() const { return Pos; }

private:
	bool        OwnReader = false;
	io::Reader* Reader    = nullptr;
	int64_t     Pos       = 0;
};

inline void Buffer::WriteUint16BE(uint16_t v) {
	uint8_t b[sizeof(v)];
	b[0] = (uint8_t)(v >> 8);
	b[1] = (uint8_t)(v);
	Add(b, sizeof(b));
}

inline void Buffer::WriteInt16BE(int16_t v) {
	WriteUint16BE((uint16_t) v);
}

inline void Buffer::WriteUint32BE(uint32_t v) {
	uint8_t b[sizeof(v)];
	b[0] = (uint8_t)(v >> 24);
	b[1] = (uint8_t)(v >> 16);
	b[2] = (uint8_t)(v >> 8);
	b[3] = (uint8_t)(v);
	Add(b, sizeof(b));
}

inline void Buffer::WriteInt32BE(int32_t v) {
	WriteUint32BE((uint32_t) v);
}

inline void Buffer::WriteUint64BE(uint64_t v) {
	uint8_t b[sizeof(v)];
	b[0] = (uint8_t)(v >> 56);
	b[1] = (uint8_t)(v >> 48);
	b[2] = (uint8_t)(v >> 40);
	b[3] = (uint8_t)(v >> 32);
	b[4] = (uint8_t)(v >> 24);
	b[5] = (uint8_t)(v >> 16);
	b[6] = (uint8_t)(v >> 8);
	b[7] = (uint8_t)(v);
	Add(b, sizeof(b));
}

inline void Buffer::WriteInt64BE(int64_t v) {
	WriteUint64BE((uint64_t) v);
}

} // namespace io
} // namespace imqs