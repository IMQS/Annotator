#include "pch.h"
/* You may do whatever you want with this code.
The author assumes no responsibility for it.
Ben Harper */
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "WKG.h"
#include "WKG_internal.h"

#define WKG_WRITER 1
#ifdef WKG_WRITER

/* Checks whether a polygon is CCW or CW. */
int PolygonOrient(int n, const double* pgon, int stride) {
	int    i, vleft, vright, rightlow;
	double area;
	if (n < 3)
		return Orient_Null;

#define X(i) pgon[(i) *stride + 0]
#define Y(i) pgon[(i) *stride + 1]

	/* Use method described in comp.graphics.algorithms faq 2.07 */
	rightlow = 0;
	for (i = 1; i < n; i++) {
		if ((Y(i) < Y(rightlow)) ||
		    (Y(i) == Y(rightlow) && X(i) > X(rightlow))) {
			rightlow = i;
		}
	}

	vleft  = (rightlow + n - 1) % n;
	vright = (rightlow + 1) % n;

	/*
	a = pgon[ vleft ];
	b = pgon[ rightlow ];
	c = pgon[ vright ];

	We translate everything so that a.x, a.y is the origin. This helps with precision when the coordinates are large.

	Natural:
	double area = a.x * b.y - a.y * b.x   +   a.y * c.x - a.x * c.y   +   b.x * c.y - c.x * b.y;

	Shifted: (a.x and a.y become zero)
	*/
	area = (X(rightlow) - X(vleft)) * (Y(vright) - Y(vleft)) - (X(vright) - X(vleft)) * (Y(rightlow) - Y(vleft));

	if (area > 0)
		return Orient_CCW;
	else if (area < 0)
		return Orient_CW;
	else
		return Orient_Null;

#undef X
#undef Y
}

typedef struct _WKBWriter {
	uint8_t* Buf;
	uint32_t Flags;

	uint8_t HasZ;        /* Setup by WKB_Initialize */
	uint8_t HasM;        /* Setup by WKB_Initialize */
	uint8_t IsBigEndian; /* Setup by WKB_Initialize */
	uint8_t Pad1;

	ptrdiff_t MOff;   /* Working state, retrieved by IWKT_WKB_Geom_Reader.Info()  */
	ptrdiff_t ZOff;   /* Working state, retrieved by IWKT_WKB_Geom_Reader.Info()  */
	uint32_t  Stride; /* Working state, retrieved by IWKT_WKB_Geom_Reader.Info()  */
} WKBWriter;

static void WriteUInt32LE(uint32_t v, void* dst) {
	uint8_t bytes[4] = {
	    uint8_t(v & 0xff),
	    uint8_t((v >> 8) & 0xff),
	    uint8_t((v >> 16) & 0xff),
	    uint8_t((v >> 24) & 0xff),
	};
	memcpy(dst, bytes, sizeof(bytes));
}

static void WriteUInt32BE(uint32_t v, void* dst) {
	uint8_t bytes[4] = {
	    uint8_t((v >> 24) & 0xff),
	    uint8_t((v >> 16) & 0xff),
	    uint8_t((v >> 8) & 0xff),
	    uint8_t(v & 0xff),
	};
	memcpy(dst, bytes, sizeof(bytes));
}

static void WriteUInt64LE(uint64_t v, void* dst) {
	uint8_t bytes[8] = {
	    uint8_t(v & 0xff),
	    uint8_t((v >> 8) & 0xff),
	    uint8_t((v >> 16) & 0xff),
	    uint8_t((v >> 24) & 0xff),
	    uint8_t((v >> 32) & 0xff),
	    uint8_t((v >> 40) & 0xff),
	    uint8_t((v >> 48) & 0xff),
	    uint8_t((v >> 56) & 0xff),
	};
	memcpy(dst, bytes, sizeof(bytes));
}

static void WriteUInt64BE(uint64_t v, void* dst) {
	uint8_t bytes[8] = {
	    uint8_t((v >> 56) & 0xff),
	    uint8_t((v >> 48) & 0xff),
	    uint8_t((v >> 40) & 0xff),
	    uint8_t((v >> 32) & 0xff),
	    uint8_t((v >> 24) & 0xff),
	    uint8_t((v >> 16) & 0xff),
	    uint8_t((v >> 8) & 0xff),
	    uint8_t(v & 0xff),
	};
	memcpy(dst, bytes, sizeof(bytes));
}

static void WriteU8(uint8_t** buf, uint8_t v) {
	**buf = v;
	*buf += 1;
}

static void WriteDouble(uint8_t** buf, int isBE, double v) {
	if (isBE)
		WriteUInt64BE(*((uint64_t*) &v), *buf);
	else
		WriteUInt64LE(*((uint64_t*) &v), *buf);
	*buf += sizeof(v);
}

