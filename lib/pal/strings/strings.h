#pragma once

#include <utfz/utfz.h>

namespace imqs {

IMQS_PAL_API int64_t AtoI64(const char* s);
IMQS_PAL_API size_t ItoA(int value, char* result, int base);       // Returns size of string, excluding null terminator. Max 12 bytes written (including null terminator), for base 10
IMQS_PAL_API size_t I64toA(int64_t value, char* result, int base); // Returns size of string, excluding null terminator. Max 21 bytes written (including null terminator), for base 10
IMQS_PAL_API std::string ItoA(int value, int base = 10);
IMQS_PAL_API std::string I64toA(int64_t value, int base = 10);

namespace strings {
IMQS_PAL_API void ToHex(uint8_t val, char* out);                 // Does not add a null terminator
IMQS_PAL_API void ToHex(const void* buf, size_t len, char* out); // Always adds a null terminator, so length of 'out' must be len * 2 + 1
IMQS_PAL_API std::string ToHex(const void* buf, size_t len);
IMQS_PAL_API std::string FromHex(const char* s, size_t len);
IMQS_PAL_API uint8_t FromHex8(char c);                           // Convert one hex character to an integer
IMQS_PAL_API uint32_t FromHex32(const char* s, size_t len = -1); // Convert two characters at a time, starting at the front of the string.

IMQS_PAL_API std::string tolower(const std::string& s);
IMQS_PAL_API std::string toupper(const std::string& s);

IMQS_PAL_API bool eqnocase(const char* a, const char* b);               // Returns true if the strings are equal, assuming they are both ASCII
IMQS_PAL_API bool eqnocase(const std::string& a, const char* b);        // Returns true if the strings are equal, assuming they are both ASCII
IMQS_PAL_API bool eqnocase(const std::string& a, const std::string& b); // Returns true if the strings are equal, assuming they are both ASCII

// Replace all occurrences of 'find' with 'replacement'
IMQS_PAL_API std::string Replace(const std::string& s, const std::string& find, const std::string& replacement);

IMQS_PAL_API bool StartsWith(const char* s, const char* prefix);
IMQS_PAL_API bool StartsWith(const std::string& s, const char* prefix);
IMQS_PAL_API bool EndsWith(const std::string& s, const char* suffix);

IMQS_PAL_API std::string TrimLeft(const std::string& s);  // Trim whitespace (space, tab, newline, carriage return) from the left end of the string
IMQS_PAL_API std::string TrimRight(const std::string& s); // Trim whitespace (space, tab, newline, carriage return) from the right end of the string
IMQS_PAL_API std::string Trim(const std::string& s);      // Trim whitespace (space, tab, newline, carriage return) from the left and right of the string

IMQS_PAL_API bool MatchWildcard(const std::string& s, const std::string& p);
IMQS_PAL_API bool MatchWildcard(const char* s, const char* p);
IMQS_PAL_API bool MatchWildcardNoCase(const std::string& s, const std::string& p);
IMQS_PAL_API bool MatchWildcardNoCase(const char* s, const char* p);

// Split a UTF-8 string into a vector of strings
// delim: The splitting code point
template <typename TList>
void Split(const char* s, int delim, TList& list) {
	if (*s == 0)
		return;
	typedef typename TList::value_type TString;
	TString*                           item;
	list.push_back(TString());
	item = &list.back();
	for (auto cp : utfz::cp(s)) {
		if (cp == delim) {
			list.push_back(TString());
			item = &list.back();
		} else {
			char buf[5];
			int  nch = utfz::encode(buf, cp);
			for (int i = 0; i < nch; i++)
				*item += buf[i];
		}
	}
}

// Split into vector of strings
IMQS_PAL_API std::vector<std::string> Split(const char* s, int delim);
IMQS_PAL_API std::vector<std::string> Split(const std::string& s, int delim);

IMQS_PAL_API std::string Join(const std::vector<std::string>& parts, const char* joiner);
} // namespace strings
} // namespace imqs
