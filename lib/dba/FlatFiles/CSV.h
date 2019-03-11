#pragma once
#include "FlatFile.h"
#include "../Allocators.h"

namespace imqs {
namespace dba {

/* CSV reader

After calling Open(), you can manually alter the field types inside CachedFields. Read() will try and
convert the CSV text cell into whatever type is specified inside CachedFields. Geometry can be
read from WKT.
*/
class IMQS_DBA_API CSV : public FlatFile {
public:
	os::MMapFile               File;
	std::vector<schema::Field> CachedFields;
	std::string                Buf;
	std::vector<size_t>        BufCells;
	int64_t                    LastRecord = -2; // -1 is not sufficient, because then when reading record zero, it looks like we're predicting correctly
	csv::Decoder               Decoder;
	std::vector<int64_t>       RecordStarts;
	bool                       ReadFieldTypesOnOpen = true; // If true, then figure out field types by scanning the entire file during Open()

	Error                      Open(const std::string& filename, bool create) override;
	int64_t                    RecordCount() override;
	std::vector<schema::Field> Fields() override;
	Error                      Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) override;

private:
	OnceOffAllocator TmpAlloc;

	Error ReadFields();
	Error ReadRecordStarts();
};

} // namespace dba
} // namespace imqs