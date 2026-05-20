#pragma once
#include <vector>
#include "engine/math/struct.h"

class SplineRail{
public:
    std::vector<Vector3> nodes;

    // --- ここから等速移動用の追加コード ---

    // 距離の計測テーブル
    std::vector<float> distanceTable_;
    std::vector<float> tTable_;
    float totalLength_ = 0.0f;

    // 座標を計算する関数（既存のものと仮定）
    Vector3 EvaluatePosition(float t) const;

    // ① レールの長さを事前に計算する関数（ノードを追加/削除した時に1回だけ呼ぶ）
    void BuildDistanceTable();

    // ② 進みたい「距離(Distance)」から、スプラインの「t」を逆算する関数
    float GetTFromDistance(float targetDistance) const;

    // ③ 現在のtにおける「進行方向（接線）」を求める関数（カメラ用）
    Vector3 EvaluateTangent(float t) const;
};