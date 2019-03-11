// This file has an underscore at the end so that it doesn't alias with the C standard library's math.h
#pragma once

#include <float.h>

namespace imqs {
namespace math {

#define IMQS_PI 3.1415926535897932384626433832795

#define IMQS_RAD2DEG (180 / 3.1415926535897932384626433832795)
#define IMQS_DEG2RAD (3.1415926535897932384626433832795 / 180)

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

template <>
class Traits<int32_t> {
public:
	static int32_t Epsilon() { return 0; }
	static int32_t Min() { return INT32_MIN; }
	static int32_t Max() { return INT32_MAX; }
	static bool    IsNaN(int32_t v) { return false; }
	static bool    Finite(int32_t v) { return true; }
};

template <>
class Traits<int64_t> {
public:
	static int64_t Epsilon() { return 0; }
	static int64_t Min() { return INT64_MIN; }
	static int64_t Max() { return INT64_MAX; }
	static bool    IsNaN(int64_t v) { return false; }
	static bool    Finite(int64_t v) { return true; }
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

template <typename T>
T Clamp(const T& v, const T& vmin, const T& vmax) {
	if (v < vmin)
		return vmin;
	if (v > vmax)
		return vmax;
	return v;
}

template <typename T>
T RoundUpInt(T v, T denom) {
	return (v + denom - 1) & ~(denom - 1);
}

inline bool IsFinite(float v) { return std::isfinite(v); }
inline bool IsFinite(double v) { return std::isfinite(v); }

template <typename T, typename Result>
std::pair<Result, Result> MeanAndVariance(size_t n, const T* x) {
	Result mean = 0;
	for (size_t i = 0; i < n; i++) {
		mean += x[i];
	}
	mean /= (Result) n;

	Result var = 0;
	for (size_t i = 0; i < n; i++) {
		var += (x[i] - mean) * (x[i] - mean);
	}
	var /= Result(n - 1);
	return std::pair<Result, Result>(mean, var);
}

template <typename T, typename Result>
std::pair<Result, Result> MeanAndVariance(const std::vector<T>& x) {
	return MeanAndVariance<T, Result>(x.size(), &x[0]);
}

template <typename T, typename Result>
std::pair<Result, Result> MinMax(size_t n, const T* x) {
	Result vmin = 0, vmax = 0;
	if (n != 0) {
		vmin = x[0];
		vmax = x[0];
		for (size_t i = 0; i < n; i++) {
			vmin = vmin < x[i] ? vmin : x[i];
			vmax = vmax > x[i] ? vmax : x[i];
		}
	}
	return std::pair<Result, Result>(vmin, vmax);
}

template <typename T, typename Result>
std::pair<Result, Result> MinMax(const std::vector<T>& x) {
	return MinMax<T, Result>(x.size(), &x[0]);
}

template <typename T>
T Median(size_t n, const T* x) {
	if (n == 0)
		return T();
	if (n == 1)
		return x[0];
	auto copy = new T[n];
	for (size_t i = 0; i < n; i++)
		copy[i] = x[i];
	std::sort(copy, copy + n);
	delete[] copy;
	return copy[n / 2];
}

template <typename T>
T Median(const std::vector<T>& x) {
	return Median<T>(x.size(), &x[0]);
}

} // namespace math
} // namespace imqs