static void WriteU32In(uint8_t* buf, int isBE, uint32_t v) {
	if (isBE)
		WriteUInt32BE(v, buf);
	else
		WriteUInt32LE(v, buf);
}

static void WriteU32(WKBWriter* w, uint32_t v) {
	WriteU32In(w->Buf, w->IsBigEndian, v);
	w->Buf += 4;
}

static void WKBWriter_Initialize(WKBWriter* w, uint8_t* buf, uint32_t flags) {
	w->Buf   = buf;
	w->Flags = flags;
	w->HasZ  = !!(flags & WKG_Write_Z);
	w->HasM  = !!(flags & WKG_Write_M);
	if ((flags & (WKG_Write_LittleEndian | WKG_Write_BigEndian)) == 0) {
		uint32_t test  = 0xff000000;
		char*    btest = (char*) &test;
		if (btest[0] == (char) 0xff)
			flags |= WKG_Write_BigEndian;
		else
			flags |= WKG_Write_LittleEndian;
	}

	// If the caller specifies both little and big endian, then we default to little endian
	if (!!(flags & WKG_Write_BigEndian))
		w->IsBigEndian = TRUE;
	if (!!(flags & WKG_Write_LittleEndian))
		w->IsBigEndian = FALSE;
}

WKG_API size_t WKG_BinBytes(WKG_Reader* val, uint32_t write_flags) {
	int       type, part, numparts, srid, vxcount, depth, closed, head, dims, pt;
	ptrdiff_t moffset, zoffset;
	uint32_t  vxstride;
	double*   vx;
	uint8_t   hasZ, hasM, isMulti;
	size_t    t; /* total */

	val->Info(val->Context, &type, &numparts, &vxstride, &moffset, &zoffset, &srid);

	head = 5; /* XDR/NDR(int8) + Type(int32) */
	hasZ = !!(write_flags & WKG_Write_Z);
	hasM = !!(write_flags & WKG_Write_M);
	dims = 2 + (hasZ ? 1 : 0) + (hasM ? 1 : 0);
	pt   = dims * 8;

	t = head;
	if (!!(write_flags & WKG_Write_SRID))
		t += 4;

	switch (type) {
	case WKG_Point: t += pt; break;
	case WKG_MultiPoint:
		val->GetPart(val->Context, 0, &vx, &vxcount, &depth, &closed);
		t += 4; /* numPoints */
		t += (head + pt) * vxcount;
		break;
	case WKG_LineString:
	case WKG_MultiLineString:
	case WKG_Polygon:
	case WKG_MultiPolygon: {
		/* MultiPolygon means more than 1 exterior ring.
			  MultiLineString means simply more than 1 ring. */
		isMulti = type == WKG_MultiLineString || type == WKG_MultiPolygon || !!(write_flags & WKG_Write_Force_Multi);
		if (type == WKG_Polygon || type == WKG_MultiPolygon) {
			if (isMulti)
				t += 4; /* num external */
			for (part = 0; part < numparts; part++) {
				val->GetPart(val->Context, part, &vx, &vxcount, &depth, &closed);
				if (closed)
					vxcount++;
				if (depth == WKG_RingOuter) {
					if (isMulti)
						t += head;
					t += 4; /*num rings in part */
				}
				t += 4;            /* num verts in ring */
				t += vxcount * pt; /* verts */
			}
		} else {
			if (isMulti)
				t += 4; /* num rings */
			for (part = 0; part < numparts; part++) {
				val->GetPart(val->Context, part, &vx, &vxcount, &depth, &closed);
				if (closed)
					vxcount++;
				if (isMulti)
					t += head;
				t += 4;            /* num verts */
				t += vxcount * pt; /* verts */
			}
		}
	} break;
	default:
		PANIC;
	}

	return t;
}

static void WriteVx(WKBWriter* w, const double* p) {
	WriteDouble(&w->Buf, w->IsBigEndian, p[0]);
	WriteDouble(&w->Buf, w->IsBigEndian, p[1]);
	if (w->HasZ && w->ZOff)
		WriteDouble(&w->Buf, w->IsBigEndian, p[w->ZOff]);
	if (w->HasM && w->MOff)
		WriteDouble(&w->Buf, w->IsBigEndian, p[w->MOff]);
}

static void Write(WKBWriter* w, uint32_t n, const double* p, uint8_t reverse) {
	uint32_t i;
	if (reverse)
		for (i = 0; i < n; i++)
			WriteVx(w, &p[(n - i - 1) * w->Stride]);
	else
		for (i = 0; i < n; i++)
			WriteVx(w, &p[i * w->Stride]);
}

