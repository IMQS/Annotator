#include "pch.h"
#include "Field.h"

using namespace std;

namespace imqs {
namespace dba {
namespace schema {

Field::Field() {
}

Field::Field(const char* name, dba::Type type, int width_or_srid, TypeFlags flags) {
	Name = name;
	Type = type;
	if (IsTypeGeom())
		SRID = width_or_srid;
	else
		Width = width_or_srid;
	Flags = flags;
}

Field::~Field() {
}

// Examples:
// INT32
// int32
// text(20)
// text
// polyline
// polylinez(4326)
// polylinezm
Error Field::ParseType(const char* ftype, size_t len) {
	char lowcase[30];
	if (len == -1)
		len = strlen(ftype);
	if (len < 3 || len >= sizeof(lowcase) - 1)
		return Error::Fmt("Invalid field type '%v'", ftype);

	modp_tolower_copy(lowcase, ftype, len);

	int widthOrSRID = 0;
	if (lowcase[len - 1] == ')') {
		// extract width
		const char* open = strchr(lowcase, '(');
		if (open == nullptr)
			return Error::Fmt("Invalid field type '%v' (unmatched parentheses)", ftype);
		widthOrSRID  = atoi(open + 1);
		len          = open - lowcase;
		lowcase[len] = 0;
	}

	if ((lowcase[len - 2] == 'm' && lowcase[len - 1] == 'z') || (lowcase[len - 2] == 'z' && lowcase[len - 1] == 'm')) {
		Flags |= TypeFlags::GeomHasM | TypeFlags::GeomHasZ;
		lowcase[len - 2] = 0;
	} else if (lowcase[len - 1] == 'z') {
		Flags |= TypeFlags::GeomHasZ;
		lowcase[len - 1] = 0;
	} else if (lowcase[len - 1] == 'm') {
		Flags |= TypeFlags::GeomHasM;
		lowcase[len - 1] = 0;
	}

	// clang-format off
	// SYNC-FIELD_NAME_TABLE
	switch (hash::crc32(lowcase)) {
	case "null"_crc32: Type = Type::Null; break;
	case "bool"_crc32: Type = Type::Bool; break;
	case "int16"_crc32: Type = Type::Int16; break;
	case "int32"_crc32: Type = Type::Int32; break;
	case "int64"_crc32: Type = Type::Int64; break;
	case "float"_crc32: Type = Type::Float;  break;
	case "double"_crc32: Type = Type::Double; break;
	case "text"_crc32: Type = Type::Text; break;
	case "date"_crc32: Type = Type::Date; break;
	case "datetime"_crc32: Type = Type::Date; break;
	case "guid"_crc32: Type = Type::Guid; break;
	case "time"_crc32: Type = Type::Time; break;
	case "bin"_crc32: Type = Type::Bin; break;
	case "point"_crc32: Type = Type::GeomPoint; break;
	case "multipoint"_crc32: Type = Type::GeomMultiPoint; break;
	case "polyline"_crc32: Type = Type::GeomPolyline; break;
	case "polygon"_crc32: Type = Type::GeomPolygon; break;
	case "geometry"_crc32: Type = Type::GeomAny; break;
	default:
		return Error::Fmt("Invalid field type '%v'", ftype);
	}
	// clang-format on

	if (widthOrSRID != 0) {
		switch (Type) {
		case Type::GeomPoint:
		case Type::GeomMultiPoint:
		case Type::GeomPolyline:
		case Type::GeomPolygon:
		case Type::GeomAny:
			SRID = widthOrSRID;
			break;
		default:
			Width = widthOrSRID;
			break;
		}
	}
	return Error();
}

std::string Field::TypeToString() const {
	std::string t = FieldTypeToString(Type, Flags);

	if (IsTypeGeom() && GeomHasZ())
		t += "z";
	if (IsTypeGeom() && GeomHasM())
		t += "m";

	if (IsTypeGeom() && SRID != 0)
		t += tsf::fmt("(%d)", SRID);
	else if (!IsTypeGeom() && Width != 0)
		t += tsf::fmt("(%d)", Width);

	return t;
}

// This is built to match SchemaArk::DecodeJsonField()
void Field::ToJson(nlohmann::json& j) const {
	if (FriendlyName != "")
		j["Alias"] = FriendlyName;

	if (UIOrder != 0)
		j["UIOrder"] = UIOrder;

	if (Tags.size() != 0) {
		vector<string> tagArray;
		for (const auto& it : Tags)
			tagArray.push_back(it);
		j["Tags"] = tagArray;
	}

	if (Unit != "" || UIDigits != 0) {
		j["NumberFormat"] = {
		    {"Digits", UIDigits},
		    {"Unit", Unit},
		};
	}
}

void Field::Set(const char* name, dba::Type type) {
	Name = name;
	Type = type;
}

void Field::Set(const char* name, dba::Type type, int width_or_srid) {
	Name = name;
	Type = type;
	if (IsTypeGeom())
		SRID = width_or_srid;
	else
		Width = width_or_srid;
}

void Field::Set(const char* name, dba::Type type, int width_or_srid, TypeFlags flags) {
	Name = name;
	Type = type;
	if (IsTypeGeom())
		SRID = width_or_srid;
	else
		Width = width_or_srid;
	Flags = flags;
}

std::vector<std::string> Field::TagArray() const {
	std::vector<std::string> a;
	for (const auto& t : Tags)
		a.push_back(t);
	return a;
}

bool Field::operator==(const Field& b) const {
	return Type == b.Type &&
	       Name == b.Name &&
	       FriendlyName == b.FriendlyName &&
	       Flags == b.Flags &&
	       Tags == b.Tags &&
	       Unit == b.Unit &&
	       UIGroup == b.UIGroup &&
	       SRID == b.SRID &&
	       Width == b.Width &&
	       UIOrder == b.UIOrder &&
	       UIDigits == b.UIDigits;
}

bool Field::operator!=(const Field& b) const {
	return !(*this == b);
}
} // namespace schema
} // namespace dba
} // namespace imqs
