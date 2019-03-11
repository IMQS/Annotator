#pragma once

#include <vector>
#include <algorithm>

// Helper functions that augment the C++ standard library
// There are similar string-specific functions inside strings/strings.h

namespace imqs {
namespace stdutils {

template <typename Iter, typename T>
size_t indexof(Iter& begin, Iter& end, const T& val) {
	auto pos = std::find(begin, end, val);
	if (pos == end)
		return -1;
	return pos - begin;
}

template <typename T>
size_t indexof(std::vector<T>& v, const T& val) {
	auto pos = std::find(v.begin(), v.end(), val);
	if (pos == v.end())
		return -1;
	return pos - v.begin();
}

// Test whether container has at least one member equal to val
template <typename C, typename V>
bool contains(const C& container, const V& val) {
	for (const auto& v : container) {
		if (v == val)
			return true;
	}
	return false;
}

template <typename T>
void append_to(std::vector<T>& a, const std::vector<T>& b) {
	a.insert(a.end(), b.begin(), b.end());
}

} // namespace stdutils

/* I may some day have the energy to make this work easily.

// variable length argument helpers
namespace vargs {

// Generic packer, where type can be deduced from parameters
template <typename T>
void pack(T* pack) {
}

template <typename T>
void pack(T* pack, const T& arg) {
	*pack = arg;
}

template <typename T, typename... Args>
void pack(T* pack, const T& arg, const Args&... args) {
	*pack = arg;
	pack(pack + 1, args...);
}

// const char* packer
inline void pack_cchar(const char** pack) {
}

inline void pack_cchar(const char** pack, const char* arg) {
	*pack = arg;
}

template <typename... Args>
void pack_cchar(const char** pack, const char* arg, const Args&... args) {
	*pack = arg;
	pack_cchar(pack + 1, args...);
}

template <typename... Args>
void example_cchar(const char* fs, const Args&... args) {
	const auto  num_args = sizeof...(Args);
	const char* pack_array[num_args + 1]; // +1 for zero args case
	pack_cchar(pack_array, args...);
	return do_something(num_args, pack_array);
}

} // namespace vargs
*/

} // namespace imqs
