#pragma once

#include "Vec3.h"
#include "Vec4.h"

#define XX row[0].x
#define XY row[0].y
#define XZ row[0].z
#define XW row[0].w

#define YX row[1].x
#define YY row[1].y
#define YZ row[1].z
#define YW row[1].w

#define ZX row[2].x
#define ZY row[2].y
#define ZZ row[2].z
#define ZW row[2].w

#define WX row[3].x
#define WY row[3].y
#define WZ row[3].z
#define WW row[3].w

namespace imqs {
namespace gfx {

template <typename T>
class Mat4 {
public:
	typedef Vec4<T>     Vec4T;
	typedef Vec4Base<T> Vec4BaseT;
	typedef Vec3<T>     Vec3T;
	typedef Vec3Base<T> Vec3BaseT;

	Vec4Base<T> row[4];

	T& m(int _row, int col) { return row[_row][col]; }
	T  m(int _row, int col) const { return row[_row][col]; }

	void Zero() {
		row[0] = Vec4T(0, 0, 0, 0);
		row[1] = Vec4T(0, 0, 0, 0);
		row[2] = Vec4T(0, 0, 0, 0);
		row[3] = Vec4T(0, 0, 0, 0);
	}

	void MakeIdentity() {
		row[0] = Vec4T(1, 0, 0, 0);
		row[1] = Vec4T(0, 1, 0, 0);
		row[2] = Vec4T(0, 0, 1, 0);
		row[3] = Vec4T(0, 0, 0, 1);
	}

	Mat4 Inverted() const {
		Mat4 mr;
		mr = *this;
		mr.Invert();
		return mr;
	}

	// Setup a transformation matrix ala glOrtho
	void Ortho(T left, T right, T bottom, T top, T znear, T zfar) {
		T A    = 2 / (right - left);
		T B    = 2 / (top - bottom);
		T C    = -2 / (zfar - znear);
		T tx   = -(right + left) / (right - left);
		T ty   = -(top + bottom) / (top - bottom);
		T tz   = -(zfar + znear) / (zfar - znear);
		row[0] = Vec4T(A, 0, 0, tx);
		row[1] = Vec4T(0, B, 0, ty);
		row[2] = Vec4T(0, 0, C, tz);
		row[3] = Vec4T(0, 0, 0, 1);
	}

	// equivalent to a glScale3
	void Scale(T x, T y, T z, bool post = true) {
		Mat4 m;
		m.MakeIdentity();
		m.XX = x;
		m.YY = y;
		m.ZZ = z;
		if (post)
			*this = (*this) * m;
		else
			*this = m * (*this);
	}

	void Scale(Vec3T s, bool post = true) {
		Scale(s.x, s.y, s.z, post);
	}

	// equivalent to glRotate(), except angles are in radians
	void Rotate(T angle, T x, T y, T z, bool post = true) {
		Vec3T v(x, y, z);
		v.normalize();
		x = v.x;
		y = v.y;
		z = v.z;
		Mat4 r;
		T    c   = cos(angle);
		T    s   = sin(angle);
		T    cm1 = 1 - c;
		// row 0
		r.XX = x * x * cm1 + c;
		r.XY = x * y * cm1 - z * s;
		r.XZ = x * z * cm1 + y * s;
		r.XW = 0;
		// row 1
		r.YX = y * x * cm1 + z * s;
		r.YY = y * y * cm1 + c;
		r.YZ = y * z * cm1 - x * s;
		r.YW = 0;
		// row 2
		r.ZX = z * x * cm1 - y * s;
		r.ZY = z * y * cm1 + x * s;
		r.ZZ = z * z * cm1 + c;
		r.ZW = 0;
		// row 3
		r.WX = 0;
		r.WY = 0;
		r.WZ = 0;
		r.WW = 1;
		if (post)
			*this = (*this) * r;
		else
			*this = r * (*this);
	}

	void Translate(T x, T y, T z, bool post = true) {
		Mat4 tm;
		tm.MakeIdentity();
		tm.XW = x;
		tm.YW = y;
		tm.ZW = z;
		if (post)
			*this = (*this) * tm;
		else
			*this = tm * (*this);
	}

	void Translate(Vec3T t, bool post = true) {
		Translate(t.x, t.y, t.z, post);
	}

