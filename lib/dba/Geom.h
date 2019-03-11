#pragma once

// Various geometry-related functions

#include "Attrib.h"

namespace imqs {
namespace dba {
namespace geom {

const int SRID_WGS84LatLonDegrees = 4326;

IMQS_DBA_API void EncodeWKB(const Attrib& src, io::Buffer& dst);
IMQS_DBA_API geom2d::BBox2d BBox2d(const Attrib& geom);

IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec2d>& v);
IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec3d>& v);
IMQS_DBA_API void GetPolyVertices(const Attrib& geom, size_t part, std::vector<gfx::Vec4d>& v);

IMQS_DBA_API double Distance2D(const Attrib& g1, const Attrib& g2);

} // namespace geom
} // namespace dba
} // namespace imqs
