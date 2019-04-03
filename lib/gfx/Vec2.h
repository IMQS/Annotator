#pragma once

#include <cmath>

namespace imqs {
namespace gfx {

// Base has no default constructor, so it can be included inside unions
template <typename T>
class Vec2Base {
public:
	static const size_t Dim = 2;
	typedef T           FT;
	union {
		struct {
			T x, y;
		};
		T n[2];
	};

	T  operator[](int col) const { return n[col]; }
	T& operator[](int col) { return n[col]; }

	Vec2Base& operator*=(T s) {
		x *= s;
		y *= s;
		return *this;
	}

	Vec2Base& operator*=(Vec2Base b) {
		x *= b.x;
		y *= b.y;
		return *this;
	}

	Vec2Base& operator/=(Vec2Base b) {
		x /= b.x;
		y /= b.y;
		return *this;
	}

	Vec2Base& operator/=(T d) {
		x /= d;
		y /= d;
		return *this;
	}

	Vec2Base& operator+=(Vec2Base b) {
		x += b.x;
		y += b.y;
		return *this;
	}

	Vec2Base& operator-=(Vec2Base b) {
		x -= b.x;
		y -= b.y;
		return *this;
	}

	Vec2Base operator-() const {
		Vec2Base n;
		n.x = -x;
		n.y = -y;
		return n;
	}

	bool operator==(Vec2Base b) const {
		return x == b.x && y == b.y;
	}
	bool operator!=(Vec2Base b) const {
		return x != b.x || y != b.y;
	}

	T size() const { return std::hypot(x, y); }
	T rsize() const { return ((T) 1) / std::hypot(x, y); }

	Vec2Base normalized() const {
		Vec2Base n = *this;
		auto     s = rsize();
		if (s == s) {
			n.x *= s;
			n.y *= s;
		}
		return n;
	}

	T distance(Vec2Base b) const {
		return (*this - b).size();
	}

	T distanceSQ(Vec2Base b) const {
		return (*this - b).dot(*this - b);
	}

	T distance2D(Vec2Base b) const {
		return distance(b);
	}

	T dot(Vec2Base b) const {
		return x * b.x + y * b.y;
	}

	// angle between two vectors (which need not be normalized)
	T angle(Vec2Base b) const {
		return acos(dot(b) / (size() * b.size()));
	}

	// angle between two normalized vectors
	T angleNormalized(Vec2Base b) const {
		return acos(dot(b));
	}
};

template <typename T>
Vec2Base<T> operator*(Vec2Base<T> v, T s) { return {v.x * s, v.y * s}; }

template <typename T>
Vec2Base<T> operator*(T s, Vec2Base<T> v) { return {v.x * s, v.y * s}; }

template <typename T>
Vec2Base<T> operator*(Vec2Base<T> a, Vec2Base<T> b) { return {a.x * b.x, a.y * b.y}; }

template <typename T>
Vec2Base<T> operator/(Vec2Base<T> a, Vec2Base<T> b) { return {a.x / b.x, a.y / b.y}; }

template <typename T>
Vec2Base<T> operator/(Vec2Base<T> v, T s) { return {v.x / s, v.y / s}; }

template <typename T>
Vec2Base<T> operator-(Vec2Base<T> a, Vec2Base<T> b) { return {a.x - b.x, a.y - b.y}; }

template <typename T>
Vec2Base<T> operator+(Vec2Base<T> a, Vec2Base<T> b) { return {a.x + b.x, a.y + b.y}; }

template <typename T>
class Vec2 : public Vec2Base<T> {
public:
	typedef Vec2Base<T> Base;
	Vec2() {}
	Vec2(T _x, T _y) {
		this->x = _x;
		this->y = _y;
	}
	Vec2(Base b) {
		this->x = b.x;
		this->y = b.y;
	}
};

typedef Vec2<double> Vec2d;
typedef Vec2<float>  Vec2f;

inline Vec2f Vec2dTof(const Vec2d& v) { return Vec2f((float) v.x, (float) v.y); }
inline Vec2d Vec2fTod(const Vec2f& v) { return Vec2d((double) v.x, (double) v.y); }

} // namespace gfx
} // namespace imqs
