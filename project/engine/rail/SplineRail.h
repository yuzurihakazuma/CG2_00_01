#pragma once
#include <vector>
#include "engine/math/struct.h"

class SplineRail{
public:
    std::vector<Vector3> nodes;

    // ============================================================
    // レールのタイプ：このレール上での移動操作キーを決める
    //   Horizontal(横) … A/D で移動、W/S で縦レールへ乗り換え
    //   Vertical(縦)   … W/S で移動、A/D で横レールへ乗り換え
    // タイプはレール固定（毎フレーム接線から判定しないので操作がブレない）
    // ============================================================
    enum class RailType { Horizontal, Vertical };
    RailType type = RailType::Horizontal;

    // front→back ベクトルの主軸からタイプを自動判定（|X|>=|Z|→横／それ以外→縦）
    void AutoDetectType();

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

    // ============================================================
    // 【推奨API】距離(s)ベースの公開関数。利用側は t を意識せず、これだけ使えばよい。
    //   - 内部の曲線パラメータ t は隠蔽し、すべて「レール上を進んだ距離(m)」で扱う
    //   - これにより等速移動・端点の安定・乗り換えがシンプルになる
    // ============================================================

    // 距離 s における座標を返す（s は 0〜GetLength() にクランプ）
    Vector3 GetPositionByDistance(float distance) const;

    // 距離 s における進行方向（単位ベクトル）を返す。距離空間で前後サンプルするため端でも安定
    Vector3 GetTangentByDistance(float distance) const;

    // レール全長(m)
    float GetLength() const { return totalLength_; }

    // 指定ワールド座標に最も近いレール上の距離 s を返す（スナップ／分岐検出用）
    float GetClosestDistance(const Vector3& worldPos) const;

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