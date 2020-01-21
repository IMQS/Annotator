#include "pch.h"
#include "../os/os.h"
#include "xml.h"

namespace imqs {
namespace xml {

bool Serializer::GetAttrib(tinyxml2::XMLElement* el, const char* name, std::string& val) {
	const char* v = el->Attribute(name);
	if (v)
		val = v;
	return v != nullptr;
}

std::string Serializer::GetAttrib(tinyxml2::XMLElement* el, const char* name) {
	const char* v = el->Attribute(name);
	return v ? v : "";
}

IMQS_PAL_API Error ToError(tinyxml2::XMLError err) {
	return Error(tinyxml2::XMLDocument::ErrorIDToName(err));
}

IMQS_PAL_API Error ParseFile(const std::string& filename, tinyxml2::XMLDocument& doc) {
	std::string buf;
	auto        err = os::ReadWholeFile(filename, buf);
	if (!err.OK())
		return err;
	auto xerr = doc.Parse(buf.c_str(), buf.size());
	if (xerr != tinyxml2::XMLError::XML_SUCCESS)
		return Error::Fmt("Error parsing xml in %v: %v", filename, tinyxml2::XMLDocument::ErrorIDToName(xerr));
	return Error();
}

IMQS_PAL_API Error SaveFile(const std::string& filename, tinyxml2::XMLDocument& doc) {
	auto xerr = doc.SaveFile(filename.c_str());
	if (xerr != tinyxml2::XMLError::XML_SUCCESS)
		return Error::Fmt("Failed to write xml in %v: %v", filename, tinyxml2::XMLDocument::ErrorIDToName(xerr));
	return Error();
}

IMQS_PAL_API Error ParseString(const std::string& str, tinyxml2::XMLDocument& doc) {
	return ParseString(str.c_str(), doc, str.size());
}

IMQS_PAL_API Error ParseString(const char* str, tinyxml2::XMLDocument& doc, size_t strLen) {
	auto xerr = doc.Parse(str, strLen);
	if (xerr != tinyxml2::XMLError::XML_SUCCESS)
		return Error::Fmt("Error parsing xml: %v", tinyxml2::XMLDocument::ErrorIDToName(xerr));
	return Error();
}

IMQS_PAL_API std::string ToString(tinyxml2::XMLDocument& doc) {
	tinyxml2::XMLPrinter printer;
	doc.Accept(&printer);
	return std::string(printer.CStr(), printer.CStrSize() - 1); // -1 to remove null terminator
}

IMQS_PAL_API std::string ToString(tinyxml2::XMLElement* el) {
	tinyxml2::XMLPrinter printer;
	el->Accept(&printer);
	return std::string(printer.CStr(), printer.CStrSize() - 1); // -1 to remove null terminator
}

} // namespace xml
} // namespace imqs
