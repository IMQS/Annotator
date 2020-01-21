#pragma once

namespace imqs {
namespace xml {

class IMQS_PAL_API Serializer {
public:
	static bool        GetAttrib(tinyxml2::XMLElement* el, const char* name, std::string& val);
	static std::string GetAttrib(tinyxml2::XMLElement* el, const char* name);

	template <typename T>
	static void SetAttribIfNotDefault(tinyxml2::XMLElement* el, const char* name, const T& val, const T& defaultVal) {
		if (val != defaultVal)
			el->SetAttribute(name, val);
	}
};

IMQS_PAL_API Error ToError(tinyxml2::XMLError err);
IMQS_PAL_API Error ParseFile(const std::string& filename, tinyxml2::XMLDocument& doc);
IMQS_PAL_API Error SaveFile(const std::string& filename, tinyxml2::XMLDocument& doc);
IMQS_PAL_API Error ParseString(const std::string& str, tinyxml2::XMLDocument& doc);
IMQS_PAL_API Error ParseString(const char* str, tinyxml2::XMLDocument& doc, size_t strLen = -1);
IMQS_PAL_API std::string ToString(tinyxml2::XMLDocument& doc);
IMQS_PAL_API std::string ToString(tinyxml2::XMLElement* el);

} // namespace xml
} // namespace imqs
