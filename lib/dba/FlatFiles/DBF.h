#pragma once
#include "../Attrib.h"
#include "../mem.h"
#include "FlatFile.h"
#include "DBF/XBaseDB.h"

namespace imqs {
namespace dba {

class DBF : public FlatFile {
public:
	xbase::DB                  DB;
	std::vector<schema::Field> FieldsCache;
	std::vector<int>           FieldsCacheIndex;

	Error                      Open(const std::string& filename, bool create) override;
	int64_t                    RecordCount() override;
	std::vector<schema::Field> Fields() override;
	Error                      Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) override;
};

} // namespace dba
} // namespace imqs