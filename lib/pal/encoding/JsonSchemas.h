#pragma once

namespace imqs {

#ifndef IMQS_PAL_DISABLE_RAPIDJSON
namespace rj {

class IMQS_PAL_API JsonSchemas {
public:
	JsonSchemas();
	~JsonSchemas();

	rapidjson::SchemaDocument* GetSchema(const char* schemaName);
	Error                      Validate(const char* schemaName, const rapidjson::Value& val, std::string* detailedError = nullptr);
	Error                      ParseAndValidate(const char* schemaName, const char* json, size_t jsonLen, rapidjson::Document& doc, std::string* detailedError = nullptr);
	void                       Install(const char* schemaName, const char* schema);

private:
	ohash::map<std::string, rapidjson::SchemaDocument*> Schemas;
};

#endif
} // namespace rapidJason
} // namespace imqs
