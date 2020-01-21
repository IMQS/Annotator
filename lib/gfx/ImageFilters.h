#pragma once

#include "Image.h"

namespace imqs {
namespace gfx {
namespace filters {

template <typename Func>
void Filter_1x1_RGBA(Image& img, Func f) {
	for (int y = 0; y < img.Height; y++) {
		int  width = img.Width;
		auto pix   = (Color8*) img.Line(y);
		for (int x = 0; x < width; x++, pix++)
			*pix = f(*pix);
	}
}

struct AlphaThresholdToZero {
	uint8_t Threshold = 127;

	Color8 operator()(Color8 c) const {
		if (c.a <= Threshold)
			return Color8(0);
		else
			return c;
	}
};

} // namespace filters
} // namespace gfx
} // namespace imqs