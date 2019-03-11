#include "pch.h"
#include "Color8.h"

namespace imqs {
namespace gfx {

inline float min3(float a, float b, float c) {
	float m = a < b ? a : b;
	m       = m < c ? m : c;
	return m;
}

inline float max3(float a, float b, float c) {
	float m = a > b ? a : b;
	m       = m > c ? m : c;
	return m;
}

// https://gist.github.com/yoggy/8999625
static void rgb2hsv(const float r, const float g, const float b, float& h, float& s, float& v) {
	float max = max3(r, g, b);
	float min = min3(r, g, b);

	v = max;

	if (max == 0.0f) {
		s = 0;
		h = 0;
	} else if (max - min == 0.0f) {
		s = 0;
		h = 0;
	} else {
		s = (max - min) / max;

		if (max == r) {
			h = 60.0f * ((g - b) / (max - min)) + 0.0f;
		} else if (max == g) {
			h = 60.0f * ((b - r) / (max - min)) + 120.0f;
		} else {
			h = 60.0f * ((r - g) / (max - min)) + 240.0f;
		}
	}

	if (h < 0.0f)
		h += 360.0f;
}

ColorHSVA Color8::ToHSVA() const {
	uint8_t hsv[3];
	float   _r = r / 255.0f;
	float   _g = g / 255.0f;
	float   _b = b / 255.0f;
	float   h, s, v; // h:0-360.0, s:0.0-1.0, v:0.0-1.0
	rgb2hsv(_r, _g, _b, h, s, v);
	hsv[0] = (uint8_t)(h * (255.0f / 360.0f)); // dst_h : 0-255
	hsv[1] = (uint8_t)(s * 255.0f);            // dst_s : 0-255
	hsv[2] = (uint8_t)(v * 255.0f);            // dst_v : 0-255
	return ColorHSVA(hsv[0], hsv[1], hsv[2], a);
}

void Color8::ToHSVA(const Vec4f& rgba, Vec4f& hsva) {
	float r = math::Clamp(rgba.x, 0.0f, 1.0f);
	float g = math::Clamp(rgba.y, 0.0f, 1.0f);
	float b = math::Clamp(rgba.z, 0.0f, 1.0f);
	float h, s, v; // h:0-360.0, s:0.0-1.0, v:0.0-1.0
	rgb2hsv(r, g, b, h, s, v);
	hsva.x = h / 360.0f;
	hsva.y = s;
	hsva.z = v;
	hsva.w = rgba.w;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 6031) // warning C6031: Return value ignored: 'snprintf'.
#endif
std::string Color8::ToCSS() const {
	char buf[64];
	if (a == 255) {
		buf[0] = '#';
		strings::ToHex(r, buf + 1);
		strings::ToHex(g, buf + 3);
		strings::ToHex(b, buf + 5);
		buf[7] = 0;
	} else {
		snprintf(buf, arraysize(buf), "rgba(%u,%u,%u,%.3f)", r, g, b, a / 255.0f);
	}
	return buf;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

Color8 Color8::FromFloat(float r, float g, float b, float a) {
	r          = math::Clamp(r, 0.0f, 1.0f);
	g          = math::Clamp(g, 0.0f, 1.0f);
	b          = math::Clamp(b, 0.0f, 1.0f);
	a          = math::Clamp(a, 0.0f, 1.0f);
	uint8_t _r = (uint8_t)(r * 255.0f);
	uint8_t _g = (uint8_t)(g * 255.0f);
	uint8_t _b = (uint8_t)(b * 255.0f);
	uint8_t _a = (uint8_t)(a * 255.0f);
	return Color8(_r, _g, _b, _a);
}

} // namespace gfx
} // namespace imqs