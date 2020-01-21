#pragma once

namespace imqs {
namespace dba {
class FlatFile;
class Executor;
class Tx;
namespace etl {

// Parameters to the table Copy function
struct CopyParams {
	ohash::map<std::string, std::string> FieldMap;                 // If a field is missing from fieldMap, then we assume that the field name is the same in src and dst
	ohash::map<std::string, Type>        FieldTypes;               // Force casting data to the given type, before inserting. Useful for loosely typed source data, such as sqlite databases (ie UUID = blob)
	int                                  OverrideNullSRID = 0;     // If non-zero, then override null SRIDs with this value
	size_t                               ReadChunkSize    = 10000; // Number of records in each read chunk, when reading from an SQL database
};

// Copies all records from src into dst.
IMQS_DBA_API Error Copy(FlatFile* src, const CopyParams& params, Executor* dstExec, std::string dstTable);

// Copies all records from one table into another.
// Only fields inside params.FieldMap are copied.
IMQS_DBA_API Error CopyTable(Executor* srcExec, std::string srcTable, const CopyParams& params, Executor* dstExec, std::string dstTable);

enum class CreateTableFromFlatFileFlags {
	None                     = 0,
	AddRowId                 = 1, // Add an INT64 AUTOINCREMENT PRIMARY KEY field
	AddSpatialIndex          = 2, // Add a spatial index to the first geometry field found
	AssumeLatLonDegreesWGS84 = 4, // If source data has no SRID, assume it is WGS84 LatLon Degrees (EPSG code 4326)
};

inline uint32_t                      operator&(CreateTableFromFlatFileFlags a, CreateTableFromFlatFileFlags b) { return (uint32_t) a & (uint32_t) b; }
inline CreateTableFromFlatFileFlags  operator|(CreateTableFromFlatFileFlags a, CreateTableFromFlatFileFlags b) { return (CreateTableFromFlatFileFlags)((uint32_t) a | (uint32_t) b); }
inline CreateTableFromFlatFileFlags& operator|=(CreateTableFromFlatFileFlags& a, CreateTableFromFlatFileFlags b) { return (a = a | b); }

// Creates a table inside an SQL database which is a copy of the schema in the flatfile 'src'
// tx may be null
IMQS_DBA_API Error CreateTableFromFlatFile(FlatFile* src, Conn* dst, Tx* tx, std::string dstTable, CreateTableFromFlatFileFlags flags);
} // namespace etl
} // namespace dba
} // namespace imqs
