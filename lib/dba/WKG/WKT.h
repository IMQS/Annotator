#pragma once

// Well Known Text geometry reader and writer

namespace imqs {
namespace dba {
namespace WKT {

// WKT writer
struct Writer {
	std::string Output;
	bool        HasZ, HasM;
	bool        WriteZMSuffix = false;

	Writer(bool hasZ, bool hasM, bool writeZMSuffix = false);

	void WriteF(const char* formatString, ...);

	void WriteHead(const char* str);
	void OpenParen();
	void CloseParen();
	void CommaSpace();
	void WriteEnd();
	void Write(const gfx::Vec4d& p);
	void LLWrite(const char* str);
};

IMQS_DBA_API Type ParseFieldType(const char* str, GeomFlags* flags, bool* multi);
IMQS_DBA_API Error Parse(const char* str, size_t maxLen, Attrib& dst, Allocator* alloc = nullptr, bool* hasZ = NULL, bool* hasM = NULL, bool isMSSQLMode = false);

IMQS_DBA_API const char* FieldType_OGC(Type ft, bool multi, bool hasM); // For PostGIS geometry columns. Z is never included.

} // namespace WKT
} // namespace dba
} // namespace imqs
