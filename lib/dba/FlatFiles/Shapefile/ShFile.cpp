#include "pch.h"
#include "ShFile.h"

using namespace std;

namespace imqs {
namespace dba {
namespace shapefile {

StaticError ErrReadOnly("Shapefile is not opened for write");
StaticError ErrInvalid("Shapefile has invalid parts");
StaticError ErrEmptyGeometry("Shapefile has empty geometry");
StaticError ErrTooManyParts("Shapefile geometry has too many parts");
StaticError ErrTooManyVerts("Shapefile geometry has too many vertices");

File::File() {
}

File::~File() {
	Close();
}

Error File::Open(std::string filename, OpenMode mode, ShapeType typeOfNew) {
	Close();

	BoundsDirty  = false;
	FeatureCount = 0;
	Type         = typeOfNew;

	string extension;
	path::SplitExt(filename, BaseFilename, extension);

	bool isCreate = mode == OpenModeCreate;

	auto err = isCreate ? ShpFile.Create(BaseFilename + ".shp") : ShpFile.Open(BaseFilename + ".shp");
	if (!err.OK()) {
		err = Error::Fmt("Unable to open .shp file %v: %v", BaseFilename + ".shp", err.Message());
		Close();
		return err;
	}

	err = isCreate ? ShxFile.Create(BaseFilename + ".shx") : ShxFile.Open(BaseFilename + ".shx");
	if (!err.OK()) {
		err = Error::Fmt("Unable to open .shx file %v: %v", BaseFilename + ".shx", err.Message());
		Close();
		return err;
	}

	if (isCreate) {
		err = WriteNullHeaders();
		if (!err.OK()) {
			Close();
			return err;
		}
		BoundsDirty = true;
	} else {
		err = ReadHeader();
		if (!err.OK()) {
			Close();
			return err;
		}
	}

	ModeOpen = mode;

	// Delete these, because we don't update them. Their format isn't published. Some kind of R-Tree I think.
	if (mode != OpenModeRead) {
		os::Remove(BaseFilename + ".shb");
		os::Remove(BaseFilename + ".shx");
	}

	TempVec3.resize(TempVecSize);
	TempInt.resize(TempVecSize);

	return Error();
}

void File::SetFeatureType(ShapeType ft) {
	Type = ft;
}

void File::FlushAndSleep() {
	if (IsOpen() && (ModeOpen != OpenModeRead))
		WriteHeaders();
}

void File::Close() {
	FlushAndSleep();
	BaseFilename = "";
	TempVec3.clear();
	TempInt.clear();
	ModeOpen = OpenModeRead;
	ShxFile.Close();
	ShpFile.Close();
}

Error File::ReadHeader() {
	MainHeader main, index;
	Error      err;

	int64_t shpLen = ShpFile.Length();
	int64_t shxLen = ShxFile.Length();
	if (shpLen < (int64_t) sizeof(MainHeader) || shxLen < (int64_t) sizeof(MainHeader))
		return Error("Shapefile is truncated");

	err |= ShpFile.ReadExactlyAt(0, &main, sizeof(MainHeader));
	err |= ShxFile.ReadExactlyAt(0, &index, sizeof(MainHeader));
	if (!err.OK())
		return err;

	FixHeadEndian(main);
	FixHeadEndian(index);

	if (main.FileCode != 9994)
		return Error("Shapefile FileCode not equal to 9994");
	if (main.Version != 1000)
		return Error("Shapefile Version not equal to 1000");

	if (main.Length != shpLen / 2) {
		if (!Tolerant)
			return Error::Fmt("SHP file length inconsistent with header (header: %v, file size: %v)", main.Length * 2, shpLen);
		//TRACE("Tolerating bad SHP file Header.Length\n");
	}
	if (index.Length != shxLen / 2) {
		if (!Tolerant)
			return Error::Fmt("SHX file length inconsistent with header (header: %v, file size: %v)", index.Length * 2, shxLen);
		//TRACE("Tolerating bad SHX file Header.Length\n");
	}

	if (!IsValidType(main.Type))
		return Error::Fmt("Unsupported shapefile feature type %v", main.Type);

	Type = (ShapeType) main.Type;

	FeatureCount = (int) ((shxLen - sizeof(index)) / sizeof(RecordIndex));

	Bounds     = geom3d::BBox3d(main.Xmin, main.Ymin, main.Zmin, main.Xmax, main.Ymax, main.Zmax);
	BoundsMmin = main.Mmin;
	BoundsMmax = main.Mmax;

	return Error();
}

Error File::WriteNullHeaders() {
	MainHeader head;
	memset(&head, 0, sizeof(head));
	auto err = ShxFile.Write(&head, sizeof(head));
	err |= ShpFile.Write(&head, sizeof(head));
	return err;
}

Error File::WriteHeaders() {
	if (BoundsDirty)
		RecalcBounds();

	MainHeader head;
	memset(&head, 0, sizeof(head));
	head.FileCode = 9994;
	head.Version  = 1000;
	head.Type     = Type;
	head.Mmin     = BoundsMmin;
	head.Mmax     = BoundsMmax;
	head.Xmin     = Bounds.X1;
	head.Ymin     = Bounds.Y1;
	head.Zmin     = Bounds.Z1;
	head.Xmax     = Bounds.X2;
	head.Ymax     = Bounds.Y2;
	head.Zmax     = Bounds.Z2;

	Error err;
	head.Length = int32_t(ShpFile.Length() / 2);
	FixHeadEndian(head);
	err |= ShpFile.Seek(0, io::SeekWhence::Begin);
	err |= ShpFile.Write(&head, sizeof(head));
	FixHeadEndian(head);

	head.Length = int32_t(ShxFile.Length() / 2);
	FixHeadEndian(head);
	err |= ShxFile.Seek(0, io::SeekWhence::Begin);
	err |= ShxFile.Write(&head, sizeof(head));
	FixHeadEndian(head);

	return err;
}

void File::FixHeadEndian(MainHeader& head) {
	ENDIAN(head.FileCode, true);
	ENDIAN(head.Length, true);
	ENDIAN(head.Version, false);
	ENDIAN(head.Type, false);
}

void File::ENDIAN(int& v, bool big) {
	if (big) {
		// assuming we are little endian machine
		uint8_t* bv = (uint8_t*) &v;
		std::swap(bv[0], bv[3]);
		std::swap(bv[1], bv[2]);
	}
}

void File::RecalcBounds() {
	Bounds.Reset();
	BoundsMmin = DBL_MAX;
	BoundsMmax = -DBL_MAX;

	std::vector<uint8_t> closed;
	for (int i = 0; i < FeatureCount; i++) {
		bool           isNull;
		uint32_t       nParts, nPoints;
		geom3d::BBox3d bb;
		double         m[2];
		gfx::Vec3d     p;
		closed.clear();

		switch (Type) {
		case ShapeTypePolyline:
		case ShapeTypePolylineM:
		case ShapeTypePolylineZ:
		case ShapeTypePolygon:
		case ShapeTypePolygonM:
		case ShapeTypePolygonZ:
			ReadPolyHead(i, isNull, nParts, closed, nPoints, bb, m);
			if (!isNull)
				Bounds.ExpandToFit(bb);
			break;
		case ShapeTypePoint:
		case ShapeTypePointM:
		case ShapeTypePointZ:
			ReadPoint(i, isNull, p, m);
			if (!isNull)
				Bounds.ExpandToFit(p.x, p.y, p.z);
			break;
		case ShapeTypeMultiPoint:
		case ShapeTypeMultiPointM:
		case ShapeTypeMultiPointZ:
			ReadMultiPointHead(i, isNull, nPoints, bb, m);
			if (!isNull)
				Bounds.ExpandToFit(bb);
			break;
		default:
			break;
		}

		if (!isNull && IsZOrMType(Type)) {
			BoundsMmin = min(BoundsMmin, m[0]);
			BoundsMmax = max(BoundsMmax, m[1]);
		}
	}

	if (!IsZType(Type)) {
		Bounds.Z1 = 0;
		Bounds.Z2 = 0;
	}

	if (!IsZOrMType(Type)) {
		BoundsMmin = 0;
		BoundsMmax = 0;
	}

	BoundsDirty = false;
}

int File::FindNewFeatureIndex() {
	return FeatureCount;
}

int64_t File::FindNewFeaturePosition(size_t contentBytes) {
	return ShpFile.Length();
}

Error File::ReadIndex(int index, RecordIndex& ri) {
	if (index < 0 || index >= FeatureCount)
		return ErrInvalid;
	auto err = ShxFile.ReadExactlyAt(sizeof(MainHeader) + (size_t) index * sizeof(RecordIndex), &ri, sizeof(ri));
	if (!err.OK())
		return err;
	ENDIAN(ri.ContentLength, true);
	ENDIAN(ri.Position, true);
	return Error();
}

Error File::WriteIndex(int index, size_t position, int contentBytes) {
	RecordIndex ri;
	ri.Position      = (int32_t)(position / 2);
	ri.ContentLength = contentBytes / 2;
	return WriteIndex(index, ri);
}

Error File::WriteIndex(int index, const RecordIndex& ri) {
	if (index < 0 || index >= FeatureCount)
		return ErrInvalid;
	RecordIndex fix = ri;
	ENDIAN(fix.ContentLength, true);
	ENDIAN(fix.Position, true);
	size_t len = sizeof(fix);
	return ShxFile.WriteAt(sizeof(MainHeader) + index * sizeof(RecordIndex), &fix, len);
}

int64_t File::FeaturePosition(int index) {
	RecordIndex ri;
	ReadIndex(index, ri);
	return ri.Position * 2;
}

size_t File::FeatureLength(int index) {
	RecordIndex ri;
	ReadIndex(index, ri);
	return ri.ContentLength * 2;
}

Error File::AddFeature(int contentBytes) {
	// find the index of the new feature (equals FeatureCount)
	int featIndex = FindNewFeatureIndex();

	// find a byte position in the SHP file that can accommodate the new feature
	size_t pos = FindNewFeaturePosition(contentBytes);

	// write the indexing information for the record into the SHX file.
	auto err = WriteIndex(featIndex, pos, contentBytes);
	if (!err.OK())
		return err;

	// write the 8-byte record header (leaving the SHP file pointer immediately after the header).
	err |= ShpFile.Seek(pos, io::SeekWhence::Begin);
	err |= WriteRecordHead(featIndex, contentBytes);
	if (!err.OK())
		return err;

	BoundsDirty = true;

	if (featIndex == FeatureCount)
		FeatureCount++;
	return Error();
}

Error File::AddNullFeature() {
	if (!CanWrite())
		return ErrReadOnly;

	ShNull nu;
	static_assert(sizeof(nu) == 4, "ShNull not 4 bytes");

	AddFeature(sizeof(nu));

	nu.Type = ShapeTypeNull;
	return ShpFile.Write(&nu, sizeof(nu));
}

Error File::AddPoint(const gfx::Vec3d& p, double m) {
	if (!CanWrite())
		return ErrReadOnly;

	auto err = AddFeature(BytesForPoint());
	if (!err.OK())
		return err;
	ShPoint pt;
	pt.Type = Type;
	pt.X    = p.x;
	pt.Y    = p.y;
	err     = ShpFile.Write(&pt, sizeof(pt));
	if (!err.OK())
		return err;

	switch (Type) {
	case ShapeTypePointM:
		err = ShpFile.Write(&m, sizeof(double));
		break;
	case ShapeTypePointZ:
		err = ShpFile.Write(&p.z, sizeof(double));
		if (err.OK())
			err = ShpFile.Write(&m, sizeof(double));
		break;
	default:
		IMQS_DIE();
	}
	return err;
}

Error File::AddMultiPoint(int n, const gfx::Vec3d* p, const double* m) {
	if (!CanWrite())
		return ErrReadOnly;

	auto err = AddFeature(BytesForMultiPoint(n));
	if (!err.OK())
		return err;

	// write common header
	ShMultiPoint pt;
	pt.Type          = Type;
	geom3d::BBox3d b = GetBounds(n, p);
	pt.Xmin          = b.X1;
	pt.Ymin          = b.Y1;
	pt.Xmax          = b.X2;
	pt.Ymax          = b.Y2;
	pt.PointCount    = n;
	err |= ShpFile.Write(&pt, sizeof(pt));

	ShRange range;

	// write points
	for (int i = 0; i < n; i++)
		err |= ShpFile.Write(p + i, 16);

	// write Z
	if (Type == ShapeTypeMultiPointZ) {
		range = ShRange(b.Z1, b.Z2);
		err |= ShpFile.Write(&range, sizeof(range));

		for (int i = 0; i < n; i++)
			err |= ShpFile.Write(&p[i].z, 8);
	}

	// write M
	if (Type == ShapeTypeMultiPointM || Type == ShapeTypeMultiPointZ) {
		range = GetBounds(n, m);
		err |= ShpFile.Write(&range, sizeof(range));

		for (int i = 0; i < n; i++)
			err |= ShpFile.Write(m + i, 8);
	}
	return err;
}

Error File::AddPolyline(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m) {
	if (!CanWrite())
		return ErrReadOnly;

	return AddPoly(nParts, partStarts, closed, nPoints, p, m);
}

Error File::AddPolygon(int nParts, const int* partStarts, int nPoints, const gfx::Vec3d* p, const double* m) {
	if (!CanWrite())
		return ErrReadOnly;

	return AddPoly(nParts, partStarts, nullptr, nPoints, p, m);
}

Error File::AddPoly(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m) {
	auto err = AddFeature(BytesForPoly(nParts, closed, nPoints));
	if (!err.OK())
		return err;

	geom3d::BBox3d b = GetBounds(nPoints, p);

	ShPoly poly;
	poly.Type      = Type;
	poly.NumParts  = nParts;
	poly.NumPoints = (closed == nullptr ? nParts : Count(nParts, closed)) + nPoints;
	poly.Xmin      = b.X1;
	poly.Ymin      = b.Y1;
	poly.Xmax      = b.X2;
	poly.Ymax      = b.Y2;

	err = ShpFile.Write(&poly, sizeof(poly));
	if (!err.OK())
		return err;

	if (IsPolygonType(Type))
		return CleanAndWritePolygon(nParts, partStarts, nPoints, p, m, b);
	else
		return CloseAndWritePolyline(nParts, partStarts, closed, nPoints, p, m, b);
}

Error File::CleanAndWritePolygon(int nParts, const int* partStarts, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb) {
	std::vector<gfx::Vec3d> tv;

	// The const_cast does not mean that we alter the contents of p.
	// It is a lexical construct. Before modifying, we make a temporary copy.
	gfx::Vec3d* pFixed = const_cast<gfx::Vec3d*>(p);

	if (nParts == 1) {
		if (geom2d::PolygonOrient(nPoints, &p[0].x, 3) == geom2d::PolyOrient::CCW) {
			// outer ring (and therefore single parts) must be clockwise.
			pFixed = CopyToTemp(nPoints, p, tv);
			Reverse(nPoints, pFixed);
		}
	} else {
		geom2d::RingTreeFinder tree;
		for (int i = 0; i < nParts; i++) {
			int count = PartSize(i, nParts, partStarts, nPoints);
			tree.AddRing(count, (const double*) (p + partStarts[i]), 3);
		}

		tree.Analyze();

		pFixed = CopyToTemp(nPoints, p, tv);

		// fix ordering so that rings at even intervals are clockwise. The shapefile
		// spec only talks of 2 possible ring levels (ie outer + islands), but I am
		// allowing more than that, and consequently sucking the even/odd ordering
		// out of my bleeding thumb.
		for (int i = 0; i < nParts; i++) {
			int  count   = PartSize(i, nParts, partStarts, nPoints);
			bool isCW    = geom2d::PolygonOrient(count, (const double*) (p + partStarts[i]), 3) == geom2d::PolyOrient::CW;
			bool isOuter = (tree.Rings[i]->Level & 1) == 0;
			if (isCW != isOuter)
				Reverse(count, pFixed + partStarts[i]);
		}
	}

	return WriteAdjustedPolyArray(nParts, partStarts, nullptr, nPoints, pFixed, m, bb);
}

Error File::CloseAndWritePolyline(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb) {
	return WriteAdjustedPolyArray(nParts, partStarts, closed, nPoints, p, m, bb);
}

// Write the adjusted (for closure) XY, Z, and M arrays.
Error File::WriteAdjustedPolyArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb) {
	int nClosed      = closed == nullptr ? nParts : Count(nParts, closed);
	int nPointsFixed = nPoints + nClosed;

	// write the adjusted Parts array
	int offset = 0;
	for (int i = 0; i < nParts; i++) {
		int  start = partStarts[i] + offset;
		auto err   = ShpFile.Write(&start, 4);
		if (!err.OK())
			return err;
		offset += (closed == nullptr || closed[i]) ? 1 : 0;
	}

	// write the adjusted Points array
	auto err = WriteAdjustedArray<0, 16, gfx::Vec3d, false>(nParts, partStarts, closed, nPoints, p);
	if (!err.OK())
		return err;

	// adjusted Z
	if (IsZType(Type)) {
		err |= ShpFile.Write(&bb.Z1, 8);
		err |= ShpFile.Write(&bb.Z2, 8);
		err |= WriteAdjustedArray<16, 8, gfx::Vec3d, false>(nParts, partStarts, closed, nPoints, p);
	}

	// adjusted M (m is allowed to be nullptr)
	if (IsZType(Type) || IsMType(Type)) {
		ShRange mr(0, 0);
		if (m)
			mr = GetBounds(nPoints, m);
		err |= ShpFile.Write(&mr.Min, 8);
		err |= ShpFile.Write(&mr.Max, 8);
		err |= WriteAdjustedArray<0, 8, double, true>(nParts, partStarts, closed, nPoints, m);
	}

	return err;
}

#ifdef _MSC_VER
#pragma warning(disable : 6011) // null pointer deref. Might be a genuine bug. Very hard to tell
#endif

template <int Offset, int Size, class TData, bool AllowNull>
Error File::WriteAdjustedArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const TData* p) {
	IMQS_ASSERT(!AllowNull || Size < 64);
	uint8_t black[64];
	bool    zero = AllowNull && p == nullptr;

	for (int i = 0; i < nParts; i++) {
		int end = i == nParts - 1 ? nPoints : partStarts[i + 1];
		for (int j = partStarts[i]; j < end; j++) {
			uint8_t* pa  = (uint8_t*) &p[j];
			auto     err = ShpFile.Write(zero ? black : pa + Offset, Size);
			if (!err.OK())
				return err;
		}
		if ((closed == nullptr || closed[i])) {
			uint8_t* pa  = (uint8_t*) &p[partStarts[i]];
			auto     err = ShpFile.Write(zero ? black : pa + Offset, Size);
			if (!err.OK())
				return err;
		}
	}
	return Error();
}

Error File::ReadIsFeatureNull(int index, bool& isNull) {
	RecordIndex ri;
	auto        err = ReadIndex(index, ri);
	if (!err.OK())
		return err;
	// We can tell whether a feature is null using only the index file, because
	// the content length of a null feature is always 4 bytes, which
	// contains only the shape type value of zero. I hope that all shapefile
	// writers are diligent on this point.
	isNull = ri.ContentLength * 2 == 4;
	return Error();
}

Error File::ReadPoint(int index, bool& isNull, gfx::Vec3d& p, double* m) {
	size_t pos = FeaturePosition(index);
	auto   err = ShpFile.Seek(pos + sizeof(RecordHeader), io::SeekWhence::Begin);
	if (!err.OK())
		return err;

	ShPoint   pt;
	ShPointM  ptM;
	ShPointMZ ptZ;

	switch (Type) {
	case ShapeTypePoint:
		err    = ShpFile.ReadExactly(&pt, sizeof(pt));
		isNull = pt.Type == ShapeTypeNull;
		p      = gfx::Vec3d(pt.X, pt.Y, 0);
		break;
	case ShapeTypePointM:
		err    = ShpFile.ReadExactly(&ptM, sizeof(ptM));
		isNull = ptM.Type == ShapeTypeNull;
		p      = gfx::Vec3d(ptM.X, ptM.Y, 0);
		if (m != nullptr)
			*m = ptM.M;
		break;
	case ShapeTypePointZ:
		err    = ShpFile.ReadExactly(&ptZ, sizeof(ptZ));
		isNull = ptZ.Type == ShapeTypeNull;
		p      = gfx::Vec3d(ptZ.X, ptZ.Y, ptZ.Z);
		if (m != nullptr)
			*m = ptZ.M;
		break;
	default:
		IMQS_DIE();
	}

	return err;
}

Error File::ReadMultiPointHead(int index, bool& isNull, uint32_t& n, geom3d::BBox3d& bb, double* mRange) {
	size_t pos = FeaturePosition(index);
	auto   err = ShpFile.Seek(pos + sizeof(RecordHeader), io::SeekWhence::Begin);

	ShMultiPoint pt;
	err |= ShpFile.ReadExactly(&pt, sizeof(pt));

	if (IsZOrMType(Type)) {
		// skip over the XY points
		err |= ShpFile.Seek(pt.PointCount * 16, io::SeekWhence::Current);
	}

	if (Type == ShapeTypePointZ) {
		err |= ShpFile.ReadExactly(&bb.Z1, 8);
		err |= ShpFile.ReadExactly(&bb.Z2, 8);
	} else {
		bb.Z1 = 0;
		bb.Z2 = 0;
	}

	if (mRange != nullptr) {
		if (IsZOrMType(Type)) {
			err |= ShpFile.ReadExactly(mRange, 8);
			err |= ShpFile.ReadExactly(mRange + 1, 8);
		} else {
			mRange[0] = 0;
			mRange[1] = 0;
		}
	}

	isNull = pt.Type == ShapeTypeNull;
	n      = pt.PointCount;
	return err;
}

Error File::ReadMultiPoints(int index, gfx::Vec3d* p, double* m) {
	size_t pos = FeaturePosition(index);
	auto   err = ShpFile.Seek(pos + sizeof(RecordHeader), io::SeekWhence::Begin);

	ShMultiPoint pt;
	err |= ShpFile.ReadExactly(&pt, sizeof(pt));

	// read the XY points
	for (int i = 0; i < pt.PointCount; i++) {
		err |= ShpFile.ReadExactly(&p[i].x, 8);
		err |= ShpFile.ReadExactly(&p[i].y, 8);
		p[i].z = 0;
	}
	if (Type == ShapeTypeMultiPointZ) {
		// skip bounds
		err |= ShpFile.Seek(16, io::SeekWhence::Current);
		for (int i = 0; i < pt.PointCount; i++)
			err |= ShpFile.ReadExactly(&p[i].z, 8);
	}
	if (m != nullptr) {
		if (Type == ShapeTypeMultiPointZ || Type == ShapeTypeMultiPointM) {
			// skip bounds
			err |= ShpFile.Seek(16, io::SeekWhence::Current);
			for (int i = 0; i < pt.PointCount; i++)
				err |= ShpFile.ReadExactly(&m[i], 8);
		} else {
			for (int i = 0; i < pt.PointCount; i++)
				m[i] = 0;
		}
	}
	return err;
}

Error File::ReadPolyHead(int index, bool& isNull, uint32_t& nParts, std::vector<uint8_t>& closed, uint32_t& nPoints, geom3d::BBox3d& bb, double* mRange) {
	nParts  = 0;
	nPoints = 0;

	size_t pos = FeaturePosition(index);
	auto   err = ShpFile.Seek(pos + sizeof(RecordHeader), io::SeekWhence::Begin);

	ShPoly poly;
	err |= ShpFile.ReadExactly(&poly, sizeof(poly));
	if (!err.OK())
		return err;

	isNull = poly.Type == ShapeTypeNull;
	closed.clear();

	// Invalid/Corrupt data ($ISSUE-002)
	if (poly.NumParts <= 0 || poly.NumPoints <= 0) {
		isNull = true;
		if (TolerateEmptyGeometryAsNull)
			return Error();
		else
			return ErrEmptyGeometry;
	}
	if (poly.NumParts > MaxPolyParts) {
		isNull = true;
		return ErrTooManyParts;
	}
	if (poly.NumPoints > MaxPolyVertices) {
		isNull = true;
		return ErrTooManyVerts;
	}
	if (isNull) {
		closed.resize(0);
		return Error();
	}

	nParts = poly.NumParts;
	bb.X1  = poly.Xmin;
	bb.Y1  = poly.Ymin;
	bb.X2  = poly.Xmax;
	bb.Y2  = poly.Ymax;

	closed.resize(nParts);

	if (IsPolygonType(Type)) {
		for (uint32_t i = 0; i < nParts; i++)
			closed[i] = true;

		// Detect $ISSUE-001 -- Phase 1 of 2 -- note this is dual-detected again during ReadPoly()
		err |= ShpFile.Seek((nParts - 1) * sizeof(int), io::SeekWhence::Current);
		int finalPartStart;
		err |= ShpFile.ReadExactly(&finalPartStart, 4);
		if (finalPartStart == poly.NumPoints)
			nParts--; // bingo

		// seek over parts and points
		err |= ShpFile.Seek(poly.NumParts * sizeof(int) + poly.NumPoints * 2 * sizeof(double), io::SeekWhence::Current);

		// we will remove the duplicate vertices
		nPoints = poly.NumPoints - nParts;
	} else {
		// read parts and points in order to determine which polylines are closed
		int              nclosed = 0;
		std::vector<int> tparts;
		int*             parts = &TempInt[0];
		if (nParts > TempVecSize) {
			tparts.resize(nParts);
			parts = &tparts[0];
		}
		err |= ShpFile.ReadExactly(parts, 4 * nParts);
		if (!err.OK())
			return err;
		int64_t fpos = ShpFile.Position();
		for (uint32_t i = 0; i < nParts; i++) {
			gfx::Vec2d a, b;
			int        plen = PartSize(i, nParts, parts, poly.NumPoints);
			//if ( plen < 2 ) isNull = true; // tired of trying to salvage crap data $ISSUE-002
			err |= ShpFile.Seek(fpos + parts[i] * 16, io::SeekWhence::Begin);
			err |= ShpFile.ReadExactly(&a, 16);
			err |= ShpFile.Seek(fpos + (parts[i] + plen - 1) * 16, io::SeekWhence::Begin);
			err |= ShpFile.ReadExactly(&b, 16);
			if (a == b)
				nclosed++;
			closed[i] = a == b;
		}
		nPoints = poly.NumPoints - nclosed;
		// seek to end of points
		err |= ShpFile.Seek(fpos + poly.NumPoints * 16, io::SeekWhence::Begin);
	}

	if (IsZType(Type)) {
		err |= ShpFile.ReadExactly(&bb.Z1, 8);
		err |= ShpFile.ReadExactly(&bb.Z2, 8);
		err |= ShpFile.Seek(poly.NumPoints * sizeof(double), io::SeekWhence::Current);
	} else {
		bb.Z1 = 0;
		bb.Z2 = 0;
	}

	if (mRange != nullptr) {
		if (IsZOrMType(Type)) {
			err |= ShpFile.ReadExactly(&mRange[0], 8);
			err |= ShpFile.ReadExactly(&mRange[1], 8);
		} else {
			mRange[0] = 0;
			mRange[1] = 0;
		}
	}
	return err;
}

Error File::ReadPoly(int index, int* partStarts, const std::vector<uint8_t>& closed, gfx::Vec3d* p, double* m) {
	size_t pos = FeaturePosition(index);
	auto   err = ShpFile.Seek(pos + sizeof(RecordHeader), io::SeekWhence::Begin);

	ShPoly poly;
	err |= ShpFile.ReadExactly(&poly, sizeof(poly));
	if (!err.OK())
		return err;

	if (poly.Type == ShapeTypeNull) {
		return ErrInvalid;
	}

	err |= ShpFile.ReadExactly(partStarts, sizeof(int) * poly.NumParts);
	if (!err.OK())
		return err;

	if (partStarts[poly.NumParts - 1] == poly.NumPoints) {
		poly.NumParts--;
	} // $ISSUE-001 -- Phase 2 of 2

	// Detect ($ISSUE-003)
	int mpart = -1;
	for (int i = 0; i < poly.NumParts; i++) {
		if (((uint32_t) partStarts[i]) >= (uint32_t) poly.NumPoints ||
		    partStarts[i] <= mpart) {
			return ErrInvalid;
		}
		mpart = partStarts[i];
	}

	int adjustedNumPoints = poly.NumPoints - Count(poly.NumParts, &closed[0]);

	// read XY
	err |= ReadAdjustedArray<0, 16, gfx::Vec3d>(poly.NumParts, partStarts, &closed.front(), poly.NumPoints, p);
	if (!err.OK())
		return err;

	// read Z
	if (IsZType(Type)) {
		// skip range
		err |= ShpFile.Seek(16, io::SeekWhence::Current);
		err |= ReadAdjustedArray<16, 8, gfx::Vec3d>(poly.NumParts, partStarts, &closed.front(), poly.NumPoints, p);
	} else {
		for (int i = 0; i < adjustedNumPoints; i++)
			p[i].z = 0;
	}

	// read M
	if (IsZOrMType(Type) && m != nullptr) {
		// skip range
		err |= ShpFile.Seek(16, io::SeekWhence::Current);
		err |= ReadAdjustedArray<0, 8, double>(poly.NumParts, partStarts, &closed.front(), poly.NumPoints, m);
	} else if (m != nullptr) {
		for (int i = 0; i < adjustedNumPoints; i++)
			m[i] = 0;
	}

	AdjustPartStarts(poly.NumParts, partStarts, &closed.front());

	return Error();
}

void File::AdjustPartStarts(int nParts, int* partStarts, const uint8_t* closed) {
	int off = 0;
	for (int i = 0; i < nParts; i++) {
		partStarts[i] -= off;
		off += closed[i] ? 1 : 0;
	}
}

template <int Offset, int Size, class TData>
Error File::ReadAdjustedArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const TData* p) {
	int outset = 0;
	for (int i = 0; i < nParts; i++) {
		int size = PartSize(i, nParts, partStarts, nPoints);
		if (size < 0)
			return ErrInvalid;
		int rsize = closed[i] ? size - 1 : size;
		for (int j = 0; j < rsize; j++) {
			uint8_t* pb  = (uint8_t*) &p[outset + j];
			auto     err = ShpFile.ReadExactly(pb + Offset, Size);
			if (!err.OK())
				return err;
		}
		outset += rsize;
		if (closed[i]) {
			auto err = ShpFile.Seek(Size, io::SeekWhence::Current);
			if (!err.OK())
				return err;
		}
	}
	return Error();
}

