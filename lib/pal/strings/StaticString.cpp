#include "pch.h"
#include "StaticString.h"
#include "../alloc.h"

namespace imqs {

StaticString::StaticString(const StaticString& s) {
	*this = s;
}

StaticString::StaticString(StaticString&& s) {
	*this = std::move(s);
}

StaticString::StaticString(const std::string& s) {
	*this = s;
}

StaticString::StaticString(const char* s) {
	*this = s;
}

StaticString::~StaticString() {
	free(Z);
}

StaticString& StaticString::operator=(StaticString&& s) {
	if (this != &s) {
		if (Z)
			free(Z);
		Z   = s.Z;
		s.Z = nullptr;
	}
	return *this;
}

StaticString& StaticString::operator=(const StaticString& s) {
	free(Z);
	Z = nullptr;
	if (s.Z) {
		auto len = strlen(s.Z);
		Z        = (char*) imqs_malloc_or_die(len + 1);
		memcpy(Z, s.Z, len + 1);
	}
	return *this;
}

StaticString& StaticString::operator=(const std::string& s) {
	free(Z);
	auto len = s.size();
	Z        = (char*) imqs_malloc_or_die(len + 1);
	memcpy(Z, s.c_str(), len + 1);
	return *this;
}

StaticString& StaticString::operator=(const char* s) {
	free(Z);
	auto len = strlen(s);
	Z        = (char*) imqs_malloc_or_die(len + 1);
	memcpy(Z, s, len + 1);
	return *this;
}

bool StaticString::operator==(const StaticString& b) const {
	if ((Z == nullptr) != (b.Z == nullptr))
		return false;

	if (Z == nullptr)
		return true;

	return strcmp(Z, b.Z) == 0;
}

} // namespace imqs
