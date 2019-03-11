/* You may do whatever you want with this code.
The author assumes no responsibility for it.
Ben Harper */

#ifndef FALSE
#define FALSE (0)
#endif
#ifndef TRUE
#define TRUE (1)
#endif

#ifndef _WIN32
typedef uint32_t BOOL;
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WKB
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef ASSERT
#define ASSERT assert
#define PANIC ASSERT(FALSE)
#endif

enum WKBByteOrder {
	wkbXDR = 0, // Big Endian
	wkbNDR = 1  // Little Endian
};

enum WKBGeometryType {
	wkbOffsetZ            = 1000,
	wkbOffsetM            = 2000,
	wkbOffsetZM           = 3000,
	wkbPoint              = 1,
	wkbLineString         = 2,
	wkbPolygon            = 3,
	wkbMultiPoint         = 4,
	wkbMultiLineString    = 5,
	wkbMultiPolygon       = 6,
	wkbGeometryCollection = 7,
	wkbPolyhedralSurface  = 15,
	wkbTIN                = 16,
	wkbTriangle           = 17,
};

enum EWKBFlags {
	EWKBFlag_Z    = 0x80000000,
	EWKBFlag_M    = 0x40000000,
	EWKBFlag_SRID = 0x20000000,
};

enum Orientation {
	Orient_Null,
	Orient_CW,
	Orient_CCW,
};

static const uint32_t WKG_2_WKB[7] =
    {
        0,
        wkbPoint,
        wkbLineString,
        wkbPolygon,
        wkbMultiPoint,
        wkbMultiLineString,
        wkbMultiPolygon,
};

static const uint32_t WKB_2_WKG[7] =
    {
        0,
        WKG_Point,
        WKG_LineString,
        WKG_Polygon,
        WKG_MultiPoint,
        WKG_MultiLineString,
        WKG_MultiPolygon,
};
