#pragma once

#include <cmath>

#include "Vec2.h"

namespace imqs {
namespace gfx {

// Base has no default constructor, so it can be included inside unions
template <typename T>
class Vec3Base {
public:
	static const size_t Dim = 3;
	typedef T           FT;
	union {
		struct {
			T x, y, z;
		};
		T           n[3];
		Vec2Base<T> vec2;
	};

	T  operator[](int col) const { return n[col]; }
	T& operator[](int col) { return n[col]; }

	bool operator==(Vec3Base b) const {
		return x == b.x && y == b.y && z == b.z;
	}
	bool operator!=(Vec3Base b) const {
		return x != b.x || y != b.y || z != b.z;
	}

	T size() const { return sqrt(x * x + y * y + z * z); }
	T rsize() const { return ((T) 1) / sqrt(x * x + y * y + z * z); }
	// Last time I checked (November 2018), neither clang's libc++ nor MSVC had std::hypot for 3 parameters
	// T size() const { return std::hypot(x, y, z); }
	// T rsize() const { return ((T) 1) / std::hypot(x, y, z); }

	T dot(Vec3Base b) const {
		return x * b.x + y * b.y + z * b.z;
	}

	T distance(Vec3Base b) const {
		return (*this - b).size();
	}

	T distance2D(Vec3Base b) const {
		return std::hypot(x - b.x, y - b.y);
	}

	Vec3Base& operator+=(Vec3Base b) {
		x += b.x;
		y += b.y;
		z += b.z;
		return *this;
	}

	Vec3Base& operator-=(Vec3Base b) {
		x -= b.x;
		y -= b.y;
		z -= b.z;
		return *this;
	}
};

template <typename T>
Vec3Base<T> operator*(Vec3Base<T> v, T s) { return {v.x * s, v.y * s, v.z * s}; }

template <typename T>
Vec3Base<T> operator*(T s, Vec3Base<T> v) { return {v.x * s, v.y * s, v.z * s}; }

template <typename T>
Vec3Base<T> operator/(Vec3Base<T> v, T s) { return {v.x / s, v.y / s, v.z / s}; }

template <typename T>
Vec3Base<T> operator-(Vec3Base<T> a, Vec3Base<T> b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

template <typename T>
Vec3Base<T> operator+(Vec3Base<T> a, Vec3Base<T> b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

template <typename T>
class Vec3 : public Vec3Base<T> {
public:
	typedef Vec3Base<T> Base;
	Vec3() {}
	Vec3(T _x, T _y, T _z) {
		this->x = _x;
		this->y = _y;
		this->z = _z;
	}
	Vec3(Base b) {
		this->x = b.x;
		this->y = b.y;
		this->z = b.z;
	}
	Vec3(Vec2Base<T> b, float z) {
		this->x = b.x;
		this->y = b.y;
		this->z = z;
	}
};

typedef Vec3<double> Vec3d;
typedef Vec3<float>  Vec3f;

inline Vec3f Vec3dTof(const Vec3d& v) { return Vec3f((float) v.x, (float) v.y, (float) v.z); }
inline Vec3d Vec3fTod(const Vec3f& v) { return Vec3d((double) v.x, (double) v.y, (double) v.z); }

} // namespace gfx
} // namespace imqs