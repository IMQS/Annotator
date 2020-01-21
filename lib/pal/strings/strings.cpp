#include "pch.h"
#include "strings.h"
#include "../modp/modp_b16.h"

namespace imqs {

inline bool IsWhite(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

IMQS_PAL_API int64_t AtoI64(const char* s) {
#ifdef _MSC_VER
	return _atoi64(s);
#else
	return (int64_t) atoll(s);
#endif
}

// This is from http://www.jb.man.ac.uk/~slowe/cpp/itoa.html
template <typename TINT, typename TCH>
size_t ItoAT(TINT value, TCH* result, int base) {
	if (base < 2 || base > 36) {
		*result = '\0';
		return 0;
	}

	TCH *ptr = result, *ptr1 = result, tmp_char;
	TINT tmp_value;

	do {
		tmp_value = value;
		value /= base;
		*ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - value * base)];
	} while (value);

	// Apply negative sign
	if (tmp_value < 0)
		*ptr++ = '-';
	size_t written = (size_t)(ptr - result);
	*ptr--         = '\0';
	while (ptr1 < ptr) {
		tmp_char = *ptr;
		*ptr--   = *ptr1;
		*ptr1++  = tmp_char;
	}
	return written;
}

IMQS_PAL_API size_t ItoA(int value, char* result, int base) {
	return ItoAT(value, result, base);
}

IMQS_PAL_API size_t I64toA(int64_t value, char* result, int base) {
	return ItoAT(value, result, base);
}

IMQS_PAL_API std::string ItoA(int value, int base) {
	char buf[34];
	ItoA(value, buf, base);
	return buf;
}

IMQS_PAL_API std::string I64toA(int64_t value, int base) {
	char buf[66];
	I64toA(value, buf, base);
	return buf;
}

namespace strings {

IMQS_PAL_API void ToHex(uint8_t val, char* out) {
	const char* lut = "0123456789ABCDEF";
	out[0]          = lut[val >> 4];
	out[1]          = lut[val & 15];
}

IMQS_PAL_API void ToHex(const void* buf, size_t len, char* out) {
	const char* lut = "0123456789ABCDEF";
	auto        src = (const uint8_t*) buf;
	for (; len != 0; len--, src++, out += 2) {
		out[0] = lut[*src >> 4];
		out[1] = lut[*src & 15];
	}
	*out = 0;
}

IMQS_PAL_API std::string ToHex(const void* buf, size_t len) {
	std::string s;
	s.resize(len * 2);
	ToHex(buf, len, &s[0]);
	return s;
}

IMQS_PAL_API uint8_t FromHexChar(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + c - 'a';
	if (c >= 'A' && c <= 'F')
		return 10 + c - 'A';
	return 0;
}

IMQS_PAL_API uint8_t FromHexByte(const char* s) {
	return (FromHexChar(s[0]) << 4) | FromHexChar(s[1]);
}

IMQS_PAL_API uint8_t FromHexByte(char c1, char c2) {
	return (FromHexChar(c1) << 4) | FromHexChar(c2);
}

IMQS_PAL_API std::string FromHex(const char* s, size_t len) {
	if (len == -1)
		len = strlen(s);
	std::string r;
	r.resize(len / 2);

	for (size_t i = 0; i < r.size(); i++, s += 2) {
		r[i] = (char) FromHexByte(s);
	}
	return r;
}

IMQS_PAL_API uint32_t FromHex32(const char* s, size_t len) {
	if (len == -1)
		len = strlen(s);
	len = std::min(len, (size_t) 8);
	uint8_t dst[4];
	size_t  r = modp_b16_decode((char*) dst, s, len);
	switch (r) {
	case (size_t) -1: return -1;
	case 0: return 0;
	case 1: return dst[0];
	case 2: return ((uint32_t) dst[0] << 8) | dst[1];
	case 3: return ((uint32_t) dst[0] << 16) | ((uint32_t) dst[1] << 8) | dst[2];
	case 4: return ((uint32_t) dst[0] << 24) | ((uint32_t) dst[1] << 16) | ((uint32_t) dst[2] << 8) | dst[3];
	}
	IMQS_DIE(); // should not be reachable
	return 0;
}

IMQS_PAL_API std::string tolower(const std::string& s) {
	std::string low;
	low.reserve(s.length());
	for (int cp : utfz::cp(s))
		utfz::encode(low, ::tolower(cp));
	return low;
}

IMQS_PAL_API std::string toupper(const std::string& s) {
	std::string up;
	up.reserve(s.length());
	for (int cp : utfz::cp(s))
		utfz::encode(up, ::toupper(cp));
	return up;
}

static char ToLowerCh(char c) {
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

// (0..31).map{|n| n.to_s }.join(",")
// (32..63).map{|n| n.to_s }.join(",")
// (64..95).map{|n| n += 32 if n >= 65 && n <= 90; n.to_s }.join(",")
// (96..127).map{|n| n.to_s }.join(",")

// At first, I was tempted here to only make the lookup table include the range from 32..127, because that is by far
// the most frequently used ASCII range. However, that makes the table 96 bytes, which is 1.5 cache lines, so if you hit
// the full range of them, you'd still hit 2 cache lines. By making the table 128 characters big, we're still only two
// cache lines, but all of the character digits a-zA-Z lie inside the top 64 bytes, so they'd all be inside the same
// cache line. This also gets rid of the -32 subtraction that you need to perform to get into the range of the 96-byte
// table.
static char ToLowerTable[128] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
    64, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};

IMQS_PAL_API bool eqnocase(const char* a, const char* b) {
	size_t i = 0;
	for (; a[i] && b[i]; i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca <= 127 && cb <= 127) {
			// this branch is by far the most common, because the range 32..127 includes
			// the standard characters, and digits, punctuation, etc
			if (ToLowerTable[(unsigned) ca] != ToLowerTable[(unsigned) cb])
				return false;
		} else {
			if (ToLowerCh(ca) != ToLowerCh(cb))
				return false;
		}
	}
	return a[i] == 0 && b[i] == 0;
}

