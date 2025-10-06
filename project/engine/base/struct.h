#pragma once


// 4x4行列
struct Matrix4x4{

	float m[4][4];
};
// 3x3行列
struct Matrix3x3{
	float m[3][3];
};
// 2次元ベクトル
struct Vector2{
	float x, y;
};
// 3次元ベクトル
struct Vector3{
	float x, y, z;
};
// 4次元ベクトル
struct Vector4{
	float x, y, z, w;
};
// 変換行列
struct Transform{
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
};