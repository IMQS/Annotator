// This file has an underscore at the end so that it doesn't alias with the C standard library's math.h
#pragma once

#include <float.h>

namespace imqs {
namespace math {

template <typename FT>
class Traits {
public:
};

template <>
class Traits<float> {
public:
	static float Epsilon() { return FLT_EPSILON; }
	static float Min() { return FLT_MIN; }
	static float Max() { return FLT_MAX; }
	static bool  IsNaN(float v) { return v != v; }
	static bool  Finite(float v) { return std::isfinite(v); }
};

template <>
class Traits<double> {
public:
	static double Epsilon() { return DBL_EPSILON; }
	static double Min() { return DBL_MIN; }
	static double Max() { return DBL_MAX; }
	static bool   IsNaN(double v) { return v != v; }
	static bool   Finite(double v) { return std::isfinite(v); }
};

// If v is less than zero, return -1
// If v is greater than zero, return 1
// Otherwise return 0
template <typename TReal>
int SignOrZero(TReal v) {
	if (v < 0)
		return -1;
	if (v > 0)
		return 1;
	return 0;
}

// Compare two values and return -1, 0, +1
// Return -1 if a < b
// Return +1 if a > b
// Return 0 otherwise
template <typename T>
int Compare(const T& a, const T& b) {
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}
}
}
