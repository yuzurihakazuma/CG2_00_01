#pragma once
#include "engine/math/struct.h"

namespace VectorMath {
	// ベクトルの加算・減算
	Vector3 Add(const Vector3& v1, const Vector3& v2);
	Vector3 Subtract(const Vector3& v1, const Vector3& v2);

	// スカラー倍
	Vector3 Multiply(float scalar, const Vector3& v);

	// 内積
	float Dot(const Vector3& v1, const Vector3& v2);
	// 外積
	Vector3 Cross(const Vector3& a, const Vector3& b);

	// 長さ
	float Length(const Vector3& v);
	// 正規化
	Vector3 Normalize(const Vector3& v);

	Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);

}