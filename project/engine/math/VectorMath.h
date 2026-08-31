#pragma once
#include "engine/math/struct.h"

namespace VectorMath {
	// ベクトルの加算・減算
	Vector3 Add(const Vector3& v1, const Vector3& v2);
	Vector3 Subtract(const Vector3& v1, const Vector3& v2);

	// スカラー倍
	Vector3 Multiply(float scalar, const Vector3& v);
	Vector3 Multiply(const Vector3& v, float scalar); // 引数順の別形（v * s と同じ）

	// 内積
	float Dot(const Vector3& v1, const Vector3& v2);
	// 外積
	Vector3 Cross(const Vector3& a, const Vector3& b);

	// 長さ／長さの2乗（比較だけなら sqrt 不要の LengthSq が速い）
	float Length(const Vector3& v);
	float LengthSq(const Vector3& v);

	// 正規化（長さ0なら v をそのまま返す）
	Vector3 Normalize(const Vector3& v);
	// 正規化（長さがほぼ0なら fallback を返す。方向が必須の計算で NaN を出さないため）
	Vector3 NormalizeSafe(const Vector3& v, const Vector3& fallback);

	// 線形補間（t=0 で a、t=1 で b）
	Vector3 Lerp(const Vector3& a, const Vector3& b, float t);

	Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t);

}