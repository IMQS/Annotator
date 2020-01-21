#pragma once

#include "../Math_.h"

namespace imqs {
namespace geom3d {

template <typename T>
class BBox3 {
public:
	T X1 = imqs::math::Traits<T>::Max();
	T Y1 = imqs::math::Traits<T>::Max();
	T Z1 = imqs::math::Traits<T>::Max();
	T X2 = -imqs::math::Traits<T>::Max();
	T Y2 = -imqs::math::Traits<T>::Max();
	T Z2 = -imqs::math::Traits<T>::Max();

	BBox3() {
	}

	BBox3(T x1, T y1, T z1, T x2, T y2, T z2) : X1(x1), Y1(y1), Z1(z1), X2(x2), Y2(y2), Z2(z2) {
	}

	static BBox3 Normalized(T x1, T y1, T z1, T x2, T y2, T z2) {
		BBox3 b;
		b.ExpandToFit(x1, y1, z1);
		b.ExpandToFit(x2, y2, z2);
		return b;
	}

	void Reset() {
		*this = BBox3();
	}

	void ExpandToFit(T x, T y, T z) {
		X1 = std::min(X1, x);
		Y1 = std::min(Y1, y);
		Z1 = std::min(Z1, z);
		X2 = std::max(X2, x);
		Y2 = std::max(Y2, y);
		Z2 = std::max(Z2, z);
	}

	void ExpandToFit(const BBox3& b) {
		X1 = std::min(X1, b.X1);
		Y1 = std::min(Y1, b.Y1);
		Z1 = std::min(Z1, b.Z1);
		X2 = std::max(X2, b.X2);
		Y2 = std::max(Y2, b.Y2);
		Z2 = std::max(Z2, b.Z2);
	}

	// Returns true if the boxes overlap or touch.
	bool PositiveUnion(const BBox3& b) const {
		return b.X2 >= X1 && b.X1 <= X2 &&
		       b.Y2 >= Y1 && b.Y1 <= Y2 &&
		       b.Z2 >= Z1 && b.Z1 <= Z2;
	}

	// Returns true if the other box is inside or equal to this box.
	bool IsInsideMe(const BBox3& b) const {
		return b.X1 >= X1 && b.Y1 >= Y1 && b.Z1 >= Z1 &&
		       b.X2 <= X2 && b.Y2 <= Y2 && b.Z2 <= Z2;
	}

	T Width() const {
		return X2 - X1;
	}

	T Height() const {
		return Y2 - Y1;
	}

	T Depth() const {
		return Z2 - Z1;
	}
};

typedef BBox3<double> BBox3d;
typedef BBox3<float>  BBox3f;
} // namespace geom3d
} // namespace imqs