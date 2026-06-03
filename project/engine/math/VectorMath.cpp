#include "VectorMath.h"
#include <cmath>

namespace VectorMath {

	Vector3 Add(const Vector3& v1, const Vector3& v2) {
		return v1 + v2;
	}

	Vector3 Subtract(const Vector3& v1, const Vector3& v2) {
		return v1 - v2;
	}

	Vector3 Multiply(float scalar, const Vector3& v) {
		return v * scalar;
	}

	float Dot(const Vector3& v1, const Vector3& v2) {
		return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
	}

	Vector3 Cross(const Vector3& a, const Vector3& b) {
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		};
	}

	float Length(const Vector3& v) {
		return std::sqrt(Dot(v, v));
	}

	Vector3 Normalize(const Vector3& v) {
		float len = Length(v);
		if (len != 0.0f) {
			return v / len;
		}
		return v;
	}

	Vector3 CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t){
		
		// Catmull-Romスプラインの公式を使って、tに応じた位置を計算する
		float t2 = t * t;
		// t3はtの3乗
		float t3 = t2 * t;

		// t0の項は、p1を2倍して足す（p0とp2は0倍、p3も0倍なので無視できる）
		Vector3 v0 = p1 * 2.0f;

		// tの項は、p0を-1倍、p1を0倍、p2を1倍、p3を0倍して足し合わせる
		Vector3 v1 = ( -p0 - p2 ) * t;

		// t2の項は、p0を1倍、p1を-5倍、p2を4倍、p3を-1倍して足し合わせる
		Vector3 v2 = ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * t2;
		
		// t3の項は、p0を-1倍、p1を3倍、p2を-3倍、p3を1倍して足し合わせる
		Vector3 v3 = ( -p0 + p1 * 3.0f - p2 * 3.0f + p3 ) * t3;
		// 最後に全ての項を足して、0.5倍する
		return ( v0 + v1 + v2 + v3 ) * 0.5f;
	}




} // namespace VectorMath