#pragma once
#include "engine/rail/SplineRail.h"
#include "engine/math/struct.h"
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

    void UpdateMotion(float dt); // 動くレールを進める（プレイヤー更新より先に呼ぶ）
    void ResetMotion();          // 編集モードへ戻った時：動くレールを基準位置へ戻す
    void UpdateMarkers();         // マーカーの行列更新（毎フレーム。カメラ移動に追従）
    void DrawMarkers() const;     // マーカー描画
    void RebuildMarkers();        // マーカーだけ作り直す（デバッグUI用。Sync 済み前提）

    // ゲーム側が参照する実行時レール
    const std::vector<SplineRail>& GetRails() const { return rails_; }

    int  Version() const { return lastVersion_; }       // 直近に同期したエディタの編集世代
    int  MarkerCount() const { return ( int ) markers_.size(); }
    bool ShowMarkers() const { return showMarkers_; }
    void SetShowMarkers(bool v) { showMarkers_ = v; }

private:
    void BuildMarkers();          // rails_ をサンプルして線マーカーを作り直す
    void UpdateMarkerPositions(); // マーカー位置 = 基準位置 + そのレールの animOffset

    std::vector<SplineRail> rails_;                    // 実行用レール本体
    std::vector<std::unique_ptr<Obj3d>> markers_;      // 緑線マーカー（細いバーの集合）
    std::vector<int>     markerRail_;                  // 各マーカーが属するレール番号
    std::vector<Vector3> markerBase_;                  // 各マーカーの基準位置（オフセット0換算）

    float    animTime_ = 0.0f;     // 動くレール用の経過時間
    int      lastVersion_ = -1;    // 直近に同期したエディタ編集世代
    bool     showMarkers_ = true;  // 緑線表示ON/OFF

    Camera*  camera_ = nullptr;    // 直近 Sync のカメラ（マーカー生成・再構築に使う）
    uint32_t whiteTexIndex_ = 0;   // 単色化用テクスチャ
};
