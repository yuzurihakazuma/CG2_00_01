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


    // ④ 逆に、tから距離を求める関数もあると便利（必要に応じて）
    float GetDistanceFromT(float t) const;

    // ⑤ 終端の接続情報（インデックスで管理・距離チェック不要）
    int  frontConnIndex = -1;    // front端の接続先レール番号 (-1=なし)
    bool frontConnToFront = true;  // true=接続先のfront, false=back
    int  backConnIndex = -1;    // back端の接続先レール番号
    bool backConnToFront = true;

    // ⑥ 途中分岐情報（BuildRailConnections でロード時に1回だけ計算）
    struct BranchPoint {
        float distance;      // このレール上の分岐距離
        int   targetRail;    // 乗り換え先レール番号
        float targetDist;    // 乗り換え先レール上の着地距離
        int   zSign;         // +1=W/Dキー(Z+方向), -1=S/Aキー(Z-方向)
    };
    std::vector<BranchPoint> branchPoints;
};