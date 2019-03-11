#include <lib/pal/pal.h>
#include <proj_api.h>

#ifndef IMQS_PROJWRAP_EXCLUDE_GDAL
// GDAL
#include <ogr_spatialref.h>
#include <cpl_string.h>
#endif

#if defined(_MSC_VER)
#define PROJWRAP_API __declspec(dllexport)
#else
#define PROJWRAP_API
#endif
