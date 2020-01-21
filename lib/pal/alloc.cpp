#include "pch.h"
#include "alloc.h"
#include "assert.h"

namespace imqs {

static IMQS_NORETURN void die_oom(size_t bytes, const char* file, int line) {
	char err[2048];
	sprintf(err, "Out of memory allocating %lld bytes, at %s:%d", (long long) bytes, file, line);
	err[sizeof(err) - 1] = 0;
	Die(file, line, err);
}

void* malloc_or_die_internal(size_t bytes, const char* file, int line) {
	void* buf = malloc(bytes);
	if (buf)
		return buf;
	die_oom(bytes, file, line);
	// this code is never reached
	return nullptr;
}

void* realloc_or_die_internal(void* p, size_t bytes, const char* file, int line) {
	void* buf = realloc(p, bytes);
	if (buf)
		return buf;
	die_oom(bytes, file, line);
	// this code is never reached
	return nullptr;
}
} // namespace imqs