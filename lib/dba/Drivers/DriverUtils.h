#pragma once

// Helper functions shared by different drivers

namespace imqs {
namespace dba {

// Count the number of bytes required to make up 'numChars' characters.
// buf is a UTF8 string that is 'bufBytes' long.
// This function is used by database writers to make sure that a string doesn't
// exceed the length specified for the field, for example VARCHAR(10).
// Returns the lesser of bufBytes, or the number of bytes inside 'buf' needed to
// make up exactly numChars characters.
IMQS_DBA_API size_t CountUtf8Bytes(size_t bufBytes, const char* buf, size_t numChars);

} // namespace dba
} // namespace imqs