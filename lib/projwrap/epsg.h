#pragma once

namespace imqs {
namespace projwrap {

// Returns the libproj string (eg "+proj=utm +zone=15 ...") for a well known EPSG code.
// This uses an EPSG table embedded inside the code.
// Returns null if the SRID is unknown
PROJWRAP_API const char* FindEPSG(int srid);

PROJWRAP_API size_t EPSG_Size();
PROJWRAP_API void   EPSG_Get(size_t i, int& srid, const char*& proj4);

} // namespace projwrap
} // namespace imqs
