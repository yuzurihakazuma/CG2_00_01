#pragma once
#include "engine/rail/SplineRail.h"
#include "engine/math/struct.h"
#include "engine/3d/model/Model.h"
#include <vector>
#include <memory>
#include <cstdint>

class Obj3d;
class Camera;

// =====================================================================
//  RailField：実行時のレール管理。
//   ・エディタ(LevelEditor)の最新データから実行用レール rails_ を作り直す
//   ・緑線マーカー（穴区間は赤）で経路を可視化する
//   ・動くレール(motionAmp)を時間で進める
//  ゲーム側（プレイヤー・敵・描画・敵エディタ）は GetRails() でこのレールを参照する。
//  ※敵の生成は RailField の責務ではない（シーンが Sync 後に呼ぶ）。
// =====================================================================
class RailField {
public:
    RailField();
    ~RailField();

    // エディタ保持の最新レールから rails_ を作り直し、マーカーも再構築する。
    //   camera        : マーカーに割り当てるカメラ（描画用）
    //   whiteTexIndex : 単色化用の白テクスチャの SRV インデックス（0=未使用）
    void Sync(Camera* camera, uint32_t whiteTexIndex);

    // 動くレールを進める（プレイヤー更新より先に呼ぶ）。
    //   ridingRail: プレイヤーが今乗っているレール番号（「乗ったら動き出す」レールの発動判定。
    //   -1=誰も乗っていない / kMotionStartAll=全レール強制発動＝エディタのプレビュー用）
    static constexpr int kMotionStartAll = -2;
    void UpdateMotion(float dt, int ridingRail = -1);
    void ResetMotion();          // 編集モードへ戻った時：動くレールを基準位置へ戻す
    void UpdateMarkers();         // マーカーの行列更新（毎フレーム。カメラ移動に追従）
    void DrawMarkers() const;     // マーカー描画
    void RebuildMarkers();        // マーカーだけ作り直す（デバッグUI用。Sync 済み前提）

    // ゲーム側が参照する実行時レール
    const std::vector<SplineRail>& GetRails() const { return rails_; }

    int  Version() const { return lastVersion_; }       // 直近に同期したエディタの編集世代
    int  MarkerCount() const { return ( int ) markerSlotsUsed_; }
    bool ShowMarkers() const { return showMarkers_; }
    void SetShowMarkers(bool v) { showMarkers_ = v; }

    // --- スタート/ゴール地点（マップ設定から Sync 時に距離へ変換済み）---
    int   GetStartRail() const { return startRail_; }
    float GetStartDistance() const { return startDist_; }
    bool  HasGoal() const { return goalRail_ >= 0 && goalRail_ < ( int ) rails_.size(); }
    // ゴールの現在ワールド座標（動くレール上でも追従する）
    Vector3 GetGoalPos() const {
        if ( !HasGoal() ) return { 0.0f, 0.0f, 0.0f };
        return rails_[goalRail_].GetPositionByDistance(goalDist_);
    }

private:
    void BuildMarkers();          // rails_ をサンプルして線マーカーを作り直す
    void UpdateMarkerPositions(); // マーカー位置 = 基準位置 + そのレールの animOffset

    std::vector<SplineRail> rails_;                    // 実行用レール本体

    // 緑線マーカー：レール1本 = リボンメッシュ1個（穴があるレールは赤リボンをもう1個）。
    //   0.5m毎の Obj3d 群をやめ、固定容量の動的バッファを使い回す
    //   （ドローコールが約220→レール本数になり、編集中の Obj3d 生成もゼロ）
    struct MarkerSlot {
        std::unique_ptr<Model> model;
        std::unique_ptr<Obj3d> obj;
        int rail = -1;
    };
    // 生成したリボンを空きスロットへ書き込む（不足時のみ新規確保）
    void EmitMarker(const Model::ModelData& data, int railIdx, const Vector4& color);
    std::vector<std::unique_ptr<MarkerSlot>> markerSlots_;
    size_t markerSlotsUsed_ = 0;

    float    animTime_ = 0.0f;     // 動くレール用の経過時間
    int      lastVersion_ = -1;    // 直近に同期したエディタ編集世代
    bool     showMarkers_ = true;  // 緑線表示ON/OFF

    // スタート/ゴール（Sync 時にノード番号から距離へ変換して保持）
    int   startRail_ = 0;   float startDist_ = 0.0f;
    int   goalRail_  = -1;  float goalDist_  = 0.0f;

    Camera*  camera_ = nullptr;    // 直近 Sync のカメラ（マーカー生成・再構築に使う）
    uint32_t whiteTexIndex_ = 0;   // 単色化用テクスチャ
};
