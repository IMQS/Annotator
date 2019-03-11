#pragma once

#include "../alloc.h"

namespace imqs {

// HashBuilder is used to collect bytes that can be fed into a hash function
// This makes it easy to collect a bunch of state in a well defined order,
// which you can then compute a hash function on.
// Compute your hash over [Buf .. Buf + Len) bytes.
class IMQS_PAL_API HashBuilder {
public:
	uint8_t* Buf    = nullptr;
	size_t   Len    = 0;
	size_t   Cap    = 0;
	bool     OwnBuf = false;

	template <size_t cap>
	HashBuilder(uint8_t (&buf)[cap]) : Buf(buf), Cap(cap) {
	}

	HashBuilder() {
	}

	~HashBuilder() {
		if (OwnBuf)
			free(Buf);
	}

	template <typename T>
	void Add(const T& v) {
		AddBytes(&v, sizeof(v));
	}

	void Add(const std::string& v) {
		AddBytes(v.c_str(), v.length());
	}

	void Add(const char* s) {
		AddBytes(s, strlen(s));
	}

	void Add(const HashBuilder& v) {
		AddBytes(v.Buf, v.Len);
	}

	template <typename T>
	void Add(const std::vector<T>& v) {
		for (const auto& el : v)
			Add(el);
	}

	void AddString(const char* s) {
		AddBytes(s, strlen(s));
	}

	void AddBytes(const void* b, size_t len) {
		Check(len);
		memcpy(Buf + Len, b, len);
		Len += len;
	}

private:
	void GrowForAdditional(size_t len);

	void Check(size_t len) {
		if (Len + len > Cap)
			GrowForAdditional(len);
	}
};

// InlineHashBuilder is a collection of helper functions that assist
// in adding state to an incremental hash function.
// eg
//  char digest[16];
//  InlineHashBuilder<hash::SpookyV2> hb;
//  hb.Add("foobar");
//  hb.Final(digest);
template <typename THash>
class InlineHashBuilder {
public:
	THash Hash;

	void Final(void* digest) {
		Hash.Final(digest);
	}

	template <typename T>
	void Add(const T& v) {
		AddBytes(&v, sizeof(v));
	}

	void Add(const std::string& v) {
		AddBytes(v.c_str(), v.length());
	}

	void Add(const char* s) {
		AddBytes(s, strlen(s));
	}

	void Add(const HashBuilder& v) {
		AddBytes(v.Buf, v.Len);
	}

	template <typename T>
	void Add(const std::vector<T>& v) {
		for (const auto& el : v)
			Add(el);
	}

	void AddString(const char* s) {
		AddBytes(s, strlen(s));
	}

	void AddBytes(const void* b, size_t len) {
		Hash.Update(b, len);
	}
};

} // namespace imqs
