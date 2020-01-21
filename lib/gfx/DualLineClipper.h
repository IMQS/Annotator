#pragma once

#include "CoarseLineClipper.h"
#include "SplittingLineClipper.h"

namespace imqs {
namespace gfx {

// This combines SplittingLineClipper and CoarseLineClipper, by first running
// SplittingLineClipper, and then feeding it's output into CoarseLineClipper
//
// See https://observablehq.com/@bmharper/dual-clipper for the initial development notebook.
//
// This clipper doesn't care whether the polyline is closed or not. In order to emit
// a closed polyline, just repeat the first vertex after everything else.
//
class DualLineClipper {
public:
	struct V2 {
		double x, y;
	};
	SplittingLineClipper<double> Splitting;
	CoarseLineClipper<double>    Coarse;

	// For upper-case rectangles (X1,Y1,X2,Y2)
	template <typename TRect>
	void InitUC(TRect little, TRect big) {
		Splitting.InitUC(big);
		Coarse.InitUC(little);
	}

	// For lower-case rectangles (x1,y1,x2,y2)
	template <typename TRect>
	void InitLC(TRect little, TRect big) {
		Splitting.InitLC(big);
		Coarse.InitLC(little);
	}

	void Begin() {
		Splitting.Begin();
		Coarse.Begin(false);
	}

	// SPLIT then COARSE
	// Returns the number of vertices emitted
	// 'out' must be large enough to hold 8 vertices
	template <typename TPoint>
	size_t NextVertex_SplitFirst(const TPoint& p, TPoint* out) {
		// The splitting clipper can yield up to 4 vertices.
		// We feed these 0..4 vertices into the coarse clipper
		TPoint split[4];
		size_t nSplit = Splitting.NextVertex(p, split);
		return EmitCoarse(nSplit, split, out);
	}

	// COARSE then SPLIT
	// Returns the number of vertices emitted
	// 'out' must be large enough to hold 8 vertices
	template <typename TPoint>
	size_t NextVertex_CoarseFirst(const TPoint& p, TPoint* out) {
		TPoint inter[2];
		size_t nInter = EmitCoarse(1, &p, inter);
		size_t nOut   = 0;
		for (size_t i = 0; i < nInter; i++)
			nOut += Splitting.NextVertex(inter[i], out + nOut);
		return nOut;
	}

	template <typename TPoint>
	size_t NextVertex_CoarseOnly(const TPoint& p, TPoint* out) {
		return EmitCoarse(1, &p, out);
	}

	template <typename TPoint>
	size_t NextVertex(const TPoint& p, TPoint* out) {
		return NextVertex_SplitFirst(p, out);
	}

private:
	V2 PrevCoarse;

	template <typename TPoint>
	size_t EmitCoarse(size_t nVert, const TPoint* vert, TPoint* out) {
		size_t nv = 0;
		for (size_t i = 0; i < nVert; i++) {
			switch (Coarse.NextVertex(vert[i], false)) {
			case CoarseLineClipper<double>::CmdEmitNothing: break;
			case CoarseLineClipper<double>::CmdEmitThis:
				out[nv++] = vert[i];
				break;
			case CoarseLineClipper<double>::CmdEmitPrevAndThis:
				out[nv++] = TPoint(PrevCoarse.x, PrevCoarse.y);
				out[nv++] = vert[i];
				break;
			}
			PrevCoarse = {vert[i].x, vert[i].y};
		}
		return nv;
	}
};

} // namespace gfx
} // namespace imqs