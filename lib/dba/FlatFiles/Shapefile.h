#pragma once
#include "FlatFile.h"
#include "Shapefile/ShFile.h"
#include "DBF.h"

namespace imqs {
namespace dba {

// A shapefile is a combination of .shp, .shx, .dbf files.
// The shp and shx are managed by the files in the "Shapefile" directory.
// The dbf is managed by the files in the "DBF" directory.
class Shapefile : public FlatFile {
public:
	shapefile::File            Shp;
	dba::DBF                   DBF;
	std::vector<schema::Field> FieldsCache;
	dba::GeomFlags             GeomFlags = dba::GeomFlags::None;
	int                        GeomDims  = 0;
	int                        SRID      = 0;
	bool                       HasM      = false;
	bool                       HasZ      = false;
	std::vector<gfx::Vec3d>    TempVx;
	std::vector<double>        TempM;
	std::vector<double>        TempDbl;
	std::vector<uint8_t>       TempClosed;
	std::vector<uint32_t>      TempParts;

	Error                      Open(const std::string& filename, bool create) override;
	int64_t                    RecordCount() override;
	std::vector<schema::Field> Fields() override;
	Error                      Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) override;
};

} // namespace dba
} // namespace imqs