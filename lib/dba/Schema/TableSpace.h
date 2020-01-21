#pragma once

namespace imqs {
namespace dba {
namespace schema {

// A table space inside a DB schema
class IMQS_DBA_API TableSpace {
public:
	friend class DB;

	const std::string& GetName() const { return Name; }
	bool               SetName(const std::string& name); // Returns false if a table space with this name already exists in the owning DB

private:
	DB*         Owner = nullptr;
	std::string Name;
};
} // namespace schema
} // namespace dba
} // namespace imqs
