#include "pch.h"
#include "RingTreeFinder.h"
#include "geom2d.h"

namespace imqs {
namespace geom2d {

RingTreeFinder::RingTreeFinder() {
}

RingTreeFinder::~RingTreeFinder() {
	for (auto r : Rings)
		delete r;
}

template <typename T>
void RingTreeFinder::AddRingInternal(size_t n, const T* vertices, int strideInT) {
	Ring* ring  = new Ring();
	ring->Index = Rings.size();

	ring->Vertices.reserve(n);
	for (int i = 0; i < n; i++) {
		Point v{vertices[0], vertices[1]};
		ring->Vertices.push_back(v);
		ring->Bounds.ExpandToFit(v.X, v.Y);
		vertices += strideInT;
	}
	ring->Area = PolygonArea(ring->Vertices.size(), &ring->Vertices[0].X, 2);

	Rings.push_back(ring);
}

void RingTreeFinder::AddRing(size_t n, const double* v, int strideInDoubles) {
	AddRingInternal(n, v, strideInDoubles);
}

void RingTreeFinder::AddRing(size_t n, const float* v, int strideInFloats) {
	AddRingInternal(n, v, strideInFloats);
}

void RingTreeFinder::WalkSetDepth(Ring* r, int level) {
	r->Level = level;
	for (int i = 0; i < r->Children.size(); i++)
		WalkSetDepth(r->Children[i], level + 1);
}

void RingTreeFinder::Analyze() {
	std::vector<Ring*> rstack;

	// Do initial N x N pass which discovers all the encircling rings of each other ring.
	// We ask: is "i" a child of "j" ?
	for (int i = 0; i < Rings.size(); i++) {
		// Pick the ring from j that encloses our first vertex, and has the least area of all such enclosing rings.
		int best_parent_index = -1;
		for (int j = 0; j < Rings.size(); j++) {
			if (i == j)
				continue;
			if (!Rings[i]->Bounds.PositiveUnion(Rings[j]->Bounds))
				continue;
			if (Rings[i]->Area >= Rings[j]->Area)
				continue;

			auto iptFirst   = Rings[i]->Vertices[0];
			bool i_inside_j = PtInsidePoly(iptFirst.X, iptFirst.Y, Rings[j]->Vertices.size(), &Rings[j]->Vertices[0].X, 2);
			if (i_inside_j) {
				if (best_parent_index == -1 || Rings[j]->Area < Rings[best_parent_index]->Area)
					best_parent_index = j;
			}
		}
		if (best_parent_index != -1) {
			Rings[best_parent_index]->Children.push_back(Rings[i]);
			Rings[i]->Parent = Rings[best_parent_index];
		}
	}

	// Establish the levels
	for (int i = 0; i < Rings.size(); i++) {
		if (Rings[i]->Parent == NULL)
			WalkSetDepth(Rings[i], 0);
	}

	int nzero = 0;

	for (int i = 0; i < Rings.size(); i++) {
		if (Rings[i]->Level == 0)
			nzero++;
		IMQS_ASSERT(Rings[i]->Level != -1);
	}

	// We should always have at least 1 outer ring
	IMQS_ASSERT(nzero != 0 || Rings.size() == 0);
}

} // namespace geom2d
} // namespace imqs