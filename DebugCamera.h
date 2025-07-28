#pragma once

#include"struct.h"
#include"Matrix4x4.h"
#include <Windows.h>
#include <cmath>
// デバックカメラのクラス

using namespace MatrixMath;

class DebugCamera{
public:


	void deleteCamera();

	// 初期化
	void Initialize();
	// 更新
	void Update();
	// 描画
	void Draw();

	void MouseMove(float dx, float dy);

	const Matrix4x4& GetViewMatrix() const{return viewMatrix_;}
	
	bool IsActive() const{ return isDebugCamera_; } // 追加
	
private:

	// カメラのローカル回転
	Vector3 rotation_;

	//カメラの位置
	Vector3 translation_;
	// ビュー行列
	Matrix4x4 viewMatrix_ = MakeIdentity4x4();
	// 射影行列
	Matrix4x4 projectionMatrix_ = MakeIdentity4x4();

	// ビュー行列の更新
	void UpdateViewMatrix();
	// 入力に夜カメラの移動や回転を行う
	void InputUpdate();

	bool isDebugCamera_ = false; // デフォルトは通常カメラ
	bool prevToggleKey_ = false; // トグル用前フレームキー

};