geom3d::BBox3d File::GetBounds(int n, const gfx::Vec3d* p) {
	geom3d::BBox3d b;
	for (int i = 0; i < n; i++) {
		b.ExpandToFit(p[i].x, p[i].y, p[i].z);
	}
	return b;
}

ShRange File::GetBounds(int n, const double* m) {
	ShRange r(DBL_MAX, -DBL_MAX);
	for (int i = 0; i < n; i++) {
		r.Min = min(r.Min, m[i]);
		r.Max = max(r.Max, m[i]);
	}
	return r;
}

int File::Count(int n, const uint8_t* b) {
	int t = 0;
	for (int i = 0; i < n; i++)
		t += b[i] ? 1 : 0;
	return t;
}

void File::Reverse(int n, gfx::Vec3d* p) {
	int        hn = n / 2;
	gfx::Vec3d t;
	for (int i = 0; i < hn; i++) {
		t            = p[i];
		p[i]         = p[n - i - 1];
		p[n - i - 1] = t;
	}
}

gfx::Vec3d* File::CopyToTemp(int n, const gfx::Vec3d* p, std::vector<gfx::Vec3d>& alternate) {
	gfx::Vec3d* up = &TempVec3[0];
	if (n > TempVecSize) {
		alternate.resize(n);
		up = &alternate[0];
	}
	memcpy(up, p, n * sizeof(gfx::Vec3d));
	return up;
}

