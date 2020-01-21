#pragma once

namespace imqs {
IMQS_PAL_API void* malloc_or_die_internal(size_t bytes, const char* file, int line);
IMQS_PAL_API void* realloc_or_die_internal(void* p, size_t bytes, const char* file, int line);
#define imqs_malloc_or_die(bytes) malloc_or_die_internal(bytes, __FILE__, __LINE__)
#define imqs_realloc_or_die(p, bytes) realloc_or_die_internal(p, bytes, __FILE__, __LINE__)
} // namespace imqs