#include "pch.h"
#include "json.h"
#include "../os/os.h"

namespace imqs {
#ifndef IMQS_PAL_DISABLE_RAPIDJSON
namespace rj {

static rapidjson::Value NullValue;

IMQS_PAL_API Error ParseString(const char* str, size_t len, rapidjson::Document& doc) {
	if (len == -1)
		doc.Parse(str);
	else
		doc.Parse(str, len);

	if (doc.HasParseError())
		return Error(tsf::fmt("JSON parse error '%s'", rapidjson::GetParseError_En(doc.GetParseError())));

	return Error();
}

IMQS_PAL_API Error ParseString(const std::string& str, rapidjson::Document& doc) {
	return ParseString(str.c_str(), str.length(), doc);
}

IMQS_PAL_API Error ParseFile(const std::string& filename, rapidjson::Document& doc) {
	std::string buf;
	auto        err = os::ReadWholeFile(filename, buf);
	if (!err.OK())
		return err;
	return ParseString(buf, doc);
}

IMQS_PAL_API std::string WriteString(const rapidjson::Document& doc) {
	rapidjson::StringBuffer                          buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	return buffer.GetString();
}

IMQS_PAL_API Error WriteFile(const rapidjson::Document& doc, const std::string& filename) {
	rapidjson::StringBuffer                          buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);
	return os::WriteWholeFile(filename, buffer.GetString(), buffer.GetSize());
}

IMQS_PAL_API void Set(rapidjson::Value& obj, const char* key, const char* value, rapidjson::MemoryPoolAllocator<>* allocator) {
	obj.AddMember(rapidjson::Value(key, *allocator).Move(), rapidjson::Value(value, *allocator).Move(), *allocator);
}

IMQS_PAL_API void Set(rapidjson::Value& obj, const char* key, const std::string& value, rapidjson::MemoryPoolAllocator<>* allocator) {
	Set(obj, key, value.c_str(), allocator);
}

IMQS_PAL_API void Set(rapidjson::Value& obj, const char* key, bool value, rapidjson::MemoryPoolAllocator<>* allocator) {
	obj.AddMember(rapidjson::Value(key, *allocator).Move(), value, *allocator);
}

IMQS_PAL_API void Set(rapidjson::Value& obj, const char* key, int64_t value, rapidjson::MemoryPoolAllocator<>* allocator) {
	obj.AddMember(rapidjson::Value(key, *allocator).Move(), value, *allocator);
}

IMQS_PAL_API void Set(rapidjson::Value& obj, const char* key, double value, rapidjson::MemoryPoolAllocator<>* allocator) {
	obj.AddMember(rapidjson::Value(key, *allocator).Move(), value, *allocator);
}

IMQS_PAL_API void Set(rapidjson::Document& doc, const char* key, const char* value) {
	Set(doc, key, value, &doc.GetAllocator());
}

IMQS_PAL_API void Set(rapidjson::Document& doc, const char* key, const std::string& value) {
	Set(doc, key, value.c_str(), &doc.GetAllocator());
}

IMQS_PAL_API void Set(rapidjson::Document& doc, const char* key, bool value) {
	Set(doc, key, value, &doc.GetAllocator());
}

IMQS_PAL_API void Set(rapidjson::Document& doc, const char* key, int64_t value) {
	Set(doc, key, value, &doc.GetAllocator());
}

IMQS_PAL_API void Set(rapidjson::Document& doc, const char* key, double value) {
	Set(doc, key, value, &doc.GetAllocator());
}

IMQS_PAL_API bool GetBool(const rapidjson::Value& v, const char* key, bool defaultValue) {
	auto iter = v.FindMember(key);
	if (iter != v.MemberEnd() && iter->value.IsBool())
		return iter->value.GetBool();
	else
		return defaultValue;
}

IMQS_PAL_API int GetInt(const rapidjson::Value& v, const char* key, int defaultValue) {
	auto iter = v.FindMember(key);
	if (iter != v.MemberEnd() && iter->value.IsNumber())
		return iter->value.GetInt();
	else
		return defaultValue;
}

IMQS_PAL_API int GetAnyAsInt(const rapidjson::Value& v, const char* key, int defaultValue) {
	auto iter = v.FindMember(key);
	if (iter == v.MemberEnd())
		return defaultValue;

	if (iter->value.IsInt())
		return iter->value.GetInt();
	else if (iter->value.IsNumber())
		return (int) iter->value.GetDouble();
	else if (iter->value.IsString())
		return atoi(iter->value.GetString());
	else
		return defaultValue;
}

IMQS_PAL_API int64_t GetInt64(const rapidjson::Value& v, const char* key, int64_t defaultValue) {
	auto iter = v.FindMember(key);
	if (iter != v.MemberEnd() && iter->value.IsNumber())
		return iter->value.GetInt64();
	else
		return defaultValue;
}

IMQS_PAL_API double GetDouble(const rapidjson::Value& v, const char* key, double defaultValue) {
	auto iter = v.FindMember(key);
	if (iter != v.MemberEnd() && iter->value.IsNumber())
		return iter->value.GetDouble();
	else
		return defaultValue;
}

IMQS_PAL_API std::string GetString(const rapidjson::Value& v, const char* key, std::string defaultValue) {
	auto iter = v.FindMember(key);
	if (iter != v.MemberEnd() && iter->value.IsString())
		return iter->value.GetString();
	else
		return defaultValue;
}

IMQS_PAL_API const rapidjson::Value& GetObject(const rapidjson::Value& v, const char* key) {
	auto it = v.FindMember(key);
	if (it == v.MemberEnd() || !it->value.IsObject())
		return NullValue;
	return it->value;
}

IMQS_PAL_API const rapidjson::Value& GetArray(const rapidjson::Value& v, const char* key) {
	auto it = v.FindMember(key);
	if (it == v.MemberEnd() || !it->value.IsArray())
		return NullValue;
	return it->value;
}

IMQS_PAL_API bool Bool(const rapidjson::Value& v, bool defaultValue) {
	return v.IsBool() ? v.GetBool() : defaultValue;
}

IMQS_PAL_API int Int(const rapidjson::Value& v, int defaultValue) {
	return v.IsNumber() ? v.GetInt() : defaultValue;
}

IMQS_PAL_API int64_t Int64(const rapidjson::Value& v, int64_t defaultValue) {
	return v.IsNumber() ? v.GetInt64() : defaultValue;
}

IMQS_PAL_API double Double(const rapidjson::Value& v, double defaultValue) {
	return v.IsNumber() ? v.GetDouble() : defaultValue;
}

IMQS_PAL_API std::string String(const rapidjson::Value& v, std::string defaultValue) {
	return v.IsString() ? v.GetString() : defaultValue;
}

IMQS_PAL_API std::vector<std::string> Keys(const rapidjson::Value& v) {
	std::vector<std::string> vals;
	for (const auto& m : v.GetObject())
		vals.push_back(m.name.GetString());
	return vals;
}

IMQS_PAL_API bool InOut(bool out, rapidjson::Document& doc, rapidjson::Value& v, const char* key, int64_t& value) {
	if (out) {
		Set(v, key, value, &doc.GetAllocator());
		return true;
	} else {
		auto iter = v.FindMember(key);
		if (iter == v.MemberEnd())
			return false;

		if (iter->value.IsInt64())
			value = iter->value.GetInt64();
		else if (iter->value.IsInt())
			value = iter->value.GetInt();
		else if (iter->value.IsUint64())
			value = iter->value.GetUint64();
		else if (iter->value.IsUint())
			value = iter->value.GetUint();
		else
			return false;

		return true;
	}
}

IMQS_PAL_API bool InOut(bool out, rapidjson::Document& doc, rapidjson::Value& v, const char* key, std::string& value) {
	if (out) {
		Set(v, key, value, &doc.GetAllocator());
		return true;
	} else {
		auto iter = v.FindMember(key);
		if (iter == v.MemberEnd() || !iter->value.IsString())
			return false;
		value = iter->value.GetString();
		return true;
	}
}
} // namespace rj
#endif

