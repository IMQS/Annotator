#include "pch.h"
/* You may do whatever you want with this code.
The author assumes no responsibility for it.
Ben Harper */
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "WKG.h"
#include "WKG_internal.h"

/*
///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// WKB Parser
///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////
*/

typedef struct _WKBParser {
	uint8_t* Buf;
	uint8_t* End;
	uint8_t  IsBig;     /* true if source is big endian */
	uint8_t  HasZ;      /* source has Z */
	uint8_t  HasM;      /* source has M */
	uint8_t  HasSRID;   /* source has SRID */
	uint8_t  CountOnly; /* WKG_BinParseFlag_CountOnly */
	size_t   VxSize;    /* number of bytes per vertex */
} WKBParser;

static BOOL CanRead(WKBParser* p, size_t bytes) {
	return p->Buf + bytes <= p->End ? TRUE : FALSE;
}

static uint8_t ReadU8(WKBParser* p) {
	uint8_t v = p->Buf[0];
	p->Buf += sizeof(v);
	return v;
}

static uint32_t ReadU32(WKBParser* p) {
	uint32_t v;
	if (p->IsBig) {
		v = ((uint32_t) p->Buf[0] << 24) |
		    ((uint32_t) p->Buf[1] << 16) |
		    ((uint32_t) p->Buf[2] << 8) |
		    ((uint32_t) p->Buf[3]);
	} else {
		v = ((uint32_t) p->Buf[3] << 24) |
		    ((uint32_t) p->Buf[2] << 16) |
		    ((uint32_t) p->Buf[1] << 8) |
		    ((uint32_t) p->Buf[0]);
	}
	p->Buf += sizeof(v);
	return v;
}

static double ReadDbl(WKBParser* p) {
	uint64_t v;
	if (p->IsBig) {
		v = ((uint64_t) p->Buf[0] << 56) |
		    ((uint64_t) p->Buf[1] << 48) |
		    ((uint64_t) p->Buf[2] << 40) |
		    ((uint64_t) p->Buf[3] << 32) |
		    ((uint64_t) p->Buf[4] << 24) |
		    ((uint64_t) p->Buf[5] << 16) |
		    ((uint64_t) p->Buf[6] << 8) |
		    ((uint64_t) p->Buf[7]);
	} else {
		v = ((uint64_t) p->Buf[7] << 56) |
		    ((uint64_t) p->Buf[6] << 48) |
		    ((uint64_t) p->Buf[5] << 40) |
		    ((uint64_t) p->Buf[4] << 32) |
		    ((uint64_t) p->Buf[3] << 24) |
		    ((uint64_t) p->Buf[2] << 16) |
		    ((uint64_t) p->Buf[1] << 8) |
		    ((uint64_t) p->Buf[0]);
	}
	p->Buf += sizeof(v);
	return *((double*) &v);
}

static WKG_BinParseResult ReadVx(WKBParser* p, WKG_Target* t, int ipart, int iring, int ivertex, int nvertex) {
	if (p->CountOnly) {
		p->Buf += p->VxSize;
		return WKG_BinParse_OK;
	} else {
		t->VX = ReadDbl(p);
		t->VY = ReadDbl(p);
		if (p->HasZ)
			t->VZ = ReadDbl(p);
		if (p->HasM)
			t->VM = ReadDbl(p);
		return (WKG_BinParseResult) t->Vertex(t, ipart, iring, ivertex, nvertex);
	}
}

static void ReadVxNoSend(WKBParser* p, double& x, double& y) {
	x = ReadDbl(p);
	y = ReadDbl(p);
	if (p->HasZ)
		p->Buf += sizeof(double);
	if (p->HasM)
		p->Buf += sizeof(double);
}

static WKG_BinParseResult ReadRing(WKBParser* p, WKG_Target* t, int ipart, int iring) {
	uint32_t           numv, i;
	WKG_BinParseResult lastResult = WKG_BinParse_OK;
	if (!CanRead(p, 4))
		return WKG_BinParse_Overrun;
	else
		numv = ReadU32(p);

	if (numv == 1)
		return WKG_BinParse_InvalidInput;

	if (!CanRead(p, p->VxSize * numv)) {
		return WKG_BinParse_Overrun;
	} else {
		// assume that polygons rings are always closed, because that's what the spec demands
		int closed = TRUE;
		if (t->Type == WKG_LineString || t->Type == WKG_MultiLineString) {
			// determine whether the ring is closed
			double x1, y1, x2, y2;
			ReadVxNoSend(p, x1, y1);
			p->Buf += p->VxSize * (numv - 2);
			ReadVxNoSend(p, x2, y2);
			p->Buf -= p->VxSize * numv;
			closed = (x1 == x2 && y1 == y2) ? TRUE : FALSE;
		}

		uint32_t filteredVertexCount = closed ? numv - 1 : numv;

		if (p->CountOnly) {
			t->Count(t, ipart, iring, filteredVertexCount, closed);
			p->Buf += p->VxSize * numv;
		} else {
			for (i = 0; i < filteredVertexCount && lastResult == WKG_BinParse_OK; i++)
				lastResult = ReadVx(p, t, ipart, iring, i, filteredVertexCount);
			// skip over the repeated vertex for a closed ring
			if (closed)
				p->Buf += p->VxSize;
		}
	}

	return lastResult;
}

