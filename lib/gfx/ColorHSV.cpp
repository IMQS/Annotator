#include "pch.h"
#include "Color8.h"
#include "ColorHSV.h"

namespace imqs {
namespace gfx {

ColorHSVA ColorHSVA::FromFloat(float h, float s, float v, float a) {
	h          = math::Clamp(h, 0.0f, 1.0f);
	s          = math::Clamp(s, 0.0f, 1.0f);
	v          = math::Clamp(v, 0.0f, 1.0f);
	a          = math::Clamp(a, 0.0f, 1.0f);
	uint8_t _h = (uint8_t) floor(h * 255.0f + 0.5f);
	uint8_t _s = (uint8_t) floor(s * 255.0f + 0.5f);
	uint8_t _v = (uint8_t) floor(v * 255.0f + 0.5f);
	uint8_t _a = (uint8_t) floor(a * 255.0f + 0.5f);
	return ColorHSVA(_h, _s, _v, _a);
}

// https://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
template <typename T>
static void hsv2rgb(const int i, const T v, const T p, const T q, const T t, T& r, T& g, T& b) {
	switch (i) {
	case 0:
	case 6: // case 6 is only hit when h = 255
		r = v;
		g = t;
		b = p;
		break;
	case 1:
		r = q;
		g = v;
		b = p;
		break;
	case 2:
		r = p;
		g = v;
		b = t;
		break;
	case 3:
		r = p;
		g = q;
		b = v;
		break;
	case 4:
		r = t;
		g = p;
		b = v;
		break;
	default:
		r = v;
		g = p;
		b = q;
		break;
	}
}

Color8 ColorHSVA::ToRGBA() const {
	Color8 out;
	out.a = a;
	if (s == 0) {
		out.r = v;
		out.g = v;
		out.b = v;
		return out;
	}

	float h_unit = (float) h / 255.0f;
	float s_unit = (float) s / 255.0f;
	float v_unit = (float) v / 255.0f;
	float i      = floor(h_unit * 6.0f);
	float f      = h_unit * 6.0f - i;

	float p = v_unit * (1 - s_unit);
	float q = v_unit * (1 - f * s_unit);
	float t = v_unit * (1 - (1 - f) * s_unit);

	uint8_t p8 = (uint8_t) floor(p * 255.0f + 0.5f);
	uint8_t q8 = (uint8_t) floor(q * 255.0f + 0.5f);
	uint8_t t8 = (uint8_t) floor(t * 255.0f + 0.5f);

	uint8_t r, g, b;
	hsv2rgb((int) i, v, p8, q8, t8, r, g, b);
	out.r = r;
	out.g = g;
	out.b = b;

	return out;
}

void ColorHSVA::ToRGBA(const Vec4f& hsva, Vec4f& rgba) {
	rgba.w = hsva.w;

	float h = math::Clamp(hsva.x, 0.0f, 1.0f);
	float s = math::Clamp(hsva.y, 0.0f, 1.0f);
	float v = math::Clamp(hsva.z, 0.0f, 1.0f);

	if (s == 0) {
		rgba.x = v;
		rgba.y = v;
		rgba.z = v;
		return;
	}

	float i = floor(h * 6.0f);
	float f = h * 6.0f - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);

	hsv2rgb((int) i, v, p, q, t, rgba.x, rgba.y, rgba.z);
}

} // namespace gfx
} // namespace imqs
