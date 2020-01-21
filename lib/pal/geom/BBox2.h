#pragma once

#include "../Math_.h"

namespace imqs {
namespace geom2d {

template <typename T>
class BBox2 {
public:
	T X1 = imqs::math::Traits<T>::Max();
	T Y1 = imqs::math::Traits<T>::Max();
	T X2 = -imqs::math::Traits<T>::Max();
	T Y2 = -imqs::math::Traits<T>::Max();

	BBox2() {
	}

	BBox2(T x1, T y1, T x2, T y2) : X1(x1), Y1(y1), X2(x2), Y2(y2) {
	}

	static BBox2 Normalized(T x1, T y1, T x2, T y2) {
		BBox2 b;
		b.ExpandToFit(x1, y1);
		b.ExpandToFit(x2, y2);
		return b;
	}

	void Reset() {
		*this = BBox2();
	}

	bool IsNull() const {
		return X1 > X2 || Y1 > Y2;
	}

	void ExpandToFit(T x, T y) {
		X1 = std::min(X1, x);
		Y1 = std::min(Y1, y);
		X2 = std::max(X2, x);
		Y2 = std::max(Y2, y);
	}

	// Returns true if the boxes overlap or touch.
	bool PositiveUnion(const BBox2& b) const {
		return b.X2 >= X1 && b.X1 <= X2 &&
		       b.Y2 >= Y1 && b.Y1 <= Y2;
	}

	// Clips this box, so that it is inside 'b'
	void ClipTo(const BBox2& b) {
		X1 = std::max(X1, b.X1);
		Y1 = std::max(Y1, b.Y1);
		X2 = std::min(X2, b.X2);
		Y2 = std::min(Y2, b.Y2);
	}

	// Expand each side by an exact amount
	void Expand(T xExpand, T yExpand) {
		X1 -= xExpand;
		Y1 -= yExpand;
		X2 += xExpand;
		Y2 += yExpand;
	}

	// Returns true if the other box is inside or equal to this box.
	bool IsInsideMe(const BBox2& b) const {
		return b.X1 >= X1 && b.Y1 >= Y1 &&
		       b.X2 <= X2 && b.Y2 <= Y2;
	}

	// Returns true if this box is inside or equal to the other box
	bool IsInsideOf(const BBox2& b) const {
		return b.IsInsideMe(*this);
	}

	T Width() const {
		return X2 - X1;
	}

	T Height() const {
		return Y2 - Y1;
	}

	void Center(T& x, T& y) const {
		x = (X1 + X2) / 2.0;
		y = (Y1 + Y2) / 2.0;
	}
};

typedef BBox2<double>  BBox2d;
typedef BBox2<float>   BBox2f;
typedef BBox2<int32_t> BBox2i32;
typedef BBox2<int64_t> BBox2i64;
} // namespace geom2d
} // namespace imqs