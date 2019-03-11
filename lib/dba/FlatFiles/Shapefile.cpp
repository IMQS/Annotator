#include "pch.h"
#include "Shapefile.h"

namespace imqs {
namespace dba {

Error Shapefile::Open(const std::string& filename, bool create) {
	shapefile::File::OpenMode mode = create ? shapefile::File::OpenModeCreate : shapefile::File::OpenModeRead;
	auto                      err  = Shp.Open(filename, mode);
	if (!err.OK())
		return err;

	schema::Field gfield;
	gfield.Name = "Geometry";

	switch (Shp.GetFeatureType()) {
	case shapefile::ShapeTypePoint:
	case shapefile::ShapeTypePointZ:
	case shapefile::ShapeTypePointM:
		gfield.Type = Type::GeomPoint;
		break;
	case shapefile::ShapeTypeMultiPoint:
	case shapefile::ShapeTypeMultiPointZ:
	case shapefile::ShapeTypeMultiPointM:
		gfield.Type = Type::GeomMultiPoint;
		break;
	case shapefile::ShapeTypePolyline:
	case shapefile::ShapeTypePolylineZ:
	case shapefile::ShapeTypePolylineM:
		gfield.Type = Type::GeomPolyline;
		break;
	case shapefile::ShapeTypePolygon:
	case shapefile::ShapeTypePolygonZ:
	case shapefile::ShapeTypePolygonM:
		gfield.Type = Type::GeomPolygon;
		break;
	default:
		Shp.Close();
		return Error::Fmt("Unsupported shapefile type %v", (int) Shp.GetFeatureType());
	}

	auto ft   = Shp.GetFeatureType();
	HasM      = false;
	HasZ      = false;
	GeomDims  = 2;
	GeomFlags = dba::GeomFlags::Double;

	if (ft == shapefile::ShapeTypePointM || ft == shapefile::ShapeTypeMultiPointM || ft == shapefile::ShapeTypePolylineM || ft == shapefile::ShapeTypePolygonM) {
		HasM     = true;
		GeomDims = 3;
		GeomFlags |= dba::GeomFlags::HasM;
	}

	if (ft == shapefile::ShapeTypePointZ || ft == shapefile::ShapeTypeMultiPointZ || ft == shapefile::ShapeTypePolylineZ || ft == shapefile::ShapeTypePolygonZ) {
		HasM     = true;
		HasZ     = true;
		GeomDims = 4;
		GeomFlags |= dba::GeomFlags::HasM | dba::GeomFlags::HasZ;
	}

	std::string basename, extension;
	path::SplitExt(filename, basename, extension);

	err = DBF.Open(basename + ".dbf", create);
	if (!err.OK()) {
		Shp.Close();
		return err;
	}

	std::string prj;
	err = os::ReadWholeFile(basename + ".prj", prj);
	if (!err.OK() && !os::IsNotExist(err)) {
		return Error::Fmt("Error reading %v: %v", basename + ".prj", err.Message());
	} else if (err.OK()) {
		err = projwrap::ParseWKT(prj.c_str(), nullptr, &SRID);
		if (!err.OK())
			return err;
		gfield.SRID = SRID;
	}

	FieldsCache.push_back(gfield);
	for (const auto& f : DBF.FieldsCache)
		FieldsCache.push_back(f);

	return Error();
}

int64_t Shapefile::RecordCount() {
	return Shp.GetFeatureCount();
}

std::vector<schema::Field> Shapefile::Fields() {
	return FieldsCache;
}

Error Shapefile::Read(size_t field, int64_t record, Attrib& val, Allocator* alloc) {
	if (field == 0) {
		// geometry
		val.SetNull();
		bool isNull;
		auto err = Shp.ReadIsFeatureNull((int) record, isNull);
		if (!err.OK())
			return err;
		if (isNull)
			return Error();

		gfx::Vec3d     pt = {0, 0, 0};
		double         m  = 0;
		uint32_t       nPoints;
		uint32_t       nParts;
		geom3d::BBox3d bbox;
		double*        dst      = nullptr;
		int            irec     = (int) record;
		auto           geomType = FieldsCache[0].Type;

		switch (geomType) {
		case Type::GeomPoint: {
			err = Shp.ReadPoint(irec, isNull, pt, &m);
			if (isNull)
				return Error();
			double dbl[4] = {pt.x, pt.y, pt.z, m};
			val.SetPoint(GeomFlags, dbl, SRID, alloc);
			break;
		}
		case Type::GeomMultiPoint:
			err = Shp.ReadMultiPointHead(irec, isNull, nPoints, bbox);
			if (!err.OK())
				return err;
			if (isNull)
				return Error();
			val.SetMultiPoint(GeomFlags, (int) nPoints, (double*) nullptr, SRID, alloc);
			TempVx.resize(nPoints);
			TempM.resize(nPoints);
			err = Shp.ReadMultiPoints(irec, &TempVx[0], &TempM[0]);
			if (!err.OK())
				return err;
			dst = val.GeomVerticesDbl();
			for (size_t i = 0; i < (size_t) nPoints; i++) {
				*dst++ = TempVx[i].x;
				*dst++ = TempVx[i].y;
				if (HasZ)
					*dst++ = TempVx[i].z;
				if (HasM)
					*dst++ = TempM[i];
			}
			break;
		case Type::GeomPolyline:
		case Type::GeomPolygon:
			err = Shp.ReadPolyHead(irec, isNull, nParts, TempClosed, nPoints, bbox);
			if (!err.OK())
				return err;
			if (isNull)
				return Error();
			TempVx.resize(nPoints);
			TempM.resize(nPoints);
			TempParts.resize(nParts + 1);
			err = Shp.ReadPoly(irec, (int*) &TempParts[0], TempClosed, &TempVx[0], &TempM[0]);
			if (!err.OK())
				return err;
			for (size_t i = 0; i < (size_t) nParts; i++) {
				if (TempClosed[i] || geomType == Type::GeomPolygon)
					TempParts[i] |= GeomPartFlag_Closed;
			}
			TempParts[nParts] = nPoints;
			if (nParts == 1 || geomType == Type::GeomPolyline) {
				// For single part polygons, or polylines, we can copy the vertices directly into the freshly allocated attrib
				if (geomType == Type::GeomPolygon)
					TempParts[0] |= GeomPartFlag_ExteriorRing;
				val.SetPoly(FieldsCache[0].Type, GeomFlags | GeomFlags::RingsInWKBOrder, nParts, &TempParts[0], (double*) nullptr, SRID, alloc);
				dst = val.GeomVerticesDbl();
				for (size_t i = 0; i < (size_t) nPoints; i++) {
					*dst++ = TempVx[i].x;
					*dst++ = TempVx[i].y;
					if (HasZ)
						*dst++ = TempVx[i].z;
					if (HasM)
						*dst++ = TempM[i];
				}
			} else {
				// For multipart polygons, we need to analyze the rings to see which ones are exterior vs interior,
				// and also which rings are inside which other rings.
				// By omitting GeomFlags::RingsInWKBOrder to SetPoly(), we force SetPoly() to tidy things up.
				TempDbl.clear();
				for (size_t i = 0; i < (size_t) nPoints; i++) {
					TempDbl.push_back(TempVx[i].x);
					TempDbl.push_back(TempVx[i].y);
					if (HasZ)
						TempDbl.push_back(TempVx[i].z);
					if (HasM)
						TempDbl.push_back(TempM[i]);
				}
				val.SetPoly(FieldsCache[0].Type, GeomFlags, nParts, &TempParts[0], (double*) &TempDbl[0], SRID, alloc);
			}
			break;
		default:
			IMQS_DIE();
		}
		return err;
	} else {
		// DBF
		return DBF.Read(field - 1, record, val, alloc);
	}
}

} // namespace dba
} // namespace imqs