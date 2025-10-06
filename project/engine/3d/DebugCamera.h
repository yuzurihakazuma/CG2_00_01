//#pragma once
//
//#include"struct.h"
//#include"matrix4x4.h"
//#include <windows.h>
//#include <cmath>
//// デバックカメラのクラス
//
//using namespace matrixmath;
//
//class debugcamera{
//public:
//
//
//
//	// 初期化
//	void initialize();
//	// 更新
//	void update();
//	// 描画
//	void draw();
//
//	void mousemove(float dx, float dy);
//
//	const matrix4x4& getviewmatrix() const{return viewmatrix_;}
//	
//	bool isactive() const{ return isdebugcamera_; } // 追加
//	
//private:
//
//	// カメラのローカル回転
//	vector3 rotation_;
//
//	//カメラの位置
//	vector3 translation_;
//	// ビュー行列
//	matrix4x4 viewmatrix_ = makeidentity4x4();
//	// 射影行列
//	matrix4x4 projectionmatrix_ = makeidentity4x4();
//
//	// ビュー行列の更新
//	void updateviewmatrix();
//	// 入力に夜カメラの移動や回転を行う
//	void inputupdate();
//
//	bool isdebugcamera_ = false; // デフォルトは通常カメラ
//	bool prevtogglekey_ = false; // トグル用前フレームキー
//
//};
//
