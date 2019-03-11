/* You may do whatever you want with this code.
The author assumes no responsibility for it.
Ben Harper */
#ifndef WKG_H_INCLUDED
#define WKG_H_INCLUDED 1

/*
WKG = Well Known Geometry
This library handles serialization of both Well Known Text and Well Known Geometry (well not the WKT yet...)
*/

#ifndef WKG_API
#define WKG_API
#endif

/* MultiPolygon means more than 1 exterior ring.
   MultiLineString means simply more than 1 ring. */

/* Well-Known-Geometry (WKT and WKB) */
enum WKG_Types {
	WKG_Point = 1,
	WKG_LineString,
	WKG_Polygon,
	WKG_MultiPoint,
	WKG_MultiLineString,
	WKG_MultiPolygon,
};

/*
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binary
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
*/

enum WKG_WriterFlags {
	WKG_Write_Z            = 0x1,
	WKG_Write_M            = 0x2,
	WKG_Write_EWKB         = 0x4,  /* WKB only */
	WKG_Write_SRID         = 0x8,  /* WKB only ? */
	WKG_Write_Force_Multi  = 0x10, /* Easy the pain in adhering to strict geometry type fields. Workaround is to make them always MULTIPOLYGON or MULTILINESTRING. Does not affect POINT. */
	WKG_Write_BigEndian    = 0x20, /* WKB only */
	WKG_Write_LittleEndian = 0x40, /* WKB only (absence of any of these flags means we write in the system native endianness) */
};

enum WKG_RingDepth {
	WKG_RingOuter = 1,
	WKG_RingInner = 2,
};

enum WKG_BinParseResult {
	WKG_BinParse_OK,
	WKG_BinParse_Stop,    /* Abort parsing, as specified by the Vertex callback function */
	WKG_BinParse_Overrun, /* Buffer overrun */
	WKG_BinParse_InvalidInput,
};

enum WKG_BinParseFlags {
	/* Only send enough information to be able to know how many parts and vertices
	there are in this geometry.
	This means that the coordinates sent to the vertex callback function will be meaningless,
	except for the first and last vertex in a ring. The first and last vertices are needed
	to 
	*/
	WKG_BinParseFlag_CountOnly = 0x1,
};

/* Source of a WKG object */
typedef struct _WKG_Reader {
	void* Context;

	/*
	type:			WKG_Types
	numparts:		For WKG_Point: 1. For WKG_MultiPoint: 1. For WKG_LineString: 1. For everything else - total number of rings.
	vxstring:		Vertex stride in doubles. So if your internal vertices are x,y,z, then return 3 for stride.
	moffset:		Offset in doubles, from the 'x' coordinate, to the 'm' coordinate. 0 means your data format does not store 'm' coordinates.
	zoffset:		Offset in doubles, from the 'x' coordinate, to the 'z' coordinate. 0 means your data format does not store 'z' coordinates.
	srid:			Spatial reference id.
	*/
	void (*Info)(void* context, int* type, int* numparts, uint32_t* vxstride, ptrdiff_t* moffset, ptrdiff_t* zoffset, int* srid);

	/*
	Note that the library assumes that your parts are ordered as such:

	Exterior Ring 1					x 1
		Interiors to Ring 1			x N
	Exterior Ring 2					x 1
		Interiors to Ring 2			x N
	etc

	vertex:			Array of packed vertices. Do not repeat the last vertex for closed parts.
	part:			Part number, starting at 0.
	count:			Number of vertices
	depth:			(for multipolygon only) 1 = Outer ring, (depth > 1) = island (use WKG_RingDepth)
	closed:			1 = closed. 0 = open.
	*/
	void (*GetPart)(void* context, int part, double** vertices, int* count, int* depth, int* closed);
} WKG_Reader;

/*
Target of a WKB readout.
*/
typedef struct _WKG_Target {
	/* Input */
	void* Context;

	/* Called for every vertex.

	If WKG_BinParseFlag_CountOnly is toggled, then this function is not called.

	The high 16 bits of part_and_ring is the part number, and the low 16 bits is the ring number.
	For a multipolygon, ring 0 is always the outer ring, and ring 1...N are islands within that outer ring.

	The vertex data is inside VX, VY, VZ, VM

	Return WKG_BinParse_OK or WKG_BinParse_Stop
	*/
	int (*Vertex)(_WKG_Target* target, int part, int ring, int vertex, int nvertex);

	/* Invoked when WKG_BinParseFlag_CountOnly is toggled.
	Called for every collection of vertices. For linestring, multilinestring, polygon, multipolygon, called once per ring. Called once for multipoint */
	void (*Count)(_WKG_Target* target, int part, int ring, int nvertex, int isClosed);

	/* Vertices are always placed here */
	double VX;
	double VY;
	double VZ;
	double VM;

	/* The following fields are populated before the first callback to Vertex */
	int     Type; /* WKG_Types */
	int     SRID;
	uint8_t HasZ;
	uint8_t HasM;
} WKG_Target;

#ifdef __cplusplus
extern "C" {
#endif

/*=============================== WRITER =================================================*/

/* Computes the exact number of bytes that this object will need to be represented in WKB format. */
WKG_API size_t WKG_BinBytes(WKG_Reader* val, uint32_t write_flags);

/* Writes this object to WKB. Assumes buf is large enough. Returns bytes written. */
WKG_API size_t WKG_BinWrite(WKG_Reader* val, uint32_t write_flags, void* buf);

/*=============================== PARSER =================================================*/

/* Parse a WKB. Returns WKG_BinParseResult. */
WKG_API int WKG_BinParse(const void* buf, size_t bytes, uint32_t parse_flags, WKG_Target* target);

#ifdef __cplusplus
}
#endif

#endif