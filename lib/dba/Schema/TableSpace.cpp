#include "pch.h"
#include "DB.h"
#include "SchemaParser.h"

using namespace std;
using namespace tsf;

namespace imqs {
namespace dba {
namespace schema {

bool TableSpace::SetName(const std::string& name) {
	if (Owner && Owner->TableSpaces.contains(name))
		return false;
	if (Owner)
		Owner->TableSpaces.erase(Name);
	Name = name;
	if (Owner)
		Owner->TableSpaces.insert(Name, this);
	return true;
}

} // namespace schema
} // namespace dba
} // namespace imqs
