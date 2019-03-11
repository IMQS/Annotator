#pragma once

/*
libproj and GDAL wrapper
========================

Provides the following on top of libproj:

* Embedded EPSG codes
* Thread-local cache for converting between coordinate systems

Custom SRIDs (no EPSG code) are a problem. 
The way we deal with them, is by using negative numbers to represent them.
We maintain an in-memory table from negative srid to proj definition string.

GDAL was not originally a dependency, but we added it in when we needed
to parse WKT coordinate systems for the .prj file that accompanies a shapefile.

Tests
=====

Tests of projwrap are inside MapServer

*/

#if !defined(PROJWRAP_API)
#if defined(_MSC_VER)
#define PROJWRAP_API __declspec(dllimport)
#else
#define PROJWRAP_API
#endif
#endif

#include "Proj.h"
#include "epsg.h"