WKG_API int WKG_BinParse(const void* buf, size_t bytes, uint32_t parse_flags, WKG_Target* target) {
	uint32_t           type;
	uint32_t           tlow;
	uint32_t           part, numparts, ring, numrings, vert, numverts;
	WKG_BinParseResult lastResult = WKG_BinParse_OK;
	WKBParser          sp;
	WKBParser* const   p = &sp;

	target->SRID = 0;

	p->Buf       = (uint8_t*) buf;
	p->End       = (uint8_t*) buf + bytes;
	p->CountOnly = !!(parse_flags & WKG_BinParseFlag_CountOnly);

	if (!CanRead(p, 5)) {
		return WKG_BinParse_Overrun;
	} else {
		p->IsBig = ReadU8(p) == wkbXDR;
		type     = ReadU32(p);
	}

	if (type & 0xff000000) {
		/* EWKB (PostGIS) */
		tlow       = type & 0x0000FFFF;
		p->HasZ    = !!(EWKBFlag_Z & type);
		p->HasM    = !!(EWKBFlag_M & type);
		p->HasSRID = !!(EWKBFlag_SRID & type);
		if (p->HasSRID) {
			if (!CanRead(p, 4))
				return WKG_BinParse_Overrun;
			else
				target->SRID = ReadU32(p);
		}
	} else {
		/* OGC WKB */
		tlow       = type % 1000;
		p->HasZ    = (type - tlow) == wkbOffsetZ || (type - tlow) == wkbOffsetZM;
		p->HasM    = (type - tlow) == wkbOffsetM || (type - tlow) == wkbOffsetZM;
		p->HasSRID = FALSE;
	}

	// clamp so that we can't read outside our translation array
	const size_t WKB_2_WKG_Size = sizeof(WKB_2_WKG) / sizeof(WKB_2_WKG[0]);
	if (tlow > WKB_2_WKG_Size - 1)
		tlow = WKB_2_WKG_Size - 1;

	p->VxSize = ((p->HasZ ? 1 : 0) + (p->HasM ? 1 : 0) + 2) * sizeof(double);

	target->HasZ = p->HasZ;
	target->HasM = p->HasM;
	target->Type = WKB_2_WKG[tlow];

	switch (tlow) {
	case wkbPoint:
		if (!CanRead(p, p->VxSize)) {
			return WKG_BinParse_Overrun;
		} else {
			if (p->CountOnly)
				target->Count(target, 0, 0, 1, 0);
			else
				lastResult = ReadVx(p, target, 0, 0, 0, 1);
		}
		break;
	case wkbLineString:
		lastResult = ReadRing(p, target, 0, 0);
		break;
	case wkbMultiPoint:
		if (CanRead(p, 4))
			numverts = ReadU32(p);
		else
			return WKG_BinParse_Overrun;

		if (!CanRead(p, numverts * (p->VxSize + 5))) {
			return WKG_BinParse_Overrun;
		} else {
			if (p->CountOnly) {
				target->Count(target, 0, 0, numverts, 0);
			} else {
				for (vert = 0; vert < numverts && lastResult == WKG_BinParse_OK; vert++) {
					p->Buf += 5; /* skip over redundant point head */
					lastResult = ReadVx(p, target, 0, 0, vert, numverts);
				}
			}
		}
		break;
	case wkbPolygon:
	case wkbMultiLineString:
	case wkbMultiPolygon:
		if (tlow == wkbMultiPolygon) {
			if (!CanRead(p, 4))
				return WKG_BinParse_Overrun;
			else
				numparts = ReadU32(p);
		} else {
			numparts = 1;
		}
		for (part = 0; part < numparts && lastResult == WKG_BinParse_OK; part++) {
			if (tlow == wkbMultiPolygon)
				p->Buf += 5; /* skip over redundant polygon head */

			if (!CanRead(p, 4))
				return WKG_BinParse_Overrun;
			else
				numrings = ReadU32(p);

			for (ring = 0; ring < numrings && lastResult == WKG_BinParse_OK; ring++) {
				if (tlow == wkbMultiLineString)
					p->Buf += 5; /* skip over redundant linestring head */
				lastResult = ReadRing(p, target, part, ring);
			}
		}
		break;
	default:
		return WKG_BinParse_InvalidInput;
	}

	return lastResult;
}
