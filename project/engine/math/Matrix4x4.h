#pragma once
#include "engine/math/struct.h"

namespace MatrixMath {
	// ---------------------------------------------
	// 行列計算
	// ---------------------------------------------
	Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);
	Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);
	Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
	Matrix4x4 Inverse(const Matrix4x4& m);
	Matrix4x4 Transpose(const Matrix4x4& m);
	Matrix4x4 MakeIdentity4x4();

	// ---------------------------------------------
	// アフィン変換行列
	// ---------------------------------------------
	Matrix4x4 MakeTranslate(const Vector3& translate);
	Matrix4x4 MakeScale(const Vector3& scale);
	Matrix4x4 MakeRotateX(float radian);
	Matrix4x4 MakeRotateY(float radian);
	Matrix4x4 MakeRotateZ(float radian);

	Matrix4x4 MakeDirectionRotation(const Vector3& forward, const Vector3& up = { 0.0f, 1.0f, 0.0f });

	Matrix4x4 MakeAffine(const Vector3& scale, const Vector3& rotate, const Vector3& translate);

	// Quaternion を使ったアフィン変換行列（スキニング・アニメーション用）
	Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);

	// ---------------------------------------------
	// 座標変換 (ベクトル × 行列)
	// ---------------------------------------------
	Vector3 Transforms(const Vector3& vector, const Matrix4x4& matrix);

	// ---------------------------------------------
	// 投影行列・ビューポート変換
	// ---------------------------------------------
	Matrix4x4 Orthographic(float left, float top, float right, float bottom, float nearClip, float farClip);
	Matrix4x4 PerspectiveFov(float fovY, float aspectRatio, float nearClip, float farClip);
	Matrix4x4 Viewport(float left, float top, float width, float height, float minDepth, float maxDepth);
}