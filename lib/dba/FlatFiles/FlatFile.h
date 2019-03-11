#pragma once

#include "../Schema/Field.h"

namespace imqs {
namespace dba {

class Attrib;
class Allocator;

// Base of a flat file, which is a file format that exposes database records,
// but it doesn't have any query functionality. Examples include Shapefiles and CSV.
// To open a flat file, use Glob.OpenFlatFile().
class IMQS_DBA_API FlatFile {
public:
	virtual ~FlatFile() {}
	virtual Error                      Open(const std::string& filename, bool create)                    = 0;
	virtual int64_t                    RecordCount()                                                     = 0;
	virtual std::vector<schema::Field> Fields()                                                          = 0;
	virtual Error                      Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) = 0;
};

// A block-scoped auto FlatFile closer.
class IMQS_DBA_API FlatFileAutoCloser {
public:
	FlatFileAutoCloser(FlatFile* ff) : FF(ff) {}
	~FlatFileAutoCloser() { delete FF; }

private:
	FlatFile* FF;
};

} // namespace dba
} // namespace imqs
