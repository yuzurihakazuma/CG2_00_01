#pragma once
#include "engine/math/VectorMath.h"
#include "engine/math/struct.h"
#include <vector>
#include <memory>


// レールのノード構造体
struct RailNode{

	Vector3 position; // ノードの位置

	float cameraFov; // カメラのFOV（視野角）をこのノードで指定できるようにするための変数

};


// スプラインレールクラス
class SplineRail{
public:
	// このレールを構成する制御点のリスト
	std::vector<RailNode> nodes;

	// 指定した進行度(T)におけるワールド座標を計算して返す
	Vector3 EvaluatePosition(float totalT) const;

	// デバッグ用の描画
	void DrawDebug();

private:

};

