#include "pch.h"
#include "asserts.h"

namespace imqs {

static void* DieCallbackContext;
static void (*DieCallback)(void* context, const char* file, int line, const char* msg);

void IMQS_NORETURN Die(const char* file, int line, const char* msg) {
	fprintf(stdout, "Program is self-terminating at %s:%d. (%s)\n", file, line, msg);
	fprintf(stderr, "Program is self-terminating at %s:%d. (%s)\n", file, line, msg);
	fflush(stdout);
	fflush(stderr);
	if (DieCallback)
		DieCallback(DieCallbackContext, file, line, msg);
	IMQS_DEBUG_BREAK();
#ifdef __clang__
	__builtin_trap();
#endif
}

IMQS_PAL_API void SetAssertCallback(void* context, void (*callback)(void* context, const char* file, int line, const char* msg)) {
	DieCallbackContext = context;
	DieCallback        = callback;
}

} // namespace imqs