	// Natural format is row-major. This will make the matrix column-major (useful for glUniformMatrix4fv on OpenGL ES)
	void Transpose() {
		Vec4T col[4];
		col[0] = Vec4T(row[0].x, row[1].x, row[2].x, row[3].x);
		col[1] = Vec4T(row[0].y, row[1].y, row[2].y, row[3].y);
		col[2] = Vec4T(row[0].z, row[1].z, row[2].z, row[3].z);
		col[3] = Vec4T(row[0].w, row[1].w, row[2].w, row[3].w);
		row[0] = col[0];
		row[1] = col[1];
		row[2] = col[2];
		row[3] = col[3];
	}

	void Invert() {
		/************************************************************
		*
		* input:
		* mat - pointer to array of 16 floats (source matrix)
		* output:
		* dst - pointer to array of 16 floats (invert matrix)
		*
		*************************************************************/
		//void Invert2( float *mat, float *dst)
		//Streaming SIMD Extensions - Inverse of 4x4 Matrix
		//7
		//{
		T dst[16];
		T tmp[12]; /* temp array for pairs */
		T src[16]; /* array of transpose source matrix */
		T det;     /* determinant */
		/* transpose matrix */
		/*for ( int i = 0; i < 4; i++) {
			src[i] = row[i].x;
			src[i + 4] = row[i].y;
			src[i + 8] = row[i].z;
			src[i + 12] = row[i].w;
		}*/
		// transpose
		src[0] = XX;
		src[1] = YX;
		src[2] = ZX;
		src[3] = WX;

		src[4] = XY;
		src[5] = YY;
		src[6] = ZY;
		src[7] = WY;

		src[8]  = XZ;
		src[9]  = YZ;
		src[10] = ZZ;
		src[11] = WZ;

		src[12] = XW;
		src[13] = YW;
		src[14] = ZW;
		src[15] = WW;

		/* calculate pairs for first 8 elements (cofactors) */
		tmp[0]  = src[10] * src[15];
		tmp[1]  = src[11] * src[14];
		tmp[2]  = src[9] * src[15];
		tmp[3]  = src[11] * src[13];
		tmp[4]  = src[9] * src[14];
		tmp[5]  = src[10] * src[13];
		tmp[6]  = src[8] * src[15];
		tmp[7]  = src[11] * src[12];
		tmp[8]  = src[8] * src[14];
		tmp[9]  = src[10] * src[12];
		tmp[10] = src[8] * src[13];
		tmp[11] = src[9] * src[12];
		/* calculate first 8 elements (cofactors) */
		dst[0] = tmp[0] * src[5] + tmp[3] * src[6] + tmp[4] * src[7];
		dst[0] -= tmp[1] * src[5] + tmp[2] * src[6] + tmp[5] * src[7];
		dst[1] = tmp[1] * src[4] + tmp[6] * src[6] + tmp[9] * src[7];
		dst[1] -= tmp[0] * src[4] + tmp[7] * src[6] + tmp[8] * src[7];
		dst[2] = tmp[2] * src[4] + tmp[7] * src[5] + tmp[10] * src[7];
		dst[2] -= tmp[3] * src[4] + tmp[6] * src[5] + tmp[11] * src[7];
		dst[3] = tmp[5] * src[4] + tmp[8] * src[5] + tmp[11] * src[6];
		dst[3] -= tmp[4] * src[4] + tmp[9] * src[5] + tmp[10] * src[6];
		dst[4] = tmp[1] * src[1] + tmp[2] * src[2] + tmp[5] * src[3];
		dst[4] -= tmp[0] * src[1] + tmp[3] * src[2] + tmp[4] * src[3];
		dst[5] = tmp[0] * src[0] + tmp[7] * src[2] + tmp[8] * src[3];
		dst[5] -= tmp[1] * src[0] + tmp[6] * src[2] + tmp[9] * src[3];
		dst[6] = tmp[3] * src[0] + tmp[6] * src[1] + tmp[11] * src[3];
		dst[6] -= tmp[2] * src[0] + tmp[7] * src[1] + tmp[10] * src[3];
		dst[7] = tmp[4] * src[0] + tmp[9] * src[1] + tmp[10] * src[2];
		dst[7] -= tmp[5] * src[0] + tmp[8] * src[1] + tmp[11] * src[2];
		/* calculate pairs for second 8 elements (cofactors) */
		tmp[0]  = src[2] * src[7];
		tmp[1]  = src[3] * src[6];
		tmp[2]  = src[1] * src[7];
		tmp[3]  = src[3] * src[5];
		tmp[4]  = src[1] * src[6];
		tmp[5]  = src[2] * src[5];
		tmp[6]  = src[0] * src[7];
		tmp[7]  = src[3] * src[4];
		tmp[8]  = src[0] * src[6];
		tmp[9]  = src[2] * src[4];
		tmp[10] = src[0] * src[5];
		tmp[11] = src[1] * src[4];
		/* calculate second 8 elements (cofactors) */
		dst[8] = tmp[0] * src[13] + tmp[3] * src[14] + tmp[4] * src[15];
		dst[8] -= tmp[1] * src[13] + tmp[2] * src[14] + tmp[5] * src[15];
		dst[9] = tmp[1] * src[12] + tmp[6] * src[14] + tmp[9] * src[15];
		dst[9] -= tmp[0] * src[12] + tmp[7] * src[14] + tmp[8] * src[15];
		dst[10] = tmp[2] * src[12] + tmp[7] * src[13] + tmp[10] * src[15];
		dst[10] -= tmp[3] * src[12] + tmp[6] * src[13] + tmp[11] * src[15];
		dst[11] = tmp[5] * src[12] + tmp[8] * src[13] + tmp[11] * src[14];
		dst[11] -= tmp[4] * src[12] + tmp[9] * src[13] + tmp[10] * src[14];
		dst[12] = tmp[2] * src[10] + tmp[5] * src[11] + tmp[1] * src[9];
		dst[12] -= tmp[4] * src[11] + tmp[0] * src[9] + tmp[3] * src[10];
		dst[13] = tmp[8] * src[11] + tmp[0] * src[8] + tmp[7] * src[10];
		dst[13] -= tmp[6] * src[10] + tmp[9] * src[11] + tmp[1] * src[8];
		dst[14] = tmp[6] * src[9] + tmp[11] * src[11] + tmp[3] * src[8];
		dst[14] -= tmp[10] * src[11] + tmp[2] * src[8] + tmp[7] * src[9];
		dst[15] = tmp[10] * src[10] + tmp[4] * src[8] + tmp[9] * src[9];
		dst[15] -= tmp[8] * src[9] + tmp[11] * src[10] + tmp[5] * src[8];
		/* calculate determinant */
		det = src[0] * dst[0] + src[1] * dst[1] + src[2] * dst[2] + src[3] * dst[3];
		/* calculate matrix inverse */
		det = 1 / det;
		for (int j = 0; j < 16; j++) {
			dst[j] *= det;
		}
		memcpy((void*) this, dst, 16 * sizeof(T));
	}

