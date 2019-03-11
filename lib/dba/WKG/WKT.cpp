#include "pch.h"
#include "../Attrib.h"
#include "WKT.h"

using namespace std;
using namespace imqs::gfx;

namespace imqs {
namespace dba {
namespace WKT {

Writer::Writer(bool hasZ, bool hasM, bool writeZMSuffix) {
	HasZ          = hasZ;
	HasM          = hasM;
	WriteZMSuffix = writeZMSuffix;
}

void Writer::WriteHead(const char* str) {
	LLWrite(str);
	if (HasM && HasZ && !WriteZMSuffix)
		LLWrite(" ZM (");
	else if (HasZ && !WriteZMSuffix)
		LLWrite(" Z (");
	else if (HasM && !WriteZMSuffix)
		LLWrite(" M (");
	else
		LLWrite(" (");
}

void Writer::OpenParen() { LLWrite("("); }
void Writer::CloseParen() { LLWrite(")"); }
void Writer::CommaSpace() { LLWrite(", "); }

void Writer::WriteEnd() {
	LLWrite(")");
}

void Writer::LLWrite(const char* str) {
	Output += str;
}

void Writer::WriteF(const char* formatString, ...) {
	const int MAXSIZE = 256 - 1;
	char      buf[MAXSIZE + 1];

	// We don't use the 'n' functions, because those do not error. It is definitely better to have an error shown than to fail silently.
	va_list va;
	va_start(va, formatString);
#if defined(_MSC_VER) && _MSC_VER >= 1400
	vsprintf_s(buf, MAXSIZE, formatString, va);
#else
	vsprintf(buf, formatString, va);
#endif
	va_end(va);
	buf[MAXSIZE] = 0;

	LLWrite(buf);
}

void Writer::Write(const gfx::Vec4d& p) {
	if (HasM && HasZ)
		WriteF("%.16g %.16g %.16g %.16g", p.x, p.y, p.z, p.w);
	else if (HasZ)
		WriteF("%.16g %.16g %.16g", p.x, p.y, p.z);
	else if (HasM)
		WriteF("%.16g %.16g %.16g", p.x, p.y, p.w);
	else
		WriteF("%.16g %.16g", p.x, p.y);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline int IMatch(const char* s1, const char* s2) {
	int i;
	for (i = 0; s1[i] && s2[i]; i++) {
		int a = islower((int) s1[i]) ? s1[i] : tolower(s1[i]);
		int b = islower((int) s2[i]) ? s2[i] : tolower(s2[i]);
		if (a != b)
			return i;
	}
	return i;
}

IMQS_DBA_API Type ParseFieldType(const char* str, GeomFlags* flags, bool* multi) {
	if (flags)
		*flags = GeomFlags::None;
	if (multi)
		*multi = false;
	int len = (int) strlen(str);
	if (len < 5)
		return Type::Null;

	// none of the basic types end in 'M' or 'Z'
	if (str[len - 2] == 'Z' && str[len - 1] == 'M') {
		if (flags)
			*flags |= GeomFlags::HasZ | GeomFlags::HasM;
	} else if (str[len - 1] == 'Z') {
		if (flags)
			*flags |= GeomFlags::HasZ;
	} else if (str[len - 1] == 'M') {
		if (flags)
			*flags |= GeomFlags::HasM;
	}

	if (IMatch(str, "MULTILINESTRING") == 15) {
		if (multi)
			*multi = true;
		return Type::GeomPolyline;
	}
	if (IMatch(str, "MULTIPOLYGON") == 12) {
		if (multi)
			*multi = true;
		return Type::GeomPolygon;
	}
	if (IMatch(str, "LINESTRING") == 10)
		return Type::GeomPolyline;
	if (IMatch(str, "MULTIPOINT") == 10)
		return Type::GeomMultiPoint;
	if (IMatch(str, "POLYGON") == 7)
		return Type::GeomPolygon;
	if (IMatch(str, "POINT") == 5)
		return Type::GeomPoint;
	if (IMatch(str, "GEOMETRY") == 8)
		return Type::GeomAny;
	return Type::Null;
}

IMQS_DBA_API const char* FieldType_OGC(Type ft, bool multi, bool hasM) {
	switch (ft) {
	case Type::GeomPoint: return hasM ? "POINTM" : "POINT";
	case Type::GeomMultiPoint: return hasM ? "MULTIPOINTM" : "MULTIPOINT";
	case Type::GeomPolyline: return multi ? (hasM ? "MULTILINESTRINGM" : "MULTILINESTRING") : (hasM ? "LINESTRINGM" : "LINESTRING");
	case Type::GeomPolygon: return multi ? (hasM ? "MULTIPOLYGONM" : "MULTIPOLYGON") : (hasM ? "POLYGONM" : "POLYGON");
	case Type::GeomAny: return "GEOMETRY";
	default:
		IMQS_DIE();
	}
	return nullptr;
}

class Parser {
public:
	struct Error {
		const char* Pos = nullptr;
		std::string Expected;
		std::string Msg;

		void Reset() {
			Pos = 0;
			Expected.clear();
			Msg.clear();
		}
		Error() { Reset(); }
		Error(const char* msg) {
			Reset();
			Msg = msg;
		}
	};

	const char* Src       = nullptr;
	const char* T         = nullptr;
	const char* End       = nullptr;
	bool        HasZ      = false;
	bool        HasM      = false;
	bool        MSSQLMode = false;
	int         SRID      = 0;
	Attrib*     Dst       = nullptr;
	Allocator*  Alloc     = nullptr;

	vector<double>   TempDbl;
	size_t           TempDblNV = 0; // number of vertices inside TempDbl, which is equal to TempDbl.size() / (2|3|4), where divisor depends on HasZ and HasM
	vector<uint32_t> TempI32;

	Error FirstError;

	string DescribeError() {
		string s = tsf::fmt("Character %d: ", int(FirstError.Pos - Src));
		if (FirstError.Expected != "")
			s += tsf::fmt("Expected '%s'", FirstError.Expected);
		else
			s += FirstError.Msg;
		return s;
	}

	bool   AtEnd() { return T >= End; }
	size_t Remain() {
		if (T >= End)
			return 0;
		return End - T;
	}

	void WS() {
		int p = 0;
		while ((T < End) && (*T == 32 || *T == 9 || *T == 10 || *T == 13)) {
			if (*T == 13) {
			} else if (*T == 10) {
			}
			T++;
		}
	}
	bool Expect(char ch, bool need = true, bool eatWS = true) {
		if (eatWS)
			WS();
		if (*T != ch) {
			if (need) {
				Error e;
				e.Expected = ch;
				throw e;
			} else
				return false;
		} else {
			T++;
			return true;
		}
	}
	void POpen() { Expect('('); }
	void PClose() { Expect(')'); }
	bool Comma(bool need = true) { return Expect(',', need); }

	int ToLower(int ch) {
		return ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch;
	}

	bool IsDigit(int ch, int n) {
		return (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+' || (n > 0 && (ch == 'e' || ch == 'E'));
	}

	int Match(int n, const char** str) {
		WS();
		for (int i = 0; i < n; i++) {
			int         j = 0;
			const char* x = T;
			const char* y = str[i];
			for (; *x && *y && x < End && ToLower(*x) == ToLower(*y); x++, y++) {
			}
			if (*y == 0) {
				T = x;
				return i;
			}
		}
		return -1;
	}

	double GNum() {
		WS();
		int  bp = 0;
		char buf[65];
		for (; !AtEnd() && bp < 64; T++, bp++) {
			if (IsDigit(*T, bp))
				buf[bp] = *T;
			else
				break;
		}
		buf[bp] = 0;
		return atof(buf);
	}

	void GPoint() {
		TempDblNV++;
		TempDbl.push_back(GNum());
		TempDbl.push_back(GNum());
		if (HasZ || (MSSQLMode && *T == 32))
			TempDbl.push_back(GNum());
		if (HasM || (MSSQLMode && *T == 32))
			TempDbl.push_back(GNum());
	}

	size_t VertexSize() {
		return 2 + (HasZ ? 1 : 0) + (HasM ? 1 : 0);
	}

	GeomFlags Flags() {
		GeomFlags f = GeomFlags::None;
		if (HasZ)
			f |= GeomFlags::HasZ;
		if (HasM)
			f |= GeomFlags::HasM;
		return f;
	}

	void ParsePoint() {
		POpen();
		GPoint();
		PClose();
		Dst->SetPoint(Flags(), &TempDbl[0], SRID, Alloc);
	}

	void ParsePointList(bool inParens) {
		TempI32.push_back((uint32_t) TempDblNV);
		POpen();
		while (true) {
			if (inParens)
				POpen();
			GPoint();
			if (inParens)
				PClose();
			if (!Comma(false))
				break;
		}
		PClose();
	}

	bool GMaybeClose() {
		size_t len    = TempDbl.size();
		size_t vxSize = VertexSize();
		if (TempDblNV >= 4 && TempDbl[0] == TempDbl[len - vxSize] && TempDbl[1] == TempDbl[1 + len - vxSize]) {
			TempDbl.erase(TempDbl.begin() + vxSize * (TempDblNV - 1), TempDbl.end());
			TempDblNV--;
			return true;
		}
		return false;
	}

	void ParseMultiPoint() {
		ParsePointList(true);
		Dst->SetMultiPoint(Flags(), (int) TempDblNV, &TempDbl[0], SRID, Alloc);
	}

	bool ParseRing() {
		ParsePointList(false);
		bool closed = GMaybeClose();
		if (closed)
			TempI32[TempI32.size() - 1] |= GeomPartFlag_Closed;
		return closed;
	}

	void ParseLineString() {
		ParseRing();
		CopyPolyOut(false);
	}

	void ParseMultiLineString() {
		ParsePoly(false);
		CopyPolyOut(false);
	}

	void ParsePolygon() {
		ParsePoly(true);
		CopyPolyOut(true);
	}

	void ParseMultiPolygon() {
		POpen();
		ParsePoly(true);
		while (Comma(false)) {
			ParsePoly(true);
		}
		PClose();
		CopyPolyOut(true);
	}

	void ParsePoly(bool isPolygon) {
		POpen();
		ParseRing();
		while (Comma(false)) {
			ParseRing();
		}
		PClose();
	}

	void CopyPolyOut(bool isPolygon) {
		TempI32.push_back((uint32_t) TempDblNV);
		Dst->SetPoly(isPolygon ? Type::GeomPolygon : Type::GeomPolyline, Flags(), (int) TempI32.size() - 1, &TempI32[0], &TempDbl[0], SRID, Alloc);
	}

	void Reset() {
		FirstError.Reset();
	}

	bool Parse() {
		try {
			// Params for Match must be longest first, if they share common prefixes, because Match returns first match, not longest match.
			static const char* prims[] = {"point", "multipoint", "linestring", "multilinestring", "polygon", "multipolygon"};
			static const char* zms[]   = {"zm", "z", "m"};

			int prim = Match(6, prims);
			if (prim == -1)
				throw Error("Expected POINT, MULTIPOINT, LINESTRING, MULTILINESTRING, POLYGON, or MULTIPOLYGON");

			int zm = Match(3, zms);
			HasZ = HasM = false;

			if (MSSQLMode)
				zm = 0;

			switch (zm) {
			case 0:
				HasZ = true;
				HasM = true;
				break;
			case 1: HasZ = true; break;
			case 2: HasM = true; break;
			}

			TempI32.clear();
			TempDbl.clear();
			TempDblNV = 0;

			switch (prim) {
			case 0: ParsePoint(); break;
			case 1: ParseMultiPoint(); break;
			case 2: ParseLineString(); break;
			case 3: ParseMultiLineString(); break;
			case 4: ParsePolygon(); break;
			case 5: ParseMultiPolygon(); break;
			}
			return true;
		} catch (Error& e) {
			FirstError     = e;
			FirstError.Pos = T;
			return false;
		}
	}
};

IMQS_DBA_API Error Parse(const char* str, size_t maxLen, Attrib& dst, Allocator* alloc, bool* hasZ, bool* hasM, bool isMSSQLMode) {
	if (maxLen == -1)
		maxLen = strlen(str);

	Parser pi;
	pi.Dst       = &dst;
	pi.Src       = str;
	pi.End       = pi.Src + maxLen;
	pi.T         = str;
	pi.MSSQLMode = isMSSQLMode;
	pi.Alloc     = alloc;
	if (!pi.Parse())
		return Error(pi.DescribeError());

	if (hasZ)
		*hasZ = pi.HasZ;
	if (hasM)
		*hasM = pi.HasM;
	return Error();
}

} // namespace WKT
} // namespace dba
} // namespace imqs