Error File::WriteRecordHead(int index_zero_based, int contentlength_bytes) {
	RecordHeader head;
	head.Index         = index_zero_based + 1; // Index is 1 based.
	head.ContentLength = contentlength_bytes / 2;
	ENDIAN(head.Index, true);
	ENDIAN(head.ContentLength, true);
	return ShpFile.Write(&head, sizeof(head));
}

int File::BytesForPoint() {
	switch (Type) {
	case ShapeTypePoint: return 4 + 16;
	case ShapeTypePointM: return 4 + 24;
	case ShapeTypePointZ: return 4 + 32;
	default:
		IMQS_DIE();
	}
	return 0;
}

int File::BytesForMultiPoint(int n) {
	switch (Type) {
	case ShapeTypeMultiPoint: return 4 + sizeof(double) * 4 + 4 + n * 16;
	case ShapeTypeMultiPointM: return 4 + sizeof(double) * 6 + 4 + n * 24;
	case ShapeTypeMultiPointZ: return 4 + sizeof(double) * 8 + 4 + n * 32;
	default:
		IMQS_DIE();
	}
	return 0;
}

int File::BytesForPoly(int nParts, const uint8_t* closed, int nPoints) {
	int nExtra;
	if (IsPolylineType(Type)) {
		// count number of closed polylines, for which we will have to add 1 vertex each.
		nExtra = Count(nParts, closed);
	} else {
		// for polygons, every ring is closed.
		nExtra = nParts;
	}

	switch (Type) {
	case ShapeTypePolyline:
	case ShapeTypePolygon:
		return 4 + sizeof(double) * 4 + 4 + 4 + nParts * 4 + (nPoints + nExtra) * 16;
	case ShapeTypePolylineM:
	case ShapeTypePolygonM:
		return 4 + sizeof(double) * 6 + 4 + 4 + nParts * 4 + (nPoints + nExtra) * 24;
	case ShapeTypePolylineZ:
	case ShapeTypePolygonZ:
		return 4 + sizeof(double) * 8 + 4 + 4 + nParts * 4 + (nPoints + nExtra) * 32;
	default:
		IMQS_DIE();
	}
	return 0;
}

int File::BytesForMultiPatch(int n, int m) {
	return 4 + sizeof(double) * 4 + 4 + 4 + 8 * n + sizeof(double) * 4 + m * 24;
}

int File::Debug(int a, int b) {
	for (int i = 0; i < FeatureCount; i++) {
		RecordIndex ri;
		ReadIndex(i, ri);
	}
	return 0;
}

} // namespace shapefile
} // namespace dba
} // namespace imqs