	T Determinant() const;
};

template <class T>
T Mat4<T>::Determinant() const {
	T sub1 = YY * (ZZ * WW - ZW * WZ) -
	         YZ * (ZY * WW - ZW * WY) +
	         YW * (ZY * WZ - ZZ * WY);

	T sub2 = YX * (ZZ * WW - ZW * WZ) -
	         YZ * (ZX * WW - ZW * WX) +
	         YW * (ZX * WZ - ZZ * WX);

	T sub3 = YX * (ZY * WW - ZW * WY) -
	         YY * (ZX * WW - ZW * WX) +
	         YW * (ZX * WY - ZY * WX);

	T sub4 = YX * (ZY * WZ - ZZ * WY) -
	         YY * (ZX * WZ - ZZ * WX) +
	         YZ * (ZX * WY - ZY * WX);
	T det = XX * sub1 - XY * sub2 + XZ * sub3 - XW * sub4;
	return det;
}
template <class T>
Mat4<T> operator*(const Mat4<T>& b, const Mat4<T>& a) {
	Mat4<T> c;

	c.XX = a.XX * b.XX + a.YX * b.XY + a.ZX * b.XZ + a.WX * b.XW;
	c.XY = a.XY * b.XX + a.YY * b.XY + a.ZY * b.XZ + a.WY * b.XW;
	c.XZ = a.XZ * b.XX + a.YZ * b.XY + a.ZZ * b.XZ + a.WZ * b.XW;
	c.XW = a.XW * b.XX + a.YW * b.XY + a.ZW * b.XZ + a.WW * b.XW;

	c.YX = a.XX * b.YX + a.YX * b.YY + a.ZX * b.YZ + a.WX * b.YW;
	c.YY = a.XY * b.YX + a.YY * b.YY + a.ZY * b.YZ + a.WY * b.YW;
	c.YZ = a.XZ * b.YX + a.YZ * b.YY + a.ZZ * b.YZ + a.WZ * b.YW;
	c.YW = a.XW * b.YX + a.YW * b.YY + a.ZW * b.YZ + a.WW * b.YW;

	c.ZX = a.XX * b.ZX + a.YX * b.ZY + a.ZX * b.ZZ + a.WX * b.ZW;
	c.ZY = a.XY * b.ZX + a.YY * b.ZY + a.ZY * b.ZZ + a.WY * b.ZW;
	c.ZZ = a.XZ * b.ZX + a.YZ * b.ZY + a.ZZ * b.ZZ + a.WZ * b.ZW;
	c.ZW = a.XW * b.ZX + a.YW * b.ZY + a.ZW * b.ZZ + a.WW * b.ZW;

	c.WX = a.XX * b.WX + a.YX * b.WY + a.ZX * b.WZ + a.WX * b.WW;
	c.WY = a.XY * b.WX + a.YY * b.WY + a.ZY * b.WZ + a.WY * b.WW;
	c.WZ = a.XZ * b.WX + a.YZ * b.WY + a.ZZ * b.WZ + a.WZ * b.WW;
	c.WW = a.XW * b.WX + a.YW * b.WY + a.ZW * b.WZ + a.WW * b.WW;

	return c;
}

template <class T>
Mat4<T> operator+(const Mat4<T>& a, const Mat4<T>& b) {
	Mat4<T> c;

	c.XX = a.XX + b.XX;
	c.XY = a.XY + b.XY;
	c.XZ = a.XZ + b.XZ;
	c.XW = a.XW + b.XW;

	c.YX = a.YX + b.YX;
	c.YY = a.YY + b.YY;
	c.YZ = a.YZ + b.YZ;
	c.YW = a.YW + b.YW;

	c.ZX = a.ZX + b.ZX;
	c.ZY = a.ZY + b.ZY;
	c.ZZ = a.ZZ + b.ZZ;
	c.ZW = a.ZW + b.ZW;

	c.WX = a.WX + b.WX;
	c.WY = a.WY + b.WY;
	c.WZ = a.WZ + b.WZ;
	c.WW = a.WW + b.WW;

	return c;
}

template <class T>
Mat4<T> operator-(const Mat4<T>& a, const Mat4<T>& b) {
	Mat4<T> c;

	c.XX = a.XX - b.XX;
	c.XY = a.XY - b.XY;
	c.XZ = a.XZ - b.XZ;
	c.XW = a.XW - b.XW;

	c.YX = a.YX - b.YX;
	c.YY = a.YY - b.YY;
	c.YZ = a.YZ - b.YZ;
	c.YW = a.YW - b.YW;

	c.ZX = a.ZX - b.ZX;
	c.ZY = a.ZY - b.ZY;
	c.ZZ = a.ZZ - b.ZZ;
	c.ZW = a.ZW - b.ZW;

	c.WX = a.WX - b.WX;
	c.WY = a.WY - b.WY;
	c.WZ = a.WZ - b.WZ;
	c.WW = a.WW - b.WW;

	return c;
}

template <class T>
Vec4<T> operator*(const Mat4<T>& a, const Vec4<T>& v) {
	return Vec4<T>(
	    v.x * a.XX + v.y * a.XY + v.z * a.XZ + v.w * a.XW,
	    v.x * a.YX + v.y * a.YY + v.z * a.YZ + v.w * a.YW,
	    v.x * a.ZX + v.y * a.ZY + v.z * a.ZZ + v.w * a.ZW,
	    v.x * a.WX + v.y * a.WY + v.z * a.WZ + v.w * a.WW);
}

typedef Mat4<double> Mat4d;
typedef Mat4<float>  Mat4f;

#undef XX
#undef XY
#undef XZ
#undef XW

#undef YX
#undef YY
#undef YZ
#undef YW

#undef ZX
#undef ZY
#undef ZZ
#undef ZW

#undef WX
#undef WY
#undef WZ
#undef WW

} // namespace gfx
} // namespace imqs
