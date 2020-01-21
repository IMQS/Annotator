#pragma once

namespace imqs {
namespace gfx {

// This is a line clipper that was built for Vector Tiles, in order to bring the
// numeric range of vertices within int32 limits.
// See https://observablehq.com/@bmharper/dual-clipper for the initial development notebook.

template <typename TBounds>
class SplittingLineClipper {
public:
	enum OutCodes {
		OutTop    = 1,
		OutBottom = 2,
		OutLeft   = 4,
		OutRight  = 8,
	};

	// Intersection Point
	struct IxPt {
		TBounds x;
		TBounds y;
		TBounds mu;
	};

	int32_t PrevOC;         // Previous OutCode
	TBounds X1, Y1, X2, Y2; // Clipping box
	TBounds PrevX, PrevY;   // Previous vertices
	bool    IsFirst;        // Is this the first vertex?

	SplittingLineClipper() {
	}

	// For upper-case rectangles (X1,Y1,X2,Y2)
	template <typename TRect>
	void InitUC(TRect clip) {
		X1 = (TBounds) clip.X1;
		Y1 = (TBounds) clip.Y1;
		X2 = (TBounds) clip.X2;
		Y2 = (TBounds) clip.Y2;
	}

	// For lower-case rectangles (x1,y1,x2,y2)
	template <typename TRect>
	void InitLC(TRect clip) {
		X1 = (TBounds) clip.x1;
		Y1 = (TBounds) clip.y1;
		X2 = (TBounds) clip.x2;
		Y2 = (TBounds) clip.y2;
	}

	void Begin() {
		PrevOC  = 0;
		IsFirst = true;
	}

	// Vertex output.
	// Call this function for every vertex of the line.
	// 'out' must be large enough to hold 4 vertices
	// Returns the number of vertices emitted into 'out'
	template <typename TPoint>
	size_t NextVertex(const TPoint& p, TPoint* out) {
		size_t nv = 0;
		auto   oc = OutCode(p);
		if (IsFirst && oc) {
			// if first vertex is outside, drop it
		} else if (!IsFirst && PrevOC != oc) {
			// add vertices wherever the segment intersects any of the 4 box lines (lines, not segments)
			nv = ClipBox(TPoint(PrevX, PrevY), p, PrevOC, oc, out);
			for (size_t i = 0; i < nv; i++)
				ClampToBox(out[i]);
			if (IsInsideBox(p)) {
				// add actual vx, if it ended inside the box
				out[nv++] = p;
			}
		} else {
			nv     = 1;
			out[0] = p;
			ClampToBox(out[0]);
		}
		IsFirst = false;
		PrevX   = p.x;
		PrevY   = p.y;
		PrevOC  = oc;
		return nv;
	}

private:
	template <typename TPoint>
	int32_t OutCode(TPoint pt) {
		int c = 0;

		if (pt.x < X1)
			c |= OutLeft;
		else if (pt.x > X2)
			c |= OutRight;

		if (pt.y < Y1)
			c |= OutTop;
		else if (pt.y > Y2)
			c |= OutBottom;

		return c;
	}

	template <typename T>
	static T Clamp(T x, T vmin, T vmax) {
		if (x < vmin)
			return vmin;
		if (x > vmax)
			return vmax;
		return x;
	}

	template <typename TPoint>
	void ClampToBox(TPoint& p) {
		p.x = Clamp(p.x, X1, X2);
		p.y = Clamp(p.y, Y1, Y2);
	}

	template <typename TPoint>
	bool IsInsideBox(const TPoint& p) const {
		return p.x > X1 && p.x < X2 &&
		       p.y > Y1 && p.y < Y2;
	}

	// Returns false if the lines are collinear (or close to it)
	template <typename TPoint>
	static bool LineIntersection(const TPoint& a1, const TPoint& a2, const TPoint& b1, const TPoint& b2, IxPt& ix) {
		auto idenom = ((a2[0] - a1[0]) * (b2[1] - b1[1]) - (a2[1] - a1[1]) * (b2[0] - b1[0]));
		// tan(0.1 deg) = 1e-3
		// tan(0.01 deg) = 1e-4
		// tan(0.001 deg) = 1e-5
		// tan(0.0001 deg) = 1e-6
		//if (fabs(idenom) <= collinearEps)
		//    return LineLineIntersection_Parallel< VecReal, robust >(A1, A2, B1, B2, ptIntersect, mua, mub, intersectionEpsilon);
		if (std::abs(idenom) <= 1e-6)
			return false;

		auto r = ((a1[1] - b1[1]) * (b2[0] - b1[0]) - (a1[0] - b1[0]) * (b2[1] - b1[1])) / idenom;
		//auto s = ((a1[1] - b1[1]) * (a2[0] - a1[0]) - (a1[0] - b1[0]) * (a2[1] - a1[1])) / idenom;

		//return [ a1[0] + r * (a2[0] - a1[0]), a1[1] + r * (a2[1] - a1[1]), r, s ];
		ix.x  = a1[0] + r * (a2[0] - a1[0]);
		ix.y  = a1[1] + r * (a2[1] - a1[1]);
		ix.mu = r;
		return true;
	}

	template <typename TPoint>
	size_t ClipBox(const TPoint& v1, const TPoint& v2, int32_t prevOC, int32_t oc, TPoint* ix) {
		auto   diff = prevOC ^ oc;
		size_t nv   = 0;
		IxPt   ixs[4];

		if (!!(diff & OutLeft) && LineIntersection(v1, v2, TPoint(X1, Y1), TPoint(X1, Y2), ixs[nv]))
			nv++;

		if (!!(diff & OutRight) && LineIntersection(v1, v2, TPoint(X2, Y1), TPoint(X2, Y2), ixs[nv]))
			nv++;

		if (!!(diff & OutTop) && LineIntersection(v1, v2, TPoint(X1, Y1), TPoint(X2, Y1), ixs[nv]))
			nv++;

		if (!!(diff & OutBottom) && LineIntersection(v1, v2, TPoint(X1, Y2), TPoint(X2, Y2), ixs[nv]))
			nv++;

		// bubble sort
		bool done = false;
		while (!done) {
			done = true;
			for (size_t i = 1; i < nv; i++) {
				if (ixs[i - 1].mu > ixs[i].mu) {
					done = false;
					std::swap(ixs[i - 1], ixs[i]);
				}
			}
		}
		for (size_t i = 0; i < nv; i++)
			ix[i] = TPoint(ixs[i].x, ixs[i].y);

		return nv;
	}
};

} // namespace gfx
} // namespace imqs