#pragma once

#include "defs.h"

namespace imqs {
IMQS_PAL_API void IMQS_NORETURN Die(const char* file, int line, const char* msg);

#define IMQS_DIE() imqs::Die(__FILE__, __LINE__, "")
#define IMQS_DIE_MSG(msg) imqs::Die(__FILE__, __LINE__, msg)

// NOTE: This is compiled in all builds (Debug, Release)
#define IMQS_ASSERT(f) (void) ((f) || (imqs::Die(__FILE__, __LINE__, #f), 0))

// This is compiled in Debug builds only
#ifdef _DEBUG
#define IMQS_DEBUG_ASSERT(f) (void) ((f) || (imqs::Die(__FILE__, __LINE__, #f), 0))
#else
#define IMQS_DEBUG_ASSERT(f) (void) (0)
#endif

// Set a callback function that is called whenever Die() is called. You might want to log the issue, for example.
IMQS_PAL_API void SetAssertCallback(void* context, void (*callback)(void* context, const char* file, int line, const char* msg));
} // namespace imqs