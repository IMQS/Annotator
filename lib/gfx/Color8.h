#pragma once

#include "ColorHSV.h"
#include <cmath>
#include "Vec4.h"

namespace imqs {
namespace gfx {

class ColorHSVA;

// Jim Blinn's perfect unsigned byte multiply
inline unsigned ByteMul(unsigned a, unsigned b) {
	unsigned i = a * b + 128;
	return (i + (i >> 8)) >> 8;
}

// A cheaper unsigned byte multiplier, which only guarantees that 1 * x = x, and 0 * x = 0
inline unsigned ByteMulCheap(unsigned a, unsigned b) {
	return ((a + 1) * b) >> 8;
}

/* 8-bit RGBA color

If not specified, then:
 * RGB values are sRGB
 * Alpha values are linear
 * Not premultiplied

*/
class Color8 {
public:
	union {
		struct
		{
#ifdef IMQS_ENDIAN_LITTLE
			uint8_t r : 8;
			uint8_t g : 8;
			uint8_t b : 8;
			uint8_t a : 8;
#else
			uint8_t a, b, g, r;
#endif
		};
		uint32_t u;
	};

	Color8() {}
	Color8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r), g(g), b(b), a(a) {}
	bool   operator==(const Color8& b) const { return u == b.u; }
	bool   operator!=(const Color8& b) const { return u != b.u; }
	void   Premultiply();
	Color8 Premultipied() const;

	ColorHSVA   ToHSVA() const;
	static void ToHSVA(const Vec4f& rgba, Vec4f& hsva);
	std::string ToCSS() const;
	uint8_t     Lum() const;

	float Rf() const { return (float) r / 255.0f; } // Returns Red 0..1
	float Gf() const { return (float) g / 255.0f; } // Returns Green 0..1
	float Bf() const { return (float) b / 255.0f; } // Returns Blue 0..1
	float Af() const { return (float) a / 255.0f; } // Returns Alpha 0..1

	float RLinear() const { return SRGBtoLinear((float) r / 255.0f); } // Returns Red 0..1, after converting from sRGB to linear
	float GLinear() const { return SRGBtoLinear((float) g / 255.0f); } // Returns Green 0..1, after converting from sRGB to linear
	float BLinear() const { return SRGBtoLinear((float) b / 255.0f); } // Returns Blue 0..1, after converting from sRGB to linear

	// Input values are clamped between 0..1
	static Color8 FromFloat(float h, float s, float v, float a);

	static Color8 White() { return Color8(255, 255, 255, 255); }
	static Color8 Black() { return Color8(0, 0, 0, 255); }
	static Color8 Red() { return Color8(255, 0, 0, 255); }
	static Color8 Transparent() { return Color8(0, 0, 0, 0); }

	static float SRGBtoLinear(float v) {
		const float a = 0.055f;
		return v <= 0.04045f ? v / 12.92f : pow((v + a) / (1 + a), 2.4f);
	}

	static float SRGBtoLinearU8(uint8_t v) {
		return SRGBtoLinear((float) v * (1.0f / 255.0f));
	}

	static void SRGBtoLinear(Vec4f& v) {
		v.x = SRGBtoLinear(v.x);
		v.y = SRGBtoLinear(v.y);
		v.z = SRGBtoLinear(v.z);
	}

	static float LinearToSRGB(float v) {
		const float a = 0.055f;
		return v <= 0.0031308f ? 12.92f * v : (1 + a) * pow(v, (1 / 2.4f));
	}

	static uint8_t LinearToSRGBU8(float v) {
		v = 255.0f * LinearToSRGB(v);
		if (v < 0)
			return 0;
		if (v > 255)
			return 255;
		return (uint8_t) v;
	}

	static void LinearToSRGB(Vec4f& v) {
		v.x = LinearToSRGB(math::Clamp(v.x, 0.0f, 1.0f));
		v.y = LinearToSRGB(math::Clamp(v.y, 0.0f, 1.0f));
		v.z = LinearToSRGB(math::Clamp(v.z, 0.0f, 1.0f));
	}
};

inline void Color8::Premultiply() {
	r = ByteMul(r, a);
	g = ByteMul(g, a);
	b = ByteMul(b, a);
}

inline Color8 Color8::Premultipied() const {
	Color8 copy = *this;
	copy.Premultiply();
	return copy;
}

inline uint8_t Color8::Lum() const {
	//  (0.3 * R) + (0.59 * G) + (0.11 * B)
	// If we reformulate the above ratios such that we're performing multiplication by 256,
	// then we can downshift by 8 bits instead of diving by 100.
	// 256 / 100 = 2.56
	// So we multiply the coefficients by 2.56, to get 77, 151, 28, for R,G,B.
	// And to check, we verify that R=G=B=255, does indeed produce 255 after downshifting by 8:
	// (255*77 + 255*151 + 255*28) = 65280
	// 65280 >> 8 = 255
	auto _r = (unsigned) r * 77;  // 77  = 30 * 2.56
	auto _g = (unsigned) g * 151; // 151 = 59 * 2.56
	auto _b = (unsigned) b * 28;  // 28  = 11 * 2.56
	auto m  = (_r + _g + _b) >> 8;
	return (uint8_t) m;
}

} // namespace gfx
} // namespace imqs
