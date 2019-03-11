#include "pch.h"
#include "JsonSchemas.h"

namespace imqs {

#ifndef IMQS_PAL_DISABLE_RAPIDJSON
namespace rj {

JsonSchemas::JsonSchemas() {
}

JsonSchemas::~JsonSchemas() {
	for (auto s : Schemas)
		delete s.second;
}

rapidjson::SchemaDocument* JsonSchemas::GetSchema(const char* schemaName) {
	IMQS_ASSERT(Schemas.contains(schemaName));
	return Schemas.get(schemaName);
}

Error JsonSchemas::Validate(const char* schemaName, const rapidjson::Value& val, std::string* detailedError) {
	auto schema = Schemas.get(schemaName);
	IMQS_ASSERT(schema != nullptr);
	rapidjson::SchemaValidator validator(*schema);
	if (!val.Accept(validator)) {
		if (detailedError) {
			rapidjson::StringBuffer sb;
			auto                    x1 = validator.GetInvalidSchemaPointer();
			auto                    x2 = validator.GetInvalidDocumentPointer();
			validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
			*detailedError += tsf::fmt("Invalid schema: %s\n", sb.GetString());
			*detailedError += tsf::fmt("Invalid keyword: %s\n", validator.GetInvalidSchemaKeyword());
			sb.Clear();
			validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);
			*detailedError += tsf::fmt("Invalid document: %s\n", sb.GetString());
		}
		return Error("Invalid JSON - does not conform to expected schema");
	}
	return Error();
}

Error JsonSchemas::ParseAndValidate(const char* schemaName, const char* json, size_t jsonLen, rapidjson::Document& doc, std::string* detailedError) {
	using namespace rapidjson;
	auto schema = Schemas.get(schemaName);
	IMQS_ASSERT(schema != nullptr);

	// Check JSON syntax
	ParseResult res = doc.Parse(json, jsonLen);
	if (!res)
		return Error::Fmt("Invalid JSON: %v (%v)", rapidjson::GetParseError_En(res.Code()), res.Offset());

	// Check schema
	rapidjson::SchemaValidator validator(*schema);
	if (!doc.Accept(validator))
		return Error("JSON does not conform to API schema");

	return Error();

	// The following sequence should work, according to the rapidjson docs and examples. However,
	// it does not. It only gives worse errors. In particular, the error message for invalid schema
	// is obscure. So we rather parse + validate in two steps. At least that way we can provide
	// sensible error responses.
	/*
	// Here we parse the JSON and validate the schema in one step, which the rapidjson docs claim
	// is faster than parsing and then validating.

	auto schema = Schemas.get(schemaName);
	IMQS_ASSERT(schema != nullptr);

	typedef EncodedInputStream<rapidjson::UTF8<char>, MemoryStream> instream_t;

	MemoryStream ms(json, jsonLen);
	instream_t   is(ms);

	SchemaValidatingReader<kParseDefaultFlags, instream_t, UTF8<>> reader(is, *schema);
	doc.Populate(reader);

	// Check JSON validity
	auto res = reader.GetParseResult();
	if (!res)
	return Error::Fmt("Invalid JSON: %v (%v)", rapidjson::GetParseError_En(res.Code()), res.Offset());

	// Validate the schema
	if (!reader.IsValid()) {
	// This sequence is straight from the rapidjson docs, but it doesn't work, at least not on
	// an example that I tried, where I did not include an object member that was marked as "required".
	// The only intelligible output that I get is that it knows that something was required. However,
	// it doesn't tell us anything beyond that.
	if (detailedError) {
	rapidjson::StringBuffer sb;
	auto                    x1 = reader.GetInvalidSchemaPointer();
	auto                    x2 = reader.GetInvalidDocumentPointer();
	reader.GetInvalidSchemaPointer().StringifyUriFragment(sb);
	*detailedError += tsf::fmt("Invalid schema: %s\n", sb.GetString());
	*detailedError += tsf::fmt("Invalid keyword: %s\n", reader.GetInvalidSchemaKeyword());
	sb.Clear();
	reader.GetInvalidDocumentPointer().StringifyUriFragment(sb);
	*detailedError += tsf::fmt("Invalid document: %s\n", sb.GetString());
	}
	return Error("JSON does not conform to API schema");
	}
	return Error();
	*/
}

void JsonSchemas::Install(const char* schemaName, const char* schema) {
	IMQS_ASSERT(!Schemas.contains(schemaName));
	rapidjson::Document sd;
	if (sd.Parse(schema).HasParseError()) {
		auto offset = (unsigned) sd.GetErrorOffset();
		auto msg = GetParseError_En(sd.GetParseError());
		fprintf(stderr, "\"Failed to parse schema(offset %u): %s\n",
			offset,
			msg);
		IMQS_ASSERT(false);
	}
	Schemas.insert(schemaName, new rapidjson::SchemaDocument(sd));
}

#endif
} // namespace rapidJason
} // namespace imqs