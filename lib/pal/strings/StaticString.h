#pragma once

#include "../hash/FNV1a.h"
#include "../hash/xxhash.h"

namespace imqs {

// A small string built for one purpose only:
// We want to have a hash table like this
//   ohash::map<StaticString, Y>
// And we want to be able to perform lookups on it using "const char*"
// StaticString allows us to do that, because Z is a public member variable, so you
// can mess with the internals.
//
// Example:
//   ohash::map<StaticString, int> map;
//   map.insert("foo", 1);
//
//   // perform lookup without doing any string alloc or copy
//   StaticString ss;
//   ss.Z = a_const_char_string_pointer;
//   int val = map.get(ss);
//   ss.Z = nullptr;

class IMQS_PAL_API StaticString {
public:
	char* Z = nullptr;

	StaticString() {}
	StaticString(const StaticString& s);
	StaticString(StaticString&& s);
	StaticString(const std::string& s);
	StaticString(const char* s);
	~StaticString();

	StaticString& operator=(StaticString&& s);
	StaticString& operator=(const StaticString& s);
	StaticString& operator=(const std::string& s);
	StaticString& operator=(const char* s);

	bool operator==(const StaticString& b) const;
	bool operator!=(const StaticString& b) const { return !(*this == b); }
};

} // namespace imqs

namespace ohash {
template <>
inline hashkey_t gethashcode(const imqs::StaticString& k) {
	const char* z = k.Z;
	if (!z)
		return 0;

	// See http://aras-p.info/blog/2016/08/09/More-Hash-Function-Tests/

	size_t len = strlen(z);
	if (len <= 8)
		return fnv_32a_buf(z, len);
	else
		return XXH32(z, (uint32_t) len, 0);
}
} // namespace ohash