static void WriteRing(WKBWriter* w, uint32_t n, const double* p, uint8_t closed, uint8_t reverse) {
	WriteU32(w, closed ? n + 1 : n);
	Write(w, n, p, reverse);
	if (closed)
		WriteVx(w, reverse ? &p[(n - 1) * w->Stride] : p);
}

static void WriteHead(WKBWriter* w, uint32_t type, int srid) {
	uint8_t hasSrid = !!(w->Flags & WKG_Write_SRID) && srid != 0;
	uint8_t isEWKB  = !!(w->Flags & WKG_Write_EWKB);

	if (isEWKB) {
		if (w->HasZ)
			type |= EWKBFlag_Z;
		if (w->HasM)
			type |= EWKBFlag_M;
		if (hasSrid && srid != 0)
			type |= EWKBFlag_SRID;
	} else {
		ASSERT(!hasSrid);
		if (w->HasZ && w->HasM)
			type += wkbOffsetZM;
		else if (w->HasZ)
			type += wkbOffsetZ;
		else if (w->HasM)
			type += wkbOffsetM;
	}
	WriteU8(&w->Buf, w->IsBigEndian ? wkbXDR : wkbNDR);
	WriteU32(w, type);
	if (isEWKB && hasSrid)
		WriteU32(w, srid);
}

WKG_API size_t WKG_BinWrite(WKG_Reader* val, uint32_t write_flags, void* buf) {
	int      type, part, numparts, srid, reverse;
	uint32_t wkb_type;
	uint8_t* loc_outer_ring      = NULL;
	uint8_t* loc_num_outer_rings = NULL;
	uint32_t part_outer_ring     = 0;
	uint32_t total_outer_rings   = 0;
	int      orient;

	WKBWriter w;
	WKBWriter_Initialize(&w, (uint8_t*) buf, write_flags);

	val->Info(val->Context, &type, &numparts, &w.Stride, &w.MOff, &w.ZOff, &srid);

	if (type == WKG_LineString && !!(write_flags & WKG_Write_Force_Multi))
		type = WKG_MultiLineString;
	if (type == WKG_Polygon && !!(write_flags & WKG_Write_Force_Multi))
		type = WKG_MultiPolygon;

	wkb_type = WKG_2_WKB[type];

	WriteHead(&w, wkb_type, srid);

	switch (type) {
	case WKG_Point: break;
	case WKG_LineString: break;
	case WKG_Polygon: break;
	case WKG_MultiPoint:
		break; /* We write our number of points later */
	case WKG_MultiLineString: WriteU32(&w, numparts); break;
	case WKG_MultiPolygon:
		loc_num_outer_rings = w.Buf;
		WriteU32(&w, 0);
		break; /* we write this at the end */
	default: PANIC;
	}

	/* WKB requires outer rings to be counter clockwise, which is why we need to 
	   read the orientation for polygon parts */

	for (part = 0; part < numparts; part++) {
		double* vx;
		int     i, count, depth, closed;
		val->GetPart(val->Context, part, &vx, &count, &depth, &closed);
		switch (type) {
		case WKG_Point:
			WriteVx(&w, vx);
			break;
		case WKG_LineString:
			WriteRing(&w, count, vx, closed, FALSE);
			break;
		case WKG_MultiPoint:
			WriteU32(&w, count);
			for (i = 0; i < count; i++) {
				WriteHead(&w, wkbPoint, 0);
				WriteVx(&w, vx + w.Stride * i);
			}
			break;
		case WKG_MultiLineString:
			WriteHead(&w, wkbLineString, 0);
			WriteRing(&w, count, vx, closed, FALSE);
			break;
		case WKG_Polygon:
		case WKG_MultiPolygon:
			orient  = PolygonOrient(count, vx, w.Stride);
			reverse = (depth == WKG_RingOuter) != (orient == Orient_CCW);
			if (depth == WKG_RingOuter) {
				/* Rewind and write the number of rings in this part */
				if (loc_outer_ring)
					WriteU32In(loc_outer_ring, w.IsBigEndian, part - part_outer_ring);
				if (type == WKG_MultiPolygon)
					WriteHead(&w, wkbPolygon, 0);
				total_outer_rings++;
				part_outer_ring = part;
				loc_outer_ring  = w.Buf;
				WriteU32(&w, 0); /* we'll come back to this once we know how many sub-rings there are */
			}
			WriteRing(&w, count, vx, closed, reverse);
			break;
		}
	}

	/* Rewind and write the number of rings in the last part */
	if (loc_num_outer_rings)
		WriteU32In(loc_num_outer_rings, w.IsBigEndian, total_outer_rings);
	if (loc_outer_ring)
		WriteU32In(loc_outer_ring, w.IsBigEndian, numparts - part_outer_ring);

	return (uint8_t*) w.Buf - (uint8_t*) buf;
}

#endif