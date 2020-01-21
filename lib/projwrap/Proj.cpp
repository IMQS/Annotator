#include "pch.h"
#include "Proj.h"
#include "epsg.h"

#if defined(_MSC_VER) && !defined(strdup)
#define strdup _strdup
#endif

using namespace std;

namespace imqs {
namespace projwrap {

enum class BuildState : int32_t {
	NotBuilt = 0,
	Building = 1,
	Built    = 2,
};

struct ThreadCache {
	projCtx Ctx     = nullptr;
	int     SrcSRID = 0;
	int     DstSRID = 0;
	projPJ  SrcPJ   = nullptr;
	projPJ  DstPJ   = nullptr;
};

static const int     MaxThreads = 256;
std::atomic<int32_t> NextThreadId;
static ThreadCache   Thread[MaxThreads];

static thread_local int32_t MyThreadId = 0;

struct Global {
	typedef ohash::map<const char*, int, ohash::func_default<const char*>, ohash::getkey_pair_pchar<int>> StrToInt;

	std::mutex                   Lock;
	ohash::map<int, const char*> CustomSRID_2_Proj;
	StrToInt                     Proj_2_CustomSRID;
	int                          NextCustomSRID = -2; // Start at -2, because -1 often looks like an invalid value
	StrToInt                     Proj_2_EPSG;         // Map from proj4 definition string to EPSG code
	std::atomic<int32_t>         Proj_2_EPSG_State;