IMQS_PAL_API bool eqnocase(const std::string& a, const char* b) {
	return eqnocase(a.c_str(), b);
}

IMQS_PAL_API bool eqnocase(const std::string& a, const std::string& b) {
	return eqnocase(a, b.c_str());
}

IMQS_PAL_API std::string Replace(const std::string& s, const std::string& find, const std::string& replacement) {
	if (find.size() == 0)
		return s;
	std::string r;
	size_t      i = 0; // position in 's'
	for (;;) {
		size_t j = s.find(find, i);
		if (j == -1) {
			// no more occurrences of 'find'; Add remaining bytes.
			r.append(&s[i], s.size() - i);
			break;
		}
		r.append(&s[i], j - i);
		r.append(replacement);
		i = j + find.length();
	}
	return r;
}

IMQS_PAL_API bool StartsWith(const char* s, const char* prefix) {
	size_t i = 0;
	for (; s[i] && prefix[i]; i++) {
		if (s[i] != prefix[i])
			break;
	}
	return prefix[i] == 0;
}

IMQS_PAL_API bool StartsWith(const std::string& s, const char* prefix) {
	const char* a = s.c_str();
	size_t      i = 0;
	for (; a[i] && prefix[i]; i++) {
		if (a[i] != prefix[i])
			break;
	}
	return prefix[i] == 0;
}

IMQS_PAL_API bool EndsWith(const char* s, const char* suffix) {
	auto len1 = strlen(s);
	auto len2 = strlen(suffix);
	if (len2 > len1)
		return false;
	for (size_t i = 0; i != len2; i++) {
		if (s[len1 - len2 + i] != suffix[i])
			return false;
	}
	return true;
}

IMQS_PAL_API bool EndsWith(const std::string& s, const char* suffix) {
	auto pos = s.rfind(suffix, std::string::npos);
	if (pos == -1)
		return false;
	return pos == s.length() - strlen(suffix);
}

IMQS_PAL_API std::string TrimRight(const std::string& s) {
	size_t i = s.length() - 1;
	for (; i != -1; i--) {
		if (!IsWhite(s[i]))
			break;
	}
	return s.substr(0, i + 1);
}

IMQS_PAL_API std::string TrimLeft(const std::string& s) {
	size_t a   = 0;
	size_t len = s.length();
	for (; a != len; a++) {
		if (!IsWhite(s[a]))
			break;
	}
	if (a == len)
		return std::string();
	return s.substr(a);
}

IMQS_PAL_API std::string Trim(const std::string& s) {
	size_t a   = 0;
	size_t len = s.length();
	for (; a != len; a++) {
		if (!IsWhite(s[a]))
			break;
	}
	if (a == len)
		return std::string();
	size_t b = len - 1;
	for (; b != -1; b--) {
		if (!IsWhite(s[b]))
			break;
	}
	return s.substr(a, b - a + 1);
}

template <bool CaseSensitive>
bool MatchWildcardT(const char* s, const char* p) {
	if (*p == '*') {
		while (*p == '*')
			++p;
		if (*p == 0)
			return true;
		while (*s != 0 && !MatchWildcardT<CaseSensitive>(s, p)) {
			int cp;
			if (!utfz::next(s, cp))
				break;
		}
		return *s != 0;
	} else if (*p == 0 || *s == 0) {
		return *p == *s;
	} else {
		int px = utfz::decode(p);
		int sx = utfz::decode(s);

		if ((CaseSensitive ? (px == sx) : ::tolower(px) == ::tolower(sx)) || *p == '?') {
			int cp;
			utfz::next(s, cp);
			utfz::next(p, cp);
			return MatchWildcardT<CaseSensitive>(s, p);
		} else {
			return false;
		}
	}
}

IMQS_PAL_API bool MatchWildcard(const std::string& s, const std::string& p) {
	return MatchWildcardT<true>(s.c_str(), p.c_str());
}
IMQS_PAL_API bool MatchWildcard(const char* s, const char* p) {
	return MatchWildcardT<true>(s, p);
}
IMQS_PAL_API bool MatchWildcardNoCase(const std::string& s, const std::string& p) {
	return MatchWildcardT<false>(s.c_str(), p.c_str());
}
IMQS_PAL_API bool MatchWildcardNoCase(const char* s, const char* p) {
	return MatchWildcardT<false>(s, p);
}

IMQS_PAL_API std::vector<std::string> Split(const char* s, int delim) {
	std::vector<std::string> list;
	Split(s, delim, list);
	return list;
}

IMQS_PAL_API std::vector<std::string> Split(const std::string& s, int delim) {
	return Split(s.c_str(), delim);
}

IMQS_PAL_API std::string Join(const std::vector<std::string>& parts, const char* joiner) {
	std::string r;
	for (size_t i = 0; i < parts.size(); i++) {
		r += parts[i];
		if (i != parts.size() - 1)
			r += joiner;
	}
	return r;
}

} // namespace strings
} // namespace imqs
