#pragma once

namespace imqs {
namespace projwrap {

PROJWRAP_API void Initialize();
PROJWRAP_API void Shutdown();

enum ConvertFlags {
	ConvertFlagCheckNaN = 1, // After converting, check every value to see if it's NaN, and return false if any NaNs are found.
};

// stride is the number of doubles between one 'x' and the next 'x'. Likewise for y and z.
// For example, if x values are tightly packed, the stride is 1.
// On the other hand, if you have pairs of xy,xy,xy, then stride is 2.
PROJWRAP_API bool Convert(int srcSrid, int dstSrid, size_t nvertex, size_t stride, double* x, double* y, double* z, uint32_t convertFlags = 0);

// Register a custom coordinate system.
// It's safe to call this multiple times with the same coordinate system - the same SRID will be returned every time.
// 'proj' is a proj4 initialization string, such as +proj=tmerc +ellps=WGS84, etc.
// Ideally, you should stick to using the EPSG codes.
PROJWRAP_API int RegisterCustomSRID(const char* proj);

// Return the proj4 string for the given srid (can be EPSG or custom), or null if not found.
PROJWRAP_API const char* SRIDLookup(int srid);

// Checks if the proj string is an exact match of an EPSG code, and if so returns it.
// If it is not an exact match of an EPSG code, then a custom SRID is created using
// RegisterCustomSRID, and that custom SRID is returned.
PROJWRAP_API int Proj2SRID(const char* proj);

// Use proj4 to parse a WKT coordinate system (eg the .prj file that accompanies a shapefile), and register it with our SRID table
// proj4 and/or srid may be null
PROJWRAP_API Error ParseWKT(const char* wkt, std::string* proj4, int* srid);

} // namespace projwrap
} // namespace imqs
