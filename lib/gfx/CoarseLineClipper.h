#pragma once

namespace imqs {
namespace gfx {

/* Axis Aligned Polyline (or Polygon) Clipper

This is a line, polyline, or polygon clipper, which clips against an axis aligned bounding box
(aka a rectangle). The clipper does not cut lines where they intersect the bounding box.
Instead, it relies on a subsequent phase of the renderer to perform that low level clipping.
This clipper is good for rejecting a ton of vertices that lie outside of the bounding box.

For example, if your entire polyline is outside of the bounding box, then the clipper will
be very fast at informing you that you can avoid rendering the polyline completely.

Another example is a contour line that thousands of vertices, which briefly enters
the bounding box, for a few vertices, and then exits the bounding box, and has many
thousands more vertices until it finishes. This will also be dealt with very effectively
by this clipper. It will tell you to ignore all of the vertices until it's about to
enter the bounding box. Then, as it moves through the bounding box, you will be
instructed to emit those vertices. Finally, as it moves out of the bounding box, it will
instruct you to emit one more vertex, and then no more after that.

The checks that this clipper performs are extremely cheap, which is why it's about
as fast as it gets.

I call this a 'coarse' line clipper, because it doesn't do a lot of work - it merely
rejects or accepts vertices. Often, this is enough.

Usage:
1. Initialize the bounds by using InitLC, InitUC, or manually initializing X1,Y1,X2,Y2.
2. For every polyline, call Begin(). A closed polyline and a polygon are the same thing
   from the point of view of the clipper.
3. Call NextVertex() for every one of your vertices. See the instructions above NextVertex()
   for what to do for the 3 possible return values from NextVertex().
3. After calling NextVertex() for every vertex, check the value of TotalVertices. If it is
   equal to 1, then you must discard the entire object.

The clipper will always emit the first vertex. This is not 100% optimal, but it's workable.
On the final vertex, if the clipper detects that all vertices were outside the bounding box,
then it will emit nothing. It is thus possible to have only a single vertex emitted.
It is your responsibility to detect this condition (ie only one vertex emitted) and discard
the object.

*/
template <typename TBounds>
class CoarseLineClipper {
public:
	enum Cmd {
		CmdEmitNothing     = 0,
		CmdEmitThis        = 1,
		CmdEmitPrevAndThis = 2
	};

	enum OutCodes {
		OutTop    = 1,
		OutBottom = 2,
		OutLeft   = 4,
		OutRight  = 8,
	};

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

	bool    Closed;
	bool    PrevOut;
	int32_t PrevCode;
	int32_t SummaryCodeAND;
	size_t  TotalVertices;
	TBounds X1, Y1, X2, Y2;

	CoarseLineClipper() {
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

	void Expand(TBounds pad) {
		TBounds npad = -pad;
		X1 += npad;
		Y1 += npad;
		X2 += pad;
		Y2 += pad;
	}

	void Begin(bool isClosed) {
		PrevOut        = false;
		PrevCode       = 0;
		SummaryCodeAND = 0xffffffff;
		TotalVertices  = 0;
		Closed         = isClosed;
	}

	template <typename TPoint>
	void Summary(int n, const TPoint* p, int& oc_or, int& oc_and) {
		int qor  = 0;
		int qand = 0xffff;
		for (int i = 0; i < n; i++) {
			int oc = OutCode(p[i]);
			qor |= oc;
			qand &= oc;
		}
		oc_or  = qor;
		oc_and = qand;
	}

	/** Vertex output.
	Call this function for every vertex of the line.
	@return 0, 1, or 2.
	0: Emit nothing.
	1: Emit this vertex.
	2: Emit the previous vertex, then this vertex. Previous does not mean the last vertex that you emitted. It means the
	vertex immediately prior to this one, in the unclipped, natural line order.
	**/
	template <typename TPoint>
	Cmd NextVertex(const TPoint& p, bool isLast) {
		// isLast doesn't matter if we're not closed
		isLast = isLast & Closed;

		int oc = OutCode(p);
		SummaryCodeAND &= oc;
		bool segOut = (PrevCode & oc) != 0; // Are we outside of any clipping line?

		Cmd result;
		if (isLast && (SummaryCodeAND != 0)) {
			// We should have emitted only the first vertex.
			// The entire object must be clipped.
			return CmdEmitNothing;
		} else if (segOut && !isLast) { // never skip last vertex if we're closed
			// Skip.
			// BUT, add point if we are changing quadrants. Imagine a line that walks out of the top, walks around the left side, down to the bottom,
			// and then back into the viewport again through the bottom. As it walks back in through the bottom, you'll get this giant line
			// crossing through the viewport, whereas you actually want the line to have walked around the outside -- with as few vertices as possible, of course.
			if (oc != PrevCode) {
				TotalVertices++;
				result = CmdEmitThis;
			} else {
				result = CmdEmitNothing;
			}
		} else {
			// add previous vertex, if we have skipped it
			if (PrevOut) {
				TotalVertices += 2;
				result = CmdEmitPrevAndThis;
			} else {
				TotalVertices++;
				result = CmdEmitThis;
			}
		}

		PrevOut  = segOut;
		PrevCode = oc;

		return result;
	}
};

// This is a wrapper around CoarseLineClipper which has a slightly easier to use API
template <typename TBounds, typename TPoint>
class CoarseLineClipperEasy {
public:
	CoarseLineClipper<TBounds> Clipper;
	TPoint                     LastVertex;

	// For upper-case rectangles (X1,Y1,X2,Y2)
	template <typename TRect>
	void InitUC(TRect clip) {
		Clipper.InitUC(clip);
	}

	// For lower-case rectangles (x1,y1,x2,y2)
	template <typename TRect>
	void InitLC(TRect clip) {
		Clipper.InitLC(clip);
	}

	void Begin(bool isClosed) {
		Clipper.Begin(isClosed);
	}

	// Returns the number of vertices emitted into 'out' (either 0, 1, or 2)
	size_t NextVertex(const TPoint& p, bool isLast, TPoint* out) {
		size_t nv = 0;
		switch (Clipper.NextVertex(p, isLast)) {
		case CoarseLineClipper<TBounds>::CmdEmitNothing:
			break;
		case CoarseLineClipper<TBounds>::CmdEmitThis:
			out[0] = p;
			nv     = 1;
			break;
		case CoarseLineClipper<TBounds>::CmdEmitPrevAndThis:
			out[0] = LastVertex;
			out[1] = p;
			nv     = 2;
			break;
		}
		LastVertex = p;
		return nv;
	}
};

} // namespace gfx
} // namespace imqs