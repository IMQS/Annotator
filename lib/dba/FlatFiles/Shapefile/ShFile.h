#pragma once

#include "ShHeaders.h"

namespace imqs {
namespace dba {
namespace shapefile {

/* Main shapefile interface.

$ISSUE-001: Polygon shapefiles wherein the second part is empty. It has no vertices. Origin unknown (Municipality of Cape Town, Jan 2010).
$ISSUE-002: Polygons have NumParts = 0 or NumPoints = 0. Also Cape Town. July 2010 (Zoning.shp).
$ISSUE-003: Polygons have invalid part starts. This I have not in fact witnessed. I erroneously blamed the data when it was my adb-based cleanup detection.
            I leave it in because it seems sensible anyway, and it's a very cheap test.

*/
class File {
public:
	File();
	~File();

	enum OpenMode {
		OpenModeRead   = 1,
		OpenModeCreate = 2
	};

	// Open a file. If you do not specify any type here during creation, then you must do so with SetFeatureType() before adding any records.
	Error Open(std::string filename, OpenMode mode, ShapeType typeOfNew = ShapeTypeNull);

	void Close();                      // Close the file. Any pending write operations will be flushed.
	void SetFeatureType(ShapeType ft); // Set the feature type of a new shapefile.
	void FlushAndSleep();              // Flush all pending writes, and release any temporary memory buffers.

	geom3d::BBox3d GetBoundsXYZ() { return Bounds; }

	void GetBoundsM(double& min, double& max) {
		min = BoundsMmin;
		max = BoundsMmax;
	}

	int       GetFeatureCount() { return FeatureCount; }
	ShapeType GetFeatureType() { return Type; }

	// Return the base filename, which is a fully specified path, but has no extension (ie no .shp nor .shx).
	std::string GetBaseFilename() { return BaseFilename; }

	bool CanWrite() { return IsOpenForWrite() && Type != ShapeTypeNull; }
	bool IsOpenForWrite() { return IsOpen() && !IsOpenReadOnly(); }
	bool IsOpenReadOnly() { return IsOpen() && ModeOpen == OpenModeRead; }
	bool IsOpen() { return ShpFile.IsOpen(); }

	Error AddNullFeature();
	Error AddPoint(const gfx::Vec3d& p, double m = 0);
	Error AddMultiPoint(int n, const gfx::Vec3d* p, const double* m = NULL);
	Error AddPolyline(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m = NULL);
	Error AddPolygon(int nParts, const int* partStarts, int nPoints, const gfx::Vec3d* p, const double* m = NULL);

	Error ReadIsFeatureNull(int index, bool& isNull);
	Error ReadPoint(int index, bool& isNull, gfx::Vec3d& p, double* m = NULL);
	Error ReadMultiPointHead(int index, bool& isNull, uint32_t& n, geom3d::BBox3d& bb, double* mRange = NULL);
	Error ReadMultiPoints(int index, gfx::Vec3d* p, double* m = NULL);

	/* Read Polylines and Polygons
	Use the following pair of functions to read polygons and polylines.
	Note that the 'std::vector<bool> closed' parameter that ReadPolyHead returns needs to be fed
	back into ReadPoly. This is in order to make the system stateless without introducing
	any duplicate computation.

	ReadPoly will return false iff it detect corrupt data.

	CAVEAT! In order to tolerate corruption of type $ISSUE-001, you will need to always allocate space for at least one more part
	than the number returned here. Sorry!
	*/
	Error ReadPolyHead(int index, bool& isNull, uint32_t& nParts, std::vector<uint8_t>& closed, uint32_t& nPoints, geom3d::BBox3d& bb, double* mRange = NULL);
	Error ReadPoly(int index, int* partStarts, const std::vector<uint8_t>& closed, gfx::Vec3d* p, double* m = NULL);

	int Debug(int a, int b);

	// If true, then we ignore certain errors that I have, over the years, discovered in bad shapefiles.
	bool Tolerant = true;

	// If a polyline has zero parts or zero vertices, just treat it as null
	bool TolerateEmptyGeometryAsNull = true;

protected:
	std::string    BaseFilename;
	ShapeType      Type   = ShapeTypeNull;
	bool           Strict = true;
	os::MMapFile   ShpFile;
	os::MMapFile   ShxFile;
	OpenMode       ModeOpen     = OpenModeRead;
	int            FeatureCount = 0;
	bool           BoundsDirty  = true;
	geom3d::BBox3d Bounds;
	double         BoundsMmin = DBL_MAX;
	double         BoundsMmax = -DBL_MAX;

	static const int        TempVecSize = 128;
	std::vector<int>        TempInt;
	std::vector<gfx::Vec3d> TempVec3;

	Error ReadHeader();
	Error WriteNullHeaders();
	Error WriteHeaders();
	void  RecalcBounds();

	geom3d::BBox3d GetBounds(int n, const gfx::Vec3d* p);
	ShRange        GetBounds(int n, const double* m);
	int            Count(int n, const uint8_t* b);
	gfx::Vec3d*    CopyToTemp(int n, const gfx::Vec3d* p, std::vector<gfx::Vec3d>& alternate);
	void           Reverse(int n, gfx::Vec3d* p);

	int PartSize(int part, int nParts, const int* partStarts, int nPoints) {
		int end = part == nParts - 1 ? nPoints : partStarts[part + 1];
		return end - partStarts[part];
	}

	int     FindNewFeatureIndex();
	int64_t FindNewFeaturePosition(size_t contentBytes);
	Error   ReadIndex(int index, RecordIndex& ri);
	Error   WriteIndex(int index, const RecordIndex& ri);
	Error   WriteIndex(int index, size_t position, int contentBytes);
	int64_t FeaturePosition(int index);
	size_t  FeatureLength(int index);

	// Add a feature and write it's header, leaving the SHP file position immediately after the record header.
	// A call to AddFeature() must immediately be followed by a call to one of the Add__ functions,
	// such as AddPoint, AddPolygon, etc.
	Error AddFeature(int contentBytes);

	Error CleanAndWritePolygon(int nParts, const int* partStarts, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb);
	Error CloseAndWritePolyline(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb);
	Error AddPoly(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m);
	Error WriteAdjustedPolyArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const gfx::Vec3d* p, const double* m, const geom3d::BBox3d& bb);

	void AdjustPartStarts(int nParts, int* partStarts, const uint8_t* closed);

	template <int Offset, int Size, class TData>
	Error ReadAdjustedArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const TData* p);

	template <int Offset, int Size, class TData, bool AllowNull>
	Error WriteAdjustedArray(int nParts, const int* partStarts, const uint8_t* closed, int nPoints, const TData* p);

	Error WriteRecordHead(int index_zero_based, int contentlength_bytes);

	// Content-Length calculators.
	// The following set of functions compute the content length of the specified feature.
	// Note that the content length excludes the 8-byte record header also found in the SHP File.
	int BytesForPoint();
	int BytesForMultiPoint(int n);
	int BytesForPoly(int nParts, const uint8_t* closed, int nPoints);
	int BytesForMultiPatch(int n, int m);

	void FixHeadEndian(MainHeader& head);

	void ENDIAN(int& v, bool big);
};

} // namespace shapefile
} // namespace dba
} // namespace imqs