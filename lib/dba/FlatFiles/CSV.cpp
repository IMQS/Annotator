#include "pch.h"
#include "CSV.h"
#include "../Attrib.h"

using namespace std;

namespace imqs {
namespace dba {

Error CSV::Open(const std::string& filename, bool create) {
	IMQS_ASSERT(!create);
	auto err = File.Open(filename);
	if (!err.OK())
		return err;

	Decoder.SetReader(&File);

	// Since we're running off a memory mapped file, we actually want a small buffer.
	// If the reader is seeking, then we *especially* want a small buffer.
	Decoder.SetBufferSize(512);

	err = ReadFields();
	if (!err.OK())
		return err;

	err = ReadRecordStarts();
	if (!err.OK())
		return err;

	return Error();
}

int64_t CSV::RecordCount() {
	return RecordStarts.size();
}

std::vector<schema::Field> CSV::Fields() {
	return CachedFields;
}

Error CSV::Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) {
	if (record < -1 || record >= (int64_t) RecordStarts.size())
		return Error("Invalid record");

	if (record != LastRecord) {
		if (record != LastRecord + 1) {
			// seek to record
			Decoder.ResetBuffer();
			auto err = File.Seek(RecordStarts[record], io::SeekWhence::Begin);
			if (!err.OK())
				return err;
		}
		auto err = Decoder.ClearAndReadLine(Buf, BufCells);
		if (!err.OK())
			return err;
		LastRecord = record;
	}

	if (field >= CachedFields.size())
		return Error("Invalid field");

	if (field >= BufCells.size() - 1) {
		// line is missing values
		val.SetNull();
		return Error();
	}

	const char* b   = &Buf[0];
	auto        len = BufCells[field + 1] - BufCells[field];
	if (CachedFields[field].Type != Type::Text) {
		TmpAlloc.Reset();
		Attrib tmp;
		tmp.SetText(b + BufCells[field], len, &TmpAlloc);
		if (len != 0)
			tmp.CopyTo(CachedFields[field].Type, val, alloc);
		else
			val.SetNull();
	} else {
		val.SetText(b + BufCells[field], len, alloc);
	}

	return Error();
}

Error CSV::ReadFields() {
	auto err = File.Seek(0, io::SeekWhence::Begin);
	if (!err.OK())
		return err;
	err = Decoder.ReadLine(Buf, BufCells);
	if (!err.OK())
		return err;
	if (BufCells.size() == 0)
		return Error("CSV file is empty or invalid");
	for (size_t i = 0; i < BufCells.size() - 1; i++) {
		schema::Field f;
		f.Name = Buf.substr(BufCells[i], BufCells[i + 1] - BufCells[i]);
		f.Type = Type::Text;
		CachedFields.push_back(f);
	}
	for (size_t i = 0; i < CachedFields.size(); i++) {
		size_t k = 1;
		for (size_t j = i + 1; j < CachedFields.size(); j++) {
			if (CachedFields[i].Name != CachedFields[j].Name)
				continue;
			CachedFields[j].Name += "_" + ItoA((int) k++);
		}
	}
	return Error();
}

enum TypeMask {
	TMInt        = 1,
	TMReal       = 2,
	TMText       = 4,
	TMPoint      = 8,
	TMMultiPoint = 16,
	TMLine       = 32,
	TMPoly       = 64,
	TM_AllGeom   = TMPoint | TMMultiPoint | TMLine | TMPoly,
};

static int CSVElementType(const char* csv, size_t len) {
	if (len == 0)
		return 0;
	bool   dot   = false;
	bool   exp   = false;
	size_t digit = 0;
	for (size_t i = 0; i < len; i++) {
		char c = csv[i];
		if (c >= '0' && c <= '9') {
			digit++;
		} else if (c == '.') {
			if (dot)
				return TMText;
			dot = true;
		} else if (c == 'e') {
			if (exp)
				return TMText;
			exp = true;
			dot = false;
		} else if (c == '-') {
			if (i > 0 && csv[i - 1] == 'e') {
				// ok - negative exponent
			} else if (i == 0) {
				// ok - negative number
			} else {
				return TMText;
			}
		} else if (c == '+') {
			if (i > 0 && csv[i - 1] == 'e') {
				// ok - positive exponent
			} else {
				return TMText;
			}
		} else {
			return TMText;
		}
	}
	if (dot || exp)
		return TMReal;
	return TMInt;
}

static int CSVElementTypeWithGeom(const char* csv, size_t len) {
	int mask = CSVElementType(csv, len);
	if (mask != TMText)
		return mask;

	// Shorted possible geometry is POINT(0 0), which is 10 characters
	if (len < 10)
		return TMText;

	if (memcmp(csv, "POINT(", 6) == 0)
		return TMPoint;

	if (len > 11 && memcmp(csv, "LINESTRING(", 11) == 0)
		return TMLine;

	if (len > 8 && memcmp(csv, "POLYGON(", 8) == 0)
		return TMPoly;

	if (len > 11 && memcmp(csv, "MULTIPOINT(", 11) == 0)
		return TMMultiPoint;

	if (len > 16 && memcmp(csv, "MULTILINESTRING(", 16) == 0)
		return TMLine;

	if (len > 13 && memcmp(csv, "MULTIPOLYGON(", 13) == 0)
		return TMPoly;

	return TMText;
}

Error CSV::ReadRecordStarts() {
	// For every field, store a bit field of all types that we have seen inside that field
	vector<int> masks;
	for (size_t i = 0; i < CachedFields.size(); i++)
		masks.push_back(0);

	size_t lastLineLen = 0;
	Error  err;
	while (true) {
		int64_t pos = 0;
		err         = File.SeekWithResult(0, io::SeekWhence::Current, pos);
		if (!err.OK())
			return err;
		pos += Decoder.GetBufferPosBehindReader();
		err = Decoder.ClearAndReadLine(Buf, BufCells);
		if (!err.OK())
			break;
		RecordStarts.push_back(pos);
		if (ReadFieldTypesOnOpen) {
			for (size_t i = 0; i < BufCells.size() - 1 && i < masks.size(); i++) {
				if (masks[i] & TMText)
					continue;
				int mask = CSVElementTypeWithGeom(Buf.c_str() + BufCells[i], BufCells[i + 1] - BufCells[i]);
				masks[i] |= mask;
			}
		}
		lastLineLen = BufCells.back() - BufCells.front();
	}
	if (err != ErrEOF)
		return err;

	// Erase last record if it's empty (ie no commas, nothing). It's common to find a single blank line at the end of a file.
	if (lastLineLen == 0)
		RecordStarts.pop_back();

	if (ReadFieldTypesOnOpen) {
		// Look at the masks to figure out the field types. We must be conservative. If even one record
		// does not conform, then we have to fall back to text.
		for (size_t i = 0; i < CachedFields.size(); i++) {
			int  m = masks[i];
			Type t = Type::Text;
			if (m == TMPoint)
				t = Type::GeomPoint;
			else if (m == TMLine)
				t = Type::GeomPolyline;
			else if (m == TMPoly)
				t = Type::GeomPolygon;
			else if (m == TMMultiPoint)
				t = Type::GeomMultiPoint;
			else if ((m & ~TM_AllGeom) == 0 && (m & TM_AllGeom) != 0)
				t = Type::GeomAny;
			else if ((m & TMText) != 0)
				t = Type::Text;
			else if (m == TMInt)
				t = Type::Int64;
			else if ((m & ~(TMInt | TMReal)) == 0 && (m & (TMInt | TMReal)) != 0)
				t = Type::Double;
			CachedFields[i].Type = t;
		}
	}

	return Error();
}

} // namespace dba
} // namespace imqs
