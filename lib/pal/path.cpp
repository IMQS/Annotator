#include "pch.h"
#include "path.h"
#include "alloc.h"
#include "strings/strings.h"

namespace imqs {
namespace path {

#ifdef _WIN32
static const char Separator = '\\';
#else
static const char Separator = '/';
#endif

IMQS_PAL_API bool IsAnySeparator(int ch) {
	return ch == '/' || ch == '\\';
}

IMQS_PAL_API bool IsSeparator(int ch) {
#ifdef _WIN32
	return ch == '/' || ch == '\\';
#else
	return ch == '/';
#endif
}

IMQS_PAL_API bool IsAbsolute(const std::string& path) {
	if (path.size() >= 1 && path[0] == '/')
		return true;
	if (path.size() >= 2) {
		char d = path[0];
		if (((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z')) && path[1] == ':')
			return true;
	}
	return false;
}

//static size_t LastSep(const char* p) {
//	size_t i    = 0;
//	size_t last = -1;
//	for (; p[i]; i++) {
//		if (IsAnySeparator(p[i]))
//			last = i;
//	}
//	return last;
//}

static size_t LastSep(const std::string& p) {
	const char* pp = p.c_str();
	size_t      i  = p.size() - 1;
	for (; i != -1; i--) {
		if (IsAnySeparator(pp[i]))
			break;
	}
	return i;
}

IMQS_PAL_API std::string Dir(const std::string& path) {
	size_t lastSep = LastSep(path);
	if (lastSep == -1)
		return path;

	// Always return return / as parent directory of /x, or /
	if (lastSep == 0)
		return std::string(path, 0, 1);

	return std::string(path, 0, lastSep);
}

IMQS_PAL_API std::string Filename(const std::string& path) {
	size_t lastSep = LastSep(path);
	if (lastSep == -1)
		return path;

	return path.substr(lastSep + 1);
}

IMQS_PAL_API std::string MakeSlashesNative(const std::string& path) {
	std::string copy = path;
#ifdef _WIN32
	for (size_t i = 0; i < path.size(); i++) {
		if (copy[i] == '/')
			copy[i] = '\\';
	}
	return copy;
#else
	for (size_t i = 0; i < path.size(); i++) {
		if (copy[i] == '\\')
			copy[i] = '/';
	}
	return copy;
#endif
}

IMQS_PAL_API void SplitDir(const std::string& path, std::string& dir, std::string& filename) {
	size_t lastSep = LastSep(path);
	if (lastSep == -1 && path.size() >= 2 && path[1] == ':') {
		// Windows paths such as c:
		lastSep = 1;
	}

	if (lastSep == -1) {
		dir      = "";
		filename = path;
	} else {
		dir      = path.substr(0, lastSep + 1);
		filename = path.substr(lastSep + 1);
	}
}

IMQS_PAL_API void SplitExt(const std::string& path, std::string& name, std::string& ext) {
	ssize_t lastSep = (ssize_t) LastSep(path);
	ssize_t lastDot = (ssize_t) path.rfind('.');
	if (lastDot == -1 || lastDot < lastSep) {
		name = path;
		ext  = "";
		return;
	}
	name = path.substr(0, lastDot);
	ext  = path.substr(lastDot);
}

IMQS_PAL_API std::string Extension(const std::string& path) {
	std::string name, ext;
	SplitExt(path, name, ext);
	return ext;
}

IMQS_PAL_API std::string ChangeExtension(const std::string& path, const std::string& newExt) {
	std::string name, ext;
	SplitExt(path, name, ext);
	return name + newExt;
}

IMQS_PAL_API std::string SafeJoin(size_t n, const std::string** parts) {
	for (size_t i = 0; i < n; i++) {
		if (parts[i]->find("..") != -1)
			return *parts[0];
	}
	return Join(n, parts);
}

IMQS_PAL_API std::string SafeJoin(size_t n, const std::string* parts) {
	const size_t        statMax = 10;
	const std::string*  stat[statMax];
	const std::string** ptr = stat;
	if (n > statMax)
		ptr = (const std::string**) imqs_malloc_or_die(sizeof(void*) * n);
	for (size_t i = 0; i < n; i++)
		ptr[i] = &parts[i];
	auto res = SafeJoin(n, ptr);
	if (ptr != stat)
		free(ptr);
	return res;
}

IMQS_PAL_API std::string Join(size_t n, const std::string** parts) {
	std::string j;
	for (size_t i = 0; i < n; i++) {
		const auto& next = *parts[i];
		if (j.length() != 0 && !IsAnySeparator(j[j.length() - 1]))
			j += Separator;

		if (next.length() != 0 && j.length() != 0 && IsAnySeparator(next.at(0)))
			j += next.substr(1);
		else
			j += next;
	}

	return j;
}

IMQS_PAL_API std::string Join(const std::string& a, const std::string& b, const std::string& c, const std::string& d, const std::string& e) {
	const std::string* parts[5];
	size_t             n = 0;
	if (a != "")
		parts[n++] = &a;
	if (b != "")
		parts[n++] = &b;
	if (c != "")
		parts[n++] = &c;
	if (d != "")
		parts[n++] = &d;
	if (e != "")
		parts[n++] = &e;
	return Join(n, parts);
}

IMQS_PAL_API std::string SafeJoin(const std::string& a, const std::string& b, const std::string& c, const std::string& d, const std::string& e) {
	const std::string* parts[5];
	size_t             n = 0;
	if (a != "")
		parts[n++] = &a;
	if (b != "")
		parts[n++] = &b;
	if (c != "")
		parts[n++] = &c;
	if (d != "")
		parts[n++] = &d;
	if (e != "")
		parts[n++] = &e;
	return SafeJoin(n, parts);
}

} // namespace path
} // namespace imqs
