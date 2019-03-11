#pragma once

namespace imqs {
namespace dba {

class Attrib;

namespace WKB {

/*
Well Known Binary I/O.

We do not support WKB for vertices stored as floats. The idea behind geometry stored as floats, is that
it's an intermediate storage format for a geometry processing pipeline, where the end result is for
display. WKB is a serialization format typically used for long-term storage, so it doesn't make sense
to be dealing with floats in that context.
*/

enum class WriterFlags : uint32_t {
	None         = 0,
	Z            = 0x1,
	M            = 0x2,
	EWKB         = 0x4,  // PostGIS extended well known binary
	SRID         = 0x8,  // Include SRID
	Force_Multi  = 0x10, // Ease the pain in adhering to strict geometry type fields. Workaround is to make them always MULTIPOLYGON or MULTILINESTRING. Does not affect POINT.
	BigEndian    = 0x20, // Absence of endian flags means we write in the system native endianness
	LittleEndian = 0x40,
};

inline WriterFlags  operator|(WriterFlags a, WriterFlags b) { return (WriterFlags)((uint32_t) a | (uint32_t) b); }
inline WriterFlags& operator|=(WriterFlags& a, WriterFlags b) {
	a = (WriterFlags)((uint32_t) a | (uint32_t) b);
	return a;
}
inline uint32_t operator&(WriterFlags a, WriterFlags b) { return (uint32_t) a & (uint32_t) b; }
// Compute the number of bytes of the encoded geometry value
IMQS_DBA_API size_t ComputeEncodedBytes(WriterFlags flags, const Attrib& val);

// Encode the geometry. Return the number of bytes written.
IMQS_DBA_API size_t Encode(WriterFlags flags, const Attrib& val, io::Buffer& buf);

// Decode geometry
IMQS_DBA_API Error Decode(const void* buf, size_t len, Attrib& val, Allocator* alloc = nullptr);
} // namespace WKB

namespace geom {
// Take as input parts and vx, and reorder the parts and their vertices, so that they are ordered
// in the way that Well Known Binary wants them. That is, each outer part is followed by
// the parts that are interior to it. Additionally, outer parts are counter-clockwise, and
// inner parts are clockwise.
// Example OuterA, innerA1, innerA2, OuterB, innerB1, innerB2, innerB3
// parts must have a sentinel (ie parts[numParts] is the sentinel)
// We make no attempt to recover gracefully from invalid data. Upon receiving invalid data, all parts and vertices
// will be preserved, but their resulting order is undefined.
IMQS_DBA_API void FixRingOrderWKB(bool isDoubles, int vxDims, size_t numParts, const uint32_t* parts, const void* vx, uint32_t* newParts, void* newVx);
} // namespace geom
} // namespace dba
} // namespace imqs
