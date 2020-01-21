#include <lib/pal/pal.h>
#include <proj_api.h>

// GDAL
#ifndef IMQS_PROJWRAP_EXCLUDE_GDAL

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wignored-attributes"
#endif

#include <ogr_spatialref.h>
#include <cpl_string.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif

#if defined(_MSC_VER)
#define PROJWRAP_API __declspec(dllexport)
#else
#define PROJWRAP_API
#endif
