#pragma once

#include "Color8.h"

namespace imqs {
namespace gfx {

enum class BlendPrecision {
	BlendPacked,
	BlendCheap,
	BlendPerfect
};

template <bool InspectAlphaZero, bool InspectAlphaOne, BlendPrecision precis>
void TBlendOver(uint32_t pix, uint32_t* pout) {
	Color8 s = pix;
	Color8 d;

	if (InspectAlphaZero && s.a == 0) {
		// Uncomment this to get a visual indication
		// *pout = PixBGRA::Make(255,0,0,255).u;
		return;
	}

	if (InspectAlphaOne && s.a == 255) {
		*pout = s.u;
		return;
	}

	if (precis == BlendPrecision::BlendPacked) {
		// On my Athlon 64 X2, this yields a 1.5x speed improvement over component-wise ByteMulCheap.
		// Warning! This code is pretty sensitive for the optimizer. I tried moving it into an inline function, and it got slower.
		const uint32_t mask = 0x00ff00ff;
		uint32_t       d1   = *pout & mask;
		uint32_t       d2   = (*pout >> 8) & mask;
		uint32_t       mul  = 255 - s.a; // Change this to a uint32_t and it gets slower.
		d1                  = ((d1 * mul + mask) >> 8) & mask;
		d2                  = (d2 * mul + mask) & (mask << 8);
		d1                  = d1 | d2;
		d1 += s.u;
		d.u = d1;
	} else if (precis == BlendPrecision::BlendCheap) {
		d.u = *pout;
		d.r = ByteMulCheap<int32_t>(d.r, 255 - s.a) + s.r;
		d.g = ByteMulCheap<int32_t>(d.g, 255 - s.a) + s.g;
		d.b = ByteMulCheap<int32_t>(d.b, 255 - s.a) + s.b;
		d.a = ByteMulCheap<int32_t>(d.a, 255 - s.a) + s.a;
	} else {
		d.u = *pout;
		d.r = ByteMul<int32_t>(d.r, 255 - s.a) + s.r;
		d.g = ByteMul<int32_t>(d.g, 255 - s.a) + s.g;
		d.b = ByteMul<int32_t>(d.b, 255 - s.a) + s.b;
		d.a = ByteMul<int32_t>(d.a, 255 - s.a) + s.a;
	}

	*pout = d.u;
}

// Perform an "Over" Blend with reasonable defaults
inline void BlendOver(uint32_t pix, uint32_t* pout) {
	TBlendOver<true, true, BlendPrecision::BlendPacked>(pix, pout);
}

} // namespace gfx
} // namespace imqs