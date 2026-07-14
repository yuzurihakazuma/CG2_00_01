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

    // ============================================================
    // ループ（円状レール）：front と back が同じ位置に溶接された閉じたレール。
    //   BuildRailConnections で自動検出される。
    //   ループ中は距離が周回でラップし、端が無いので A/D(W/S) で回り続けられる。
    // ============================================================
    bool isLoop = false;

    // ============================================================
    // 表示フラグ：false なら「見えないレール（連結用）」。
    //   プレイヤーは普通にこの上を距離で進む（レール移動らしく滑らかに見える）が、
    //   ゲーム中は緑線(マーカー)を描かない。別々のレールを繋ぐ橋として使う。
    // ============================================================
    bool visible = true;

    // ============================================================
    // 線のつなぎ方（0=スプライン / 1=直線）。
    //   スプライン … 置いた点を全部通りながら、なめらかな曲線でつなぐ（従来どおり）
    //   直線       … 点をそのまま直線でつなぐ（角でカクッと折れる。階段や直角コース用）
    //   ノードの位置は同じで「つなぎ方」だけが違う。エディタからレール単位で切替できる。
    // ============================================================
    int lineMode = 0;

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

    // ============================================================
    // レールの地面タイプ
    //   Safe     = 端でクランプ（落ちない安全レール）
    //   Gap      = 端から飛び出せる（ギャップジャンプ・動くレール向け）
    //   NoGround = そのレール上に地面がない（入ったら即落下＝穴）
    // ============================================================
    enum class GroundType { Safe = 0, Gap = 1, NoGround = 2 };
    GroundType groundType = GroundType::Safe;

    // ============================================================
    // ノード単位の「穴」指定（nodes と同じ並び・1=穴）。
    //   エディタで「ここを穴にする」と指定したノード付近の区間が穴になる。
    //   プレイヤーはその区間に来ると落下する（ジャンプで飛び越え可）。
    // ============================================================
    std::vector<int> nodeHole;

    // 指定距離(s)が「穴」区間か？（最も近いノードが穴指定なら true）
    bool IsHoleAtDistance(float distance) const;

    // ============================================================
    // ⑦ 動くレール（ムービングプラットフォーム）
    //   motionAmp が非ゼロなら、時間に応じて sin 波でレール全体が平行移動する。
    //   形は変わらない剛体移動なので、距離テーブル・全長はそのまま使える。
    //   プレイヤーはレール上の距離から位置を毎フレーム計算するので自動で追従する。
    // ============================================================
    Vector3 motionAmp { 0.0f, 0.0f, 0.0f };   // 振幅(m)：各軸 ±この量だけ往復する
    float   motionPeriod = 2.0f;              // 1往復にかかる秒数
    Vector3 animOffset { 0.0f, 0.0f, 0.0f };  // 現在のオフセット（ゲーム側が毎フレーム更新）

    // 動きの波形と位相（エディタで選択）
    //   motionType: 0=サイン往復 / 1=端で一時停止する往復 / 2=円運動(cos,sin)
    //   motionPhase: 0〜1。複数レールの動きをずらす（0.5で半周期ずれ）
    int   motionType  = 0;
    float motionPhase = 0.0f;

    // 片方向レール（0=両方向 / 1=正方向のみ(front→back) / 2=逆方向のみ(back→front)）
    int oneWay = 0;

    // このレール上での移動速度倍率（1=通常。2で加速レール、0.5で減速レール）
    float speedMul = 1.0f;

    // 動きが設定されているか
    bool HasMotion() const {
        return motionAmp.x != 0.0f || motionAmp.y != 0.0f || motionAmp.z != 0.0f;
    }

    // ⑥ 途中分岐情報（BuildRailConnections でロード時に1回だけ計算）
    struct BranchPoint {
        float distance;      // このレール上の分岐距離
        int   targetRail;    // 乗り換え先レール番号
        float targetDist;    // 乗り換え先レール上の着地距離
        int   zSign;         // +1=W/Dキー(Z+方向), -1=S/Aキー(Z-方向)
    };
    std::vector<BranchPoint> branchPoints;
};