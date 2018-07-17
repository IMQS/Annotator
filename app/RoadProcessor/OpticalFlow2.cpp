#include "pch.h"
#include "OpticalFlow2.h"

using namespace std;
using namespace imqs::gfx;

namespace imqs {
namespace roadproc {

// Compute sum of absolute differences
static int32_t DiffSum(const Image& img1, const Image& img2, Rect32 rect1, Rect32 rect2) {
	IMQS_ASSERT(img1.Format == ImageFormat::Gray);
	IMQS_ASSERT(img2.Format == ImageFormat::Gray);
	IMQS_ASSERT(rect1.Width() == rect2.Width());
	IMQS_ASSERT(rect1.Height() == rect2.Height());
	int     w   = rect1.Width();
	int     h   = rect1.Height();
	int32_t sum = 0;
	for (int y = 0; y < h; y++) {
		const uint8_t* p1 = img1.At(rect1.x1, rect1.y1 + y);
		const uint8_t* p2 = img2.At(rect2.x1, rect2.y1 + y);
		for (int x = 0; x < w; x++) {
			int d = (int) p1[x] - (int) p2[x];
			sum += abs(d);
		}
	}
	return sum;
}

static double ImageStdDev(const Image& img, Rect32 crop) {
	uint32_t sum = 0;
	for (int y = crop.y1; y < crop.y2; y++) {
		const uint8_t* p = img.At(crop.x1, y);
		for (int x = crop.x1; x < crop.x2; x++, p++)
			sum += *p;
	}
	sum /= crop.Width() * crop.Height();
	uint32_t var = 0;
	for (int y = crop.y1; y < crop.y2; y++) {
		const uint8_t* p = img.At(crop.x1, y);
		for (int x = crop.x1; x < crop.x2; x++, p++) {
			var += ((uint32_t) *p - sum) * ((uint32_t) *p - sum);
		}
	}
	double dvar = (double) var;
	dvar        = sqrt(dvar / (double) (crop.Width() * crop.Height()));
	return dvar;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

OpticalFlow2::OpticalFlow2() {
}

static Rect32 MakeBoxAroundPoint(int x, int y, int radius) {
	return Rect32(x - radius, y - radius, x + radius, y + radius);
}

DeltaGrid::DeltaGrid() {
}

DeltaGrid::DeltaGrid(DeltaGrid& g) {
	*this = g;
}

DeltaGrid& DeltaGrid::operator=(const DeltaGrid& g) {
	if (this != &g) {
		Alloc(g.Width, g.Height);
		memcpy(Delta, g.Delta, sizeof(Vec2f) * Width * Height);
		memcpy(Valid, g.Valid, sizeof(bool) * Width * Height);
	}
	return *this;
}

DeltaGrid::~DeltaGrid() {
	free(Delta);
	free(Valid);
}

void DeltaGrid::Alloc(int w, int h) {
	if (w != Width || h != Height) {
		free(Delta);
		free(Valid);
		Width  = w;
		Height = h;
		Delta  = (gfx::Vec2f*) imqs_malloc_or_die(w * h * sizeof(gfx::Vec2f));
		Valid  = (bool*) imqs_malloc_or_die(w * h * sizeof(bool));
	}
}

static void CopyMeshToDelta(const Mesh& m, Rect32 rect, DeltaGrid& g, Vec2f norm) {
	g.Alloc(rect.Width(), rect.Height());
	//Vec2f norm = m.At(rect.x1, rect.y1).Pos - m.At(rect.x1, rect.y1).UV;
	for (int y = rect.y1; y < rect.y2; y++) {
		for (int x = rect.x1; x < rect.x2; x++) {
			g.At(x - rect.x1, y - rect.y1)      = m.At(x, y).Pos - m.At(x, y).UV - norm;
			g.IsValid(x - rect.x1, y - rect.y1) = m.At(x, y).IsValid;
		}
	}
}

static void CopyDeltaToMesh(const DeltaGrid& g, Mesh& m, Rect32 rect, Vec2f norm) {
	//Vec2f norm = m.At(rect.x1, rect.y1).Pos - m.At(rect.x1, rect.y1).UV;

	for (int y = rect.y1; y < rect.y2; y++) {
		for (int x = rect.x1; x < rect.x2; x++) {
			m.At(x, y).Pos     = g.At(x - rect.x1, y - rect.y1) + m.At(x, y).UV + norm;
			m.At(x, y).IsValid = g.IsValid(x - rect.x1, y - rect.y1);
		}
	}
}

static bool CompareVec2X(const Vec2f& a, const Vec2f& b) {
	return a.x < b.x;
}

static bool CompareVec2Y(const Vec2f& a, const Vec2f& b) {
	return a.y < b.y;
}

struct ClusterStat {
	Vec2f Center    = Vec2f(0, 0);
	int   Count     = 0;
	float Tightness = 0;
	// typical decent values for tightness are around 5, so an epsilon of 0.1 feels about right
	float Alpha() const { return (float) Count / (Tightness + 0.1); }
};

static void ComputeClusterStats(const vector<cv::Point2f>& allCV, const vector<int>& cluster, const vector<cv::Point2f>& clusterCenters, vector<ClusterStat>& stats) {
	stats.resize(clusterCenters.size());
	for (size_t i = 0; i < clusterCenters.size(); i++) {
		stats[i]          = ClusterStat();
		stats[i].Center.x = clusterCenters[i].x;
		stats[i].Center.y = clusterCenters[i].y;
	}

	for (size_t i = 0; i < cluster.size(); i++) {
		int   c  = cluster[i];
		Vec2f cv = Vec2f(clusterCenters[c].x, clusterCenters[c].y);
		stats[c].Count++;
		stats[c].Tightness += cv.distance2D(Vec2f(allCV[i].x, allCV[i].y));
	}
	for (size_t i = 0; i < clusterCenters.size(); i++) {
		stats[i].Tightness /= (float) stats[i].Count;
	}
}

static void DumpKMeans(vector<ClusterStat> clusters) {
	tsf::print("%v clusters:\n", clusters.size());
	for (auto c : clusters) {
		tsf::print("  Alpha: %4.2f Count: %2d, Tightness: %.1f, Center: %5.1f, %5.1f\n", c.Alpha(), c.Count, c.Tightness, c.Center.x, c.Center.y);
	}
}

// Returns the number of points that were replaced with a filtered replica
// I tried a smaller filter size of 3x3, but it easily introduces noisy samples
// into the final dataset. This is partly due to the sloppy metric "maxGlobalDistance".
static int MedianFilter(int pass, DeltaGrid& g, bool& hasMassiveOutliers) {
	DeltaGrid gnew                  = g;
	hasMassiveOutliers              = false;
	const int filterRadius          = 2;
	const int filterSize            = 2 * filterRadius + 1;
	const int filterSizeSQ          = filterSize * filterSize;
	float     maxDistance           = 2;  // If sample is more than maxDistance from local filter estimate, then it is filtered
	float     maxGlobalDistanceSoft = 15; // If sample is more than maxGlobalDistanceSoft from global distance estimate, then it is filtered
	float     maxGlobalDistanceHard = 25; // If sample is more than maxGlobalDistanceHard from global distance estimate, then it is replaced with the global estimate
	int       nrep                  = 0;

	// compute global median, so that we can throw away extreme outliers
	Vec2f               globalEstimate;
	vector<Vec2f>       all;
	vector<cv::Point2f> allCV;
	all.reserve(g.Width * g.Height);
	for (int y = 0; y < g.Height; y++) {
		for (int x = 0; x < g.Width; x++) {
			Vec2f p = g.At(x, y);
			all.push_back(p);
			allCV.push_back(cv::Point2f(p.x, p.y));
		}
	}
	pdqsort(all.begin(), all.end(), CompareVec2X);
	globalEstimate.x = all[all.size() / 2].x;
	pdqsort(all.begin(), all.end(), CompareVec2Y);
	globalEstimate.y = all[all.size() / 2].y;

	// after the first pass, we can skip this expensive step
	if (pass == 0) {
		bool                debugKMeans = false;
		vector<int>         icluster;
		vector<cv::Point2f> clusterCenters;
		vector<float>       clusterTightness;
		vector<ClusterStat> clusters;
		cv::TermCriteria    term(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 10, 1.0);
		if (debugKMeans) {
			for (int ncluster = 2; ncluster <= 6; ncluster++) {
				cv::kmeans(allCV, ncluster, icluster, term, 1, 0, clusterCenters);
				ComputeClusterStats(allCV, icluster, clusterCenters, clusters);
				DumpKMeans(clusters);
			}
			exit(0);
		} else {
			// From one video that I looked at, 5 seems to be a sweet spot
			int nclusters = 5;
			cv::kmeans(allCV, nclusters, icluster, term, 3, 0, clusterCenters);
			ComputeClusterStats(allCV, icluster, clusterCenters, clusters);
			float       maxAlpha = 0;
			ClusterStat best;
			for (const auto& c : clusters) {
				// This constant of 5% is a thumbsuck, given the typical number of points that we align
				int minCount = int(0.05 * (double) allCV.size());
				if (c.Count > minCount && c.Alpha() > maxAlpha) {
					maxAlpha = c.Alpha();
					best     = c;
				}
			}
			globalEstimate = best.Center;
		}
	}
	//tsf::print("%3d. %5.1f,%5.1f\n", pass, globalEstimate.x, globalEstimate.y);

	// replace obvious outliers with the global estimate
	for (int y = 0; y < g.Height; y++) {
		for (int x = 0; x < g.Width; x++) {
			Vec2f pp = g.At(x, y);
			float d  = g.At(x, y).distance(globalEstimate);
			if (d > maxGlobalDistanceHard) {
				nrep++;
				hasMassiveOutliers = true;
				g.At(x, y)         = globalEstimate;
				gnew.At(x, y)      = globalEstimate;
			}
		}
	}

	float samplesX[filterSizeSQ];
	float samplesY[filterSizeSQ];
	float samplesCleanX[filterSizeSQ];
	float samplesCleanY[filterSizeSQ];
	for (int y = 0; y < g.Height; y++) {
		for (int x = 0; x < g.Width; x++) {
			int i = 0;
			for (int yf = y - filterRadius; yf <= y + filterRadius; yf++) {
				if (yf < 0)
					continue;
				if (yf >= g.Height)
					break;
				for (int xf = x - filterRadius; xf <= x + filterRadius; xf++) {
					if (xf < 0)
						continue;
					if (xf >= g.Width)
						break;
					samplesX[i] = g.At(xf, yf).x;
					samplesY[i] = g.At(xf, yf).y;
					i++;
				}
			}
			pdqsort(samplesX, samplesX + i);
			pdqsort(samplesY, samplesY + i);
			if (fabs(g.At(x, y).x - samplesX[i / 2]) > maxDistance ||
			    fabs(g.At(x, y).y - samplesY[i / 2]) > maxDistance ||
			    fabs(g.At(x, y).x - globalEstimate.x) > maxGlobalDistanceSoft ||
			    fabs(g.At(x, y).y - globalEstimate.y) > maxGlobalDistanceSoft) {
				nrep++;
				// Build up a small set of "clean" samples, which are those that are close to the median.
				// We don't want to be filtering based on dirty samples.
				int iClean = 0;
				for (int j = 0; j < i; j++) {
					if (fabs(samplesX[j] - samplesX[i / 2]) <= maxDistance &&
					    fabs(samplesY[j] - samplesY[i / 2]) <= maxDistance &&
					    fabs(samplesX[j] - globalEstimate.x) <= maxGlobalDistanceSoft &&
					    fabs(samplesY[j] - globalEstimate.y) <= maxGlobalDistanceSoft) {
						samplesCleanX[iClean] = samplesX[j];
						samplesCleanY[iClean] = samplesY[j];
						iClean++;
					}
				}
				if (iClean >= 2) {
					gnew.At(x, y).x = samplesCleanX[iClean / 2];
					gnew.At(x, y).y = samplesCleanY[iClean / 2];
				}
			}
		}
	}
	if (nrep != 0)
		g = gnew;
	return nrep;
}

static void BlurInvalid(DeltaGrid& g, int passes) {
	DeltaGrid   tmp    = g;
	const int   kernel = 1;
	const int   size   = 2 * kernel + 1;
	const float scale  = 1.0f / (float) (size * size);

	DeltaGrid* g1 = &g;
	DeltaGrid* g2 = &tmp;
	for (int pass = 0; pass < passes; pass++) {
		for (int y = kernel; y < g1->Height - kernel; y++) {
			for (int x = kernel; x < g1->Width - kernel; x++) {
				if (g1->IsValid(x, y))
					continue;
				Vec2f avg(0, 0);
				for (int dy = -kernel; dy <= kernel; dy++) {
					for (int dx = -kernel; dx <= kernel; dx++) {
						avg += scale * g1->At(x + dx, y + dy);
					}
				}
				g2->At(x, y) = avg;
			}
		}
		std::swap(g1, g2);
	}
	if (passes % 2 == 1)
		g = tmp;
}

// This thing exists solely to bring sanity to the 4 dimensional array accesses that we do here
struct WarpScore {
	int* Sum = nullptr;
	int  MW  = 0;
	int  MH  = 0;
	int  MDX = 0;
	int  MDY = 0;
	int  StrideLev1;
	int  StrideLev2;
	WarpScore(int w, int h, int mdx, int mdy) {
		MW         = w;
		MH         = h;
		MDX        = mdx;
		MDY        = mdy;
		StrideLev1 = MDX * MDY;
		StrideLev2 = StrideLev1 * MW;
		Sum        = new int[h * StrideLev2]();
	}
	~WarpScore() {
		delete[] Sum;
	}
	int& At(int mx, int my, int dx, int dy) {
		return Sum[my * StrideLev2 + mx * StrideLev1 + dy * MDX + dx];
	}
	void BestGlobalDelta(int& dx, int& dy) {
		int bestSum = INT32_MAX;
		for (int y = 0; y < MDY; y++) {
			for (int x = 0; x < MDX; x++) {
				int sum = 0;
				for (int my = 0; my < MH; my++) {
					for (int mx = 0; mx < MW; mx++) {
						sum += At(mx, my, x, y);
					}
				}
				if (sum < bestSum) {
					bestSum = sum;
					dx      = x;
					dy      = y;
				}
			}
		}
	}
};

int FixElementsTooFarFromGlobalBest(Mesh& warpMesh, Rect32 warpMeshValidRect, Vec2f bias, Point32 bestDelta) {
	Vec2f bestDeltaF((float) bestDelta.x, (float) bestDelta.y);
	float maxDistSQ = 20 * 20;

	int nfixed = 0;
	for (int cy = warpMeshValidRect.y1; cy < warpMeshValidRect.y2; cy++) {
		for (int cx = warpMeshValidRect.x1; cx < warpMeshValidRect.x2; cx++) {
			Vec2f raw   = warpMesh.At(cx, cy).Pos;
			Vec2f delta = warpMesh.At(cx, cy).Pos - bias;
			if (delta.distance2D(bestDeltaF) > maxDistSQ) {
				nfixed++;
				warpMesh.At(cx, cy).Pos = bias + bestDeltaF;
			}
		}
	}
	return nfixed;
}

// We compute the transformed mesh of warpImg, so that it aligns to stableImg
// All pixels in stableImg are expected to be defined, but we allow blank (zero alpha) pixels
// in warpImg, and we make sure that we don't try to align any grid cells that have
// one or more blank pixels inside them.
void OpticalFlow2::Frame(Mesh& warpMesh, Frustum warpFrustum, gfx::Image& warpImg, gfx::Image& stableImg, gfx::Vec2f& bias) {
	int frameNumber = HistorySize++;

	IMQS_ASSERT(warpImg.Width == warpFrustum.Width); // just sanity check
	Vec2f warpFrustumPoly[4];
	// subtle things:
	// shrink X by 1, to ensure we don't touch any black pixels outside the frustum
	// expand Y by 0.1, so that our bottom-most vertices, which are built to butt up
	// against the bottom, are considered inside.
	warpFrustum.Polygon(warpFrustumPoly, -1, 0.1);

	Image warpImgG   = warpImg.AsType(ImageFormat::Gray);
	Image stableImgG = stableImg.AsType(ImageFormat::Gray);

	//warpImgG.SaveFile("warpImgG.jpeg");
	//stableImgG.SaveFile("stableImgG.jpeg");

	int minHSearch = -StableHSearchRange;
	int maxHSearch = StableHSearchRange;
	int minVSearch = -StableVSearchRange;
	int maxVSearch = StableVSearchRange;

	// Compute initial bias, which is the (guessed) vector that takes the top-left corner of warpMesh,
	// and moves it to the top-left corner of stableImg.
	if (frameNumber == 0) {
		// warpImg is a raw 'flat' image, which is typically large.
		// stableImg is a small extract from the bottom middle of the first flat image.
		// We assume that stableImg lies in the center of warpImg, and at the bottom.
		// Also, we assume zero motion.
		bias.x = (warpImg.Width - stableImg.Width) / 2;
		//bias.y     = warpImg.Height - stableImg.Height + AbsMaxVSearch;
		bias.y     = warpImg.Height - stableImg.Height;
		bias       = -bias; // get it from warp -> stable
		minHSearch = AbsMinHSearch;
		maxHSearch = AbsMaxHSearch;
		minVSearch = AbsMinVSearch;
		maxVSearch = AbsMaxVSearch;
	}

	if (frameNumber == 1) {
		int abc = 123;
	}

	// We're aligning on whole pixels here
	bias.x = floor(bias.x + 0.5f);
	bias.y = floor(bias.y + 0.5f);

	// apply the bias to the entire mesh
	for (int i = 0; i < warpMesh.Count; i++)
		warpMesh.Vertices[i].Pos += bias;

	// Decide which grid cells to attempt alignment on
	for (int y = 0; y < warpMesh.Height; y++) {
		for (int x = 0; x < warpMesh.Width; x++) {
			Vec2f p  = warpMesh.At(x, y).Pos;
			Vec2f p1 = p - Vec2f(MatchRadius, MatchRadius) + Vec2f(minHSearch, minVSearch);
			Vec2f p2 = p + Vec2f(MatchRadius, MatchRadius) + Vec2f(maxHSearch, maxVSearch);
			if (p1.x < 0 || p1.y < 0 || p2.x > (float) stableImg.Width || p2.y > (float) stableImg.Height) {
				// search for matching cell would reach outside of stable image
				warpMesh.At(x, y).IsValid = false;
				continue;
			}
			p  = warpMesh.At(x, y).UV;
			p1 = p - Vec2f(MatchRadius, MatchRadius);
			p2 = p + Vec2f(MatchRadius, MatchRadius);
			if (!geom2d::PtInsidePoly(p1.x, p1.y, 4, &warpFrustumPoly[0].x, 2) || !geom2d::PtInsidePoly(p2.x, p2.y, 4, &warpFrustumPoly[0].x, 2)) {
				// warp cell has pixels that are outside of the frustum (ie inside the triangular black regions on the bottom left/right of the flattened image)
				warpMesh.At(x, y).IsValid = false;
				continue;
			}
		}
	}

	// compute a box around all of the warp grid cells that are valid
	vector<Point32> validCells;
	Rect32          warpMeshValidRect = Rect32::Inverted();
	for (int y = 0; y < warpMesh.Height; y++) {
		for (int x = 0; x < warpMesh.Width; x++) {
			if (warpMesh.At(x, y).IsValid) {
				validCells.emplace_back(x, y);
				warpMeshValidRect.ExpandToFit(x, y);
			}
		}
	}
	warpMeshValidRect.x2++;
	warpMeshValidRect.y2++;
	IMQS_ASSERT(warpMeshValidRect.x1 >= 0);
	IMQS_ASSERT(warpMeshValidRect.y1 >= 0);
	IMQS_ASSERT(warpMeshValidRect.x2 <= warpMesh.Width);
	IMQS_ASSERT(warpMeshValidRect.y2 <= warpMesh.Height);

	//warpMesh.PrintValid();

	// Run multiple passes, where at the end of each pass, we perform maximum likelihood filtering, and then
	// on the subsequent pass, restrict the search window to a much smaller region.

	bool debugMedianFilter = false;
	if (debugMedianFilter)
		tsf::print("------------------------------------------------------------------------------------\n");

	//warpMesh.PrintDeltaPos(warpMeshValidRect, bias);

	// This seems to be a bad idea
	bool doGlobalFit  = false;
	bool useGlobalFit = false;
	if (doGlobalFit) {
		int       dyMin = minVSearch;
		int       dyMax = maxVSearch;
		int       dxMin = minHSearch;
		int       dxMax = maxHSearch;
		int       mW    = warpMeshValidRect.Width();
		int       mH    = warpMeshValidRect.Height();
		WarpScore scores(mW, mH, 1 + dxMax - dxMin, 1 + dyMax - dyMin);
		for (auto& c : validCells) {
			Vec2f  cSrc  = warpMesh.At(c.x, c.y).UV;
			Vec2f  cDst  = warpMesh.At(c.x, c.y).Pos;
			Rect32 rect1 = MakeBoxAroundPoint((int) cSrc.x, (int) cSrc.y, MatchRadius);
			Rect32 rect2 = MakeBoxAroundPoint((int) cDst.x, (int) cDst.y, MatchRadius);
			for (int dy = dyMin; dy <= dyMax; dy++) {
				for (int dx = dxMin; dx <= dxMax; dx++) {
					Rect32 r2 = rect2;
					r2.Offset(dx, dy);
					if (r2.x1 < 0 || r2.y1 < 0 || r2.x2 > stableImgG.Width || r2.y2 > stableImgG.Height) {
						// skip invalid rectangle which is outside of stableImg
						continue;
					}
					scores.At(c.x - warpMeshValidRect.x1, c.y - warpMeshValidRect.y1, dx - dxMin, dy - dyMin) = DiffSum(warpImgG, stableImgG, rect1, r2);
				}
			}
		}
		if (debugMedianFilter)
			warpMesh.PrintDeltaPos(warpMeshValidRect, bias);

		Point32 bestGlobal;
		scores.BestGlobalDelta(bestGlobal.x, bestGlobal.y);
		bestGlobal.x += dxMin;
		bestGlobal.y += dyMin;
		if (useGlobalFit) {
			Vec2f bestGlobalF((float) bestGlobal.x, (float) bestGlobal.y);
			for (auto& c : validCells) {
				Vec2f raw = warpMesh.At(c.x, c.y).Pos;
				warpMesh.At(c.x, c.y).Pos += bestGlobalF;
			}
			minVSearch = max(minVSearch, -20);
			maxVSearch = min(maxVSearch, 20);
			minHSearch = max(minHSearch, -20);
			maxHSearch = min(maxHSearch, 20);
		}

		tsf::print("Best Global: %v, %v\n", bestGlobal.x, bestGlobal.y);
		if (debugMedianFilter) {
			warpMesh.PrintDeltaPos(warpMeshValidRect, bias);
		}
	}

	bool hasMassiveOutliers = true;
	int  maxPass            = 2;
	for (int pass = 0; pass < maxPass; pass++) {
		//tsf::print("Flow pass %v\n", pass);
		int dyMin = minVSearch;
		int dyMax = maxVSearch;
		int dxMin = minHSearch;
		int dxMax = maxHSearch;
		if (pass == 1) {
			// refinement after filtering
			//tsf::print("Fine adjustment pass\n");
			int fineAdjust = hasMassiveOutliers ? 8 : 2;
			dyMin          = -fineAdjust;
			dyMax          = fineAdjust;
			dxMin          = -fineAdjust;
			dxMax          = fineAdjust;
		}
		for (auto& c : validCells) {
			//Vec2f  cSrc    = warpMesh.UVimg(warpImg.Width, warpImg.Height, c.x, c.y);
			Vec2f  cSrc    = warpMesh.At(c.x, c.y).UV;
			Vec2f  cDst    = warpMesh.At(c.x, c.y).Pos;
			Rect32 rect1   = MakeBoxAroundPoint((int) cSrc.x, (int) cSrc.y, MatchRadius);
			Rect32 rect2   = MakeBoxAroundPoint((int) cDst.x, (int) cDst.y, MatchRadius);
			int    bestSum = INT32_MAX;
			int    bestDx  = 0;
			int    bestDy  = 0;
			for (int dy = dyMin; dy <= dyMax; dy++) {
				for (int dx = dxMin; dx <= dxMax; dx++) {
					Rect32 r2 = rect2;
					r2.Offset(dx, dy);
					if (r2.x1 < 0 || r2.y1 < 0 || r2.x2 > stableImgG.Width || r2.y2 > stableImgG.Height) {
						// skip invalid rectangle which is outside of stableImg
						continue;
					}
					int32_t sum = DiffSum(warpImgG, stableImgG, rect1, r2);
					if (sum < bestSum) {
						bestSum = sum;
						bestDx  = dx;
						bestDy  = dy;
					}
				}
			}
			warpMesh.At(c.x, c.y).Pos += Vec2f(bestDx, bestDy);
		}
		if (debugMedianFilter)
			warpMesh.PrintDeltaPos(warpMeshValidRect, bias);

		//if (debugMedianFilter)
		//	warpMesh.PrintDeltaPos(warpMeshValidRect, bias);

		int maxFilterPasses = 10;
		//int       maxFilterPasses = 0;
		int       nfilterPasses = 0;
		DeltaGrid dg;
		CopyMeshToDelta(warpMesh, warpMeshValidRect, dg, bias);
		for (int ifilter = 0; ifilter < maxFilterPasses; ifilter++) {
			int nrep = MedianFilter(pass, dg, hasMassiveOutliers);
			if (debugMedianFilter)
				tsf::print("Median Filter replaced %v samples\n", nrep);
			if (nrep == 0)
				break;
			nfilterPasses++;
		}
		CopyDeltaToMesh(dg, warpMesh, warpMeshValidRect, bias);
		if (nfilterPasses == 0) {
			// If we performed no filtering, then a second alignment pass will not change anything
			break;
		}
		if (debugMedianFilter)
			warpMesh.PrintDeltaPos(warpMeshValidRect, bias);
	}

	//warpMesh.DrawFlowImage(warpMeshValidRect, "flow-diagram.png");

	// Fill the remaining invalid cells

	// Start by setting all invalid cells to the average displacement
	Vec2f avgDisp(0, 0);
	float avgScale = 1.0f / (float) validCells.size();
	for (auto& c : validCells)
		avgDisp += avgScale * (warpMesh.At(c.x, c.y).Pos - warpMesh.At(c.x, c.y).UV);

	vector<Point32> remain;
	for (int y = 0; y < warpMesh.Height; y++) {
		for (int x = 0; x < warpMesh.Width; x++) {
			if (!warpMesh.At(x, y).IsValid) {
				warpMesh.At(x, y).Pos = warpMesh.At(x, y).UV + avgDisp;
				remain.emplace_back(x, y);
			}
		}
	}

	// For cells higher up, make their horizontal drift = 0, so that we force the system to always move forward in a straight line
	// UPDATE: make them homogenous in X and Y
	if (true) {
		//warpMesh.DrawFlowImage("flow-diagram-all.png");
		//for (int y = 0; y < warpMeshValidRect.y1; y++) {
		//	for (int x = 0; x < warpMesh.Width; x++) {
		//		warpMesh.At(x, y).Pos.x = warpMesh.At(x, y).UV.x + bias.x;
		//	}
		//}
		// This limit here.. the "-4", is intimately tied to the mesh rect that we stitch inside Stitcher2,
		// right before it calls Rend.DrawMesh().
		for (int y = 0; y < warpMesh.Height - 5; y++) {
			for (int x = 0; x < warpMesh.Width; x++) {
				//warpMesh.At(x, y).Pos.x   = warpMesh.At(x, y).UV.x + bias.x; // lock horizontal drift (bad hack)
				warpMesh.At(x, y).Pos.x   = warpMesh.At(x, y).UV.x + avgDisp.x;
				warpMesh.At(x, y).Pos.y   = warpMesh.At(x, y).UV.y + avgDisp.y;
				warpMesh.At(x, y).IsValid = false;
			}
		}
		//warpMesh.DrawFlowImage("flow-diagram-all.png");
	}

	// smooth the invalid cells (but not too high up, since they aren't aligned at all, so that's just computation wasted)
	{
		DeltaGrid dg;
		int       y1 = warpMesh.Height / 2;
		CopyMeshToDelta(warpMesh, Rect32(0, y1, warpMesh.Width, warpMesh.Height), dg, bias);
		BlurInvalid(dg, 3);
		CopyDeltaToMesh(dg, warpMesh, Rect32(0, y1, warpMesh.Width, warpMesh.Height), bias);
		//warpMesh.DrawFlowImage("flow-diagram-all.png");
	}

	// For the cells at the bottom, clone from vertex above
	for (int x = 0; x < warpMesh.Width; x++) {
		auto delta                              = warpMesh.At(x, warpMesh.Height - 2).Pos - warpMesh.At(x, warpMesh.Height - 2).UV;
		warpMesh.At(x, warpMesh.Height - 1).Pos = warpMesh.At(x, warpMesh.Height - 1).UV + delta;
	}

	// lower the opacity of the invalid cells on the bottom
	for (int y = warpMesh.Height - 2; y < warpMesh.Height; y++) {
		for (int x = 0; x < warpMesh.Width; x++) {
			if (!warpMesh.At(x, y).IsValid)
				warpMesh.At(x, y).Color.a = 0;
		}
	}

	// lower the opacity of ALL cells on the bottom
	// Why? Because the outer edges of the frame are darker, because of the cheap lens
	//for (int y = warpMesh.Height - 2; y < warpMesh.Height; y++) {
	//	for (int x = 0; x < warpMesh.Width; x++) {
	//		warpMesh.At(x, y).Color.a = 0;
	//	}
	//}

	// lower the opacity of all cells outside of the frustum
	// NOTE: This is a poor substitute for aligning the outer triangular edges
	// via optical flow. Right now I want to avoid doing that alignment, because
	// of the expensive cost of reading a MUCH larger portion of the framebuffer
	// on which to perform alignment. I will be even more expensive once we do rotation.
	if (true) {
		for (int y = 0; y < warpMesh.Height; y++) {
			for (int x = 0; x < warpMesh.Width; x++) {
				Vec2f p = warpMesh.At(x, y).UV;
				// a larger buffer generally improves the quality of the stitching, in the absence of doing any optical flow
				// on the periphery.
				int buffer = 200;
				if (!geom2d::PtInsidePoly(p.x - buffer, p.y, 4, &warpFrustumPoly[0].x, 2) || !geom2d::PtInsidePoly(p.x + buffer, p.y, 4, &warpFrustumPoly[0].x, 2))
					warpMesh.At(x, y).Color.a = 0;
			}
		}
	}

	//warpMesh.DrawFlowImage(tsf::fmt("flow-diagram-all-%d.png", frameNumber));

	//warpMesh.PrintSample(warpMesh.Width / 2, warpMesh.Height - 1);

	//while (remain.size() != 0) {
	//	vector<Point32> newRemain;
	//	for (auto& c : remain) {
	//	}
	//}
} // namespace roadproc

// 0: x
// 1: y
// 2: x y
void OpticalFlow2::PrintGrid(int dim) {
	for (int y = 0; y < GridH; y++) {
		for (int x = 0; x < GridW; x++) {
			if (dim == 0 || dim == 2)
				tsf::print("%3.0f ", LastGridEl(x, y).x);
			else
				tsf::print("%4.0f ", LastGridEl(x, y).y);
		}
		if (dim == 2) {
			tsf::print(" | ");
			for (int x = 0; x < GridW; x++)
				tsf::print("%4.0f ", LastGridEl(x, y).y);
		}
		tsf::print("\n");
	}
}

void OpticalFlow2::DrawGrid(Image& img1) {
	Canvas c;
	float  gridSpaceX = (GridCenterAt(GridW - 1, 0).x - GridCenterAt(0, 0).x) / GridW;
	float  gridSpaceY = (GridCenterAt(0, GridH - 1).y - GridCenterAt(0, 0).y) / GridH;
	c.Alloc((GridW + 1.5) * gridSpaceX, (GridH + 1.5) * gridSpaceY, Color8(255, 255, 255, 255));

	Vec2d avgD(0, 0);
	for (int x = 0; x < GridW; x++) {
		for (int y = 0; y < GridH; y++) {
			avgD += LastGridEl(x, y) / (double) (GridW * GridH);
		}
	}

	Point32 topG = GridCenterAt(0, 0);

	for (int x = 0; x < GridW; x++) {
		for (int y = 0; y < GridH; y++) {
			auto p = GridCenterAt(x, y) - topG;
			p.x += (int) (gridSpaceX / 1.5);
			p.y += (int) (gridSpaceY / 1.5);
			auto d = LastGridEl(x, y);
			d.y -= avgD.y;
			c.FillCircle(p.x, p.y, 1.2, Color8(150, 0, 0, 255));
			c.Line(p.x, p.y, p.x + d.x, p.y + d.y, Color8(150, 0, 0, 255), 1.0f);
		}
	}
	c.GetImage()->SavePng("flow-grid.png");
}

} // namespace roadproc
} // namespace imqs