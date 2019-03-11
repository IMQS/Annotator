#pragma once

// This file is a set of wrappers around hash functions, which give them a consistent
// interface. This was built so that InlineHashBuilder could be templated to use
// different hash functions.

#include "SpookyV2.h"
#include "xxhash.h"

namespace imqs {
namespace hash {

class XXH32 {
public:
	static const size_t HashLen = 4;

	void* State = nullptr;

	XXH32() {
		State = XXH32_init(0);
	}
	~XXH32() {
		if (State)
			XXH32_digest(State);
	}

	void Update(const void* msg, size_t len) {
		XXH32_update(State, msg, (unsigned) len);
	}

	void Final(void* digest) {
		uint32_t f;
		f     = XXH32_digest(State);
		State = nullptr;
		memcpy(digest, &f, HashLen);
	}
};

class XXH64 {
public:
	static const size_t HashLen = 8;

	void* State = nullptr;

	XXH64() {
		State = XXH64_init(0);
	}
	~XXH64() {
		if (State)
			XXH64_digest(State);
	}

	void Update(const void* msg, size_t len) {
		XXH64_update(State, msg, (unsigned) len);
	}

	void Final(void* digest) {
		uint64_t f;
		f     = XXH64_digest(State);
		State = nullptr;
		memcpy(digest, &f, HashLen);
	}
};

class SpookyV2 {
public:
	static const size_t HashLen = 16;

	SpookyHash H;

	SpookyV2() {
		H.Init(0, 0);
	}

	void Update(const void* msg, size_t len) {
		H.Update(msg, len);
	}

	void Final(void* digest) {
		uint64_t f[2];
		H.Final(f, f + 1);
		memcpy(digest, f, HashLen);
	}
};

} // namespace hash
} // namespace imqs
