#pragma once

#include "../Types.h"

namespace imqs {
namespace dba {
namespace schema {

// A field inside a DB schema
// There are a bunch of fields in here that would be better placed at higher level, such as UIGroup.
// In the interest of keeping it simple though, we store them here until it becomes a problem.
class IMQS_DBA_API Field {
public:
	dba::Type               Type = dba::Type::Null;
	std::string             Name;
	std::string             FriendlyName;
	TypeFlags               Flags = TypeFlags::None;
	ohash::set<std::string> Tags;
	std::string             Unit;
	std::string             UIGroup;
	int                     SRID     = 0;
	int                     Width    = 0;
	int                     UIOrder  = 0;
	int                     UIDigits = 0;

	Field();
	Field(const char* name, dba::Type type, int width_or_srid = 0, TypeFlags flags = TypeFlags::None);
	~Field();

	Error       ParseType(const char* ftype, size_t len = -1);
	std::string TypeToString() const;
	void        ToJson(nlohmann::json& j) const;

	void Set(const char* name, dba::Type type);
	void Set(const char* name, dba::Type type, int width_or_srid);
	void Set(const char* name, dba::Type type, int width_or_srid, TypeFlags flags);

	std::vector<std::string> TagArray() const;

	bool operator==(const Field& b) const;
	bool operator!=(const Field& b) const;
	bool NotNull() const { return !!(Flags & TypeFlags::NotNull); }
	bool AutoIncrement() const { return !!(Flags & TypeFlags::AutoIncrement); }
	bool GeomHasZ() const { return !!(Flags & TypeFlags::GeomHasZ); }
	bool GeomHasM() const { return !!(Flags & TypeFlags::GeomHasM); }
	bool IsTypeGeom() const { return dba::IsTypeGeom(Type); }
	int  WidthOrSRID() const { return IsTypeGeom() ? SRID : Width; }
};
} // namespace schema
} // namespace dba
} // namespace imqs