namespace nj {
static nlohmann::json NullValue;

IMQS_PAL_API Error ParseString(const std::string& raw, nlohmann::json& doc) {
	try {
		doc = nlohmann::json::parse(raw.begin(), raw.end());
		return Error();
	} catch (std::exception& e) {
		return Error::Fmt("Error parsing JSON %v", e.what());
	}
}

IMQS_PAL_API Error ParseFile(const std::string& filename, nlohmann::json& doc) {
	std::string buf;
	auto        err = os::ReadWholeFile(filename, buf);
	if (!err.OK())
		return err;

	try {
		doc = nlohmann::json::parse(buf.begin(), buf.end());
		return Error();
	} catch (std::exception& e) {
		return Error::Fmt("Error parsing JSON from %v: %v", filename, e.what());
	}
}

IMQS_PAL_API bool GetBool(const nlohmann::json& v, const char* key, bool defaultValue) {
	auto it = v.find(key);
	if (it != v.end() && it->is_boolean())
		return it->get<bool>();
	return defaultValue;
}

IMQS_PAL_API std::string GetString(const nlohmann::json& v, const char* key, std::string defaultValue) {
	auto it = v.find(key);
	if (it != v.end() && it->is_string())
		return it->get<std::string>();
	return defaultValue;
}

IMQS_PAL_API int GetInt(const nlohmann::json& v, const char* key, int defaultValue) {
	auto it = v.find(key);
	if (it != v.end() && it->is_number_integer())
		return it->get<int>();
	return defaultValue;
}

IMQS_PAL_API int GetAnyAsInt(const nlohmann::json& v, const char* key, int defaultValue) {
	auto it = v.find(key);
	if (it == v.end())
		return defaultValue;

	if (it->is_number_integer())
		return it->get<int>();
	else if (it->is_number())
		return (int) it->get<double>();
	else if (it->is_string())
		return atoi(it->get<std::string>().c_str());
	else
		return defaultValue;
}

IMQS_PAL_API int64_t GetInt64(const nlohmann::json& v, const char* key, int64_t defaultValue) {
	auto it = v.find(key);
	if (it != v.end() && it->is_number_integer())
		return it->get<int64_t>();
	return defaultValue;
}

IMQS_PAL_API double GetDouble(const nlohmann::json& v, const char* key, double defaultValue) {
	auto it = v.find(key);
	if (it != v.end() && it->is_number())
		return it->get<double>();
	return defaultValue;
}

IMQS_PAL_API std::vector<std::string> GetStringList(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	if (it != v.end() && it->is_array()) {
		std::vector<std::string> res;
		for (size_t i = 0; i < it->size(); i++) {
			const auto& item = it->at(i);
			if (item.is_string())
				res.push_back(item.get<std::string>());
		}
		return res;
	}
	return {};
}

IMQS_PAL_API const nlohmann::json& GetArray(const nlohmann::json& v, const char* key) {
	const auto& el = v.find(key);
	if (el != v.end() && el->is_array())
		return *el;
	return NullValue;
}

IMQS_PAL_API const nlohmann::json& GetObject(const nlohmann::json& v, const char* key) {
	const auto& el = v.find(key);
	if (el != v.end() && el->is_object())
		return *el;
	return NullValue;
}

IMQS_PAL_API bool IsArray(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	return it != v.end() && it->is_array();
}

IMQS_PAL_API bool IsObject(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	return it != v.end() && it->is_object();
}

IMQS_PAL_API bool IsString(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	return it != v.end() && it->is_string();
}

IMQS_PAL_API bool IsNumber(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	return it != v.end() && it->is_number();
}

IMQS_PAL_API bool IsBool(const nlohmann::json& v, const char* key) {
	auto it = v.find(key);
	return it != v.end() && it->is_boolean();
}

} // namespace nj
} // namespace imqs