	std::mutex LockParseWkt; // Guards access to GDAL functions for parsing WKT. See where it's used for details on why.
};
Global Glob;

static ThreadCache* GetCacheForThread() {
	if (MyThreadId == 0) {
		MyThreadId = NextThreadId++;

		// You must use a pool of threads which gets reused. If your machine truly needs more
		// than 256 threads, then raise MaxThreads.
		IMQS_ASSERT(MyThreadId < MaxThreads);

		Thread[MyThreadId].Ctx = pj_ctx_alloc();
	}
	return &Thread[MyThreadId];
}

PROJWRAP_API const char* SRIDLookup(int srid) {
	if (srid < 0) {
		std::lock_guard<std::mutex> lock(Glob.Lock);
		return Glob.CustomSRID_2_Proj.get(srid);
	}
	return FindEPSG(srid);
}

ThreadCache* GetConverter(int srcSrid, int dstSrid) {
	auto cache = GetCacheForThread();
	if (cache->SrcSRID == srcSrid && cache->DstSRID == dstSrid)
		return cache;

	if (cache->SrcPJ)
		pj_free(cache->SrcPJ);
	if (cache->DstPJ)
		pj_free(cache->DstPJ);
	cache->SrcPJ = nullptr;
	cache->DstPJ = nullptr;

	const char* srcPlus = SRIDLookup(srcSrid);
	const char* dstPlus = SRIDLookup(dstSrid);

	if (!srcPlus || !dstPlus)
		return nullptr;

	cache->SrcPJ = pj_init_plus_ctx(cache->Ctx, srcPlus);
	cache->DstPJ = pj_init_plus_ctx(cache->Ctx, dstPlus);
	IMQS_ASSERT(cache->SrcPJ);
	IMQS_ASSERT(cache->DstPJ);

	cache->SrcSRID = srcSrid;
	cache->DstSRID = dstSrid;
	return cache;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int InitCount = 0;

PROJWRAP_API void Initialize() {
	InitCount++;
	if (InitCount != 1)
		return;
	NextThreadId           = 1;
	Glob.Proj_2_EPSG_State = (int32_t) BuildState::NotBuilt;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6001) // MSVC /analyze thinks we're using uninitialized memory when cleaning up Glob.Proj_2_CustomSRID
#endif
PROJWRAP_API void Shutdown() {
	InitCount--;
	if (InitCount != 0)
		return;
	for (size_t i = 0; i < MaxThreads; i++) {
		if (Thread[i].SrcPJ)
			pj_free(Thread[i].SrcPJ);
		if (Thread[i].DstPJ)
			pj_free(Thread[i].DstPJ);
		if (Thread[i].Ctx)
			pj_ctx_free(Thread[i].Ctx);
		Thread[i] = ThreadCache();
	}
	for (auto& p : Glob.Proj_2_CustomSRID) {
		free((void*) p.first);
	}
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Assume latlon coordinates are in decimal degrees
PROJWRAP_API bool Convert(int srcSrid, int dstSrid, size_t nvertex, size_t stride, double* x, double* y, double* z, uint32_t convertFlags) {
	if (srcSrid == dstSrid)
		return true;

	auto cv = GetConverter(srcSrid, dstSrid);
	if (!cv)
		return false;

	if (pj_is_latlong(cv->SrcPJ)) {
		double* dx = x;
		double* dy = y;
		for (size_t i = 0; i < nvertex; i++, dx += stride, dy += stride) {
			*dx *= IMQS_PI / 180.0;
			*dy *= IMQS_PI / 180.0;
		}
	}

	if (pj_transform(cv->SrcPJ, cv->DstPJ, (long) nvertex, (int) stride, x, y, z) != 0)
		return false;

	if (pj_is_latlong(cv->DstPJ)) {
		double* dx = x;
		double* dy = y;
		for (size_t i = 0; i < nvertex; i++, dx += stride, dy += stride) {
			*dx *= 180.0 / IMQS_PI;
			*dy *= 180.0 / IMQS_PI;
		}
	}

	if (!!(convertFlags & ConvertFlagCheckNaN)) {
		const double* sx = x;
		const double* sy = y;
		const double* sz = z;
		for (size_t i = 0; i < nvertex; i++) {
			if (!(math::IsFinite(*sx) && math::IsFinite(*sy)))
				return false;
			sx += stride;
			sy += stride;
			if (sz) {
				if (!math::IsFinite(*sz))
					return false;
				sz += stride;
			}
		}
	}

	return true;
}

PROJWRAP_API int RegisterCustomSRID(const char* proj) {
	std::lock_guard<std::mutex> lock(Glob.Lock);
	int                         srid = Glob.Proj_2_CustomSRID.get(proj);
	if (srid != 0)
		return srid;
	srid             = Glob.NextCustomSRID--;
	const char* copy = strdup(proj);
	IMQS_ASSERT(copy);
	Glob.Proj_2_CustomSRID.insert(copy, srid);
	Glob.CustomSRID_2_Proj.insert(srid, copy);
	return srid;
}

static void EnsureProj_2_EPSG_IsBuilt() {
	int build = (int) BuildState::NotBuilt;
	if (Glob.Proj_2_EPSG_State.compare_exchange_strong(build, (int) BuildState::Building)) {
		// we got the ticket to build it
		size_t n = EPSG_Size();
		for (size_t i = 0; i < n; i++) {
			const char* proj4;
			int         srid;
			EPSG_Get(i, srid, proj4);
			Glob.Proj_2_EPSG.insert(proj4, srid);
		}
		Glob.Proj_2_EPSG_State = (int) BuildState::Built;
		return;
	} else if (build == (int) BuildState::Built) {
		// already built
		return;
	}

	// wait until it is built
	while (Glob.Proj_2_EPSG_State != (int) BuildState::Built) {
		os::Sleep(time::Millisecond);
	}
}

PROJWRAP_API int Proj2SRID(const char* proj) {
	EnsureProj_2_EPSG_IsBuilt();
	int srid = Glob.Proj_2_EPSG.get(proj);
	if (srid != 0)
		return srid;
	return RegisterCustomSRID(proj);
}

PROJWRAP_API Error ParseWKT(const char* wkt, std::string* proj4, int* srid) {
#ifdef IMQS_PROJWRAP_EXCLUDE_GDAL
	return Error("projwrap has been compiled without GDAL support");
#else
	// Why do we need to take this mutex?
	// On 15 July 2019, there was a crash on demo.imqs.co.za (Windows).
	// The crash was caused by the windows kernel telling us that we were trying
	// to unlock a mutex that we don't own.
	// This was the stack trace:
	//     ntdll.dll!RtlRaiseStatus()
	//     ntdll.dll!RtlpNotOwnerCriticalSection()
	//     ntdll.dll!RtlLeaveCriticalSection()
	//     gdal201.dll!00007ff96c65bf6e()
	//     gdal201.dll!00007ff96c65be50()
	//     gdal201.dll!00007ff96c65c978()
	//     gdal201.dll!00007ff96c642927()
	//     gdal201.dll!00007ff96d527b34()
	//     projwrap.dll!imqs::projwrap::ParseWKT(const char * wkt, std::basic_string<char,std::char_traits<char>,std::allocator<char> > * proj4, int * srid)	C++
	//     MapServer.exe!imqs::maps::bcf::BCFImage::GetGeo(imqs::maps::GeoAffine & affine, std::basic_string<char,std::char_traits<char>,std::allocator<char> > & proj4)	C++
	//     MapServer.exe!imqs::maps::TileReprojector::GenerateBlockFromImage(imqs::hash::Sig16 sigBase, int targetSRID, imqs::maps::bcf::BCFImage * srcImage, __int64 z, imqs::geom2d::BBox2<__int64> genBlock, mapnik::image<mapnik::rgba8_t> & outRGBA)	C++
	//     MapServer.exe!imqs::maps::style::ConvertToMapnik_Raster(imqs::maps::style::RenderContext & cx, imqs::maps::style::ToMapnikLayerContext & layerCX, mapnik::layer & l, mapnik::Map & m)	C++
	//     MapServer.exe!imqs::maps::VectorTileServer::AddRasterLayer(imqs::maps::style::RenderContext & cx, unsigned __int64 layerIndex, const imqs::maps::style::Layer & srcLayer, mapnik::Map & m)	C++
	//     MapServer.exe!imqs::maps::VectorTileServer::RenderLayersToMapnik(imqs::maps::style::RenderContext & cx, imqs::maps::style::Theme & theme, unsigned __int64 themeIndex, mapnik::Map & m)	C++
	//     MapServer.exe!imqs::maps::VectorTileServer::SendTile(phttp::Response & w, std::shared_ptr<phttp::Request> request, int srid, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & themeName, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & requestedVersion, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & imgType, __int64 z, __int64 x, __int64 y)	C++
	//     MapServer.exe!imqs::maps::TileServer::SendTile(phttp::Response & w, std::shared_ptr<phttp::Request> request, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & theme, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & version, const std::basic_string<char,std::char_traits<char>,std::allocator<char> > & imgType, __int64 z, __int64 x, __int64 y)	C++
	//
	// As an attempted workaround, I have added this mutex.
	// Who knows... maybe this issue has already been fixed in later GDAL versions, or on Linux.
	// I didn't take the time to try and reproduce the issue, but if it is solved by this mutex,
	// then I would expect it to be fairly easy to reproduce.
	//

	OGRSpatialReference srs;
	string              p4;

	{
		std::lock_guard<std::mutex> lock(Glob.LockParseWkt);
		//auto  ogrErr = srs.importFromESRI(&src); // This crashes, reading low memory. When I was debugging it, I didn't have debug symbols for GDAL, so not sure why the crash.
#if GDAL_VERSION_NUM < 2030000
		char* src    = const_cast<char*>(wkt);
		auto  ogrErr = srs.importFromWkt(&src);
#else
		auto ogrErr = srs.importFromWkt(&wkt);
#endif

		if (ogrErr != OGRERR_NONE)
			return Error::Fmt("Error decoding BCF SRS: %v", ogrErr);

		srs.AutoIdentifyEPSG();

		char* p4z = nullptr;
		ogrErr    = srs.exportToProj4(&p4z);
		if (ogrErr != OGRERR_NONE)
			return Error::Fmt("Error translating BCF SRS to proj4: %v", ogrErr);
		p4 = p4z;
		CPLFree(p4z);
	}
	p4 = strings::Trim(p4);

	if (proj4)
		*proj4 = p4;

	if (srid)
		*srid = Proj2SRID(p4.c_str());

	return Error();
#endif
}

} // namespace projwrap
} // namespace imqs
