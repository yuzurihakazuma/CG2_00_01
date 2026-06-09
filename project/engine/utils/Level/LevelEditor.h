#pragma once

#include "engine/utils/Level/LevelData.h"
#include "LevelManager.h"
#include <vector>
#include <memory>
#include <string>

#include "engine/rail/SplineRail.h"

class Obj3d;
class Camera;

// マップエディタ専用クラス
class LevelEditor{
public:

    LevelEditor();
    ~LevelEditor();

    // 初期化
    void Initialize();
    // 毎フレームの処理（ImGuiの表示とオブジェクトの更新）
    void Update();
    // 配置したオブジェクトの描画
    void Draw();

    // マップの読み込み＆生成
    void LoadAndCreateMap(const std::string& fileName);
    // カメラのセット
    void SetCamera(const Camera* camera);

    // デバッグ用UIの描画
    void DrawDebugUI();

    // --- レール編集データの公開（ゲーム側が同じレールを参照するため）---
    int GetRailVersion() const { return railVersion_; }
    const std::vector<std::vector<Vector3>>& GetRailLines() const { return levelData_.railLines; }
    const std::vector<int>& GetRailTypes() const { return levelData_.railTypes; }

    // --- マウス編集サポート（EditorManager が Game View 上で使う）---
    int  GetCurrentRailIndex() const { return currentEditRailIndex_; }
    int  GetCurrentRailNodeCount() const;                 // 現在編集中レールのノード数
    bool GetRailNodePos(int idx, Vector3& out) const;     // ノード座標を取得
    void SetRailNodePos(int idx, const Vector3& p);       // ノードを移動（世代更新）
    void AppendRailNodeAt(const Vector3& p);              // 末尾にノード追加（世代更新・選択）
    int  GetSelectedRailNode() const { return selectedRailNode_; }
    void SetSelectedRailNode(int idx) { selectedRailNode_ = idx; }
    bool  IsRailDrawMode() const { return railDrawMode_; } // 地面クリックで追加するモードか
    float GetRailDrawHeight() const { return railDrawHeight_; } // 地面クリック時のY高さ
    bool  IsRailSnap() const { return railSnap_; }            // グリッド吸着ON/OFF
    float GetRailGridSize() const { return railGridSize_; }   // グリッド間隔(m)
    // 前のノードから相対(dx,dy,dz)に新ノードを追加（方向ボタン用・スナップ適用）
    void AppendRailNodeRelative(float dx, float dy, float dz);

private:
    // カメラは所有しない参照（描画のときに使う）
    const Camera* camera_ = nullptr;

    // レール編集の世代番号（編集のたびに増やし、ゲーム側が変化を検知する）
    int railVersion_ = 0;

    LevelData levelData_; // 現在のマップデータ
    std::vector<std::unique_ptr<Obj3d>> object3ds_; // 配置された3Dオブジェクト
    int selectedObjectIndex_ = -1; // 選択中のオブジェクト番号

    std::string saveFileName_ = "map01.json"; // ファイル名
    bool snapToGrid_ = true;

    // すべての路線の球体とパスポイントを2次元配列で保持する
    std::vector<std::vector<std::unique_ptr<Obj3d>>> railSpheresAll_;
    std::vector<std::vector<std::unique_ptr<Obj3d>>> pathPointsAll_;

    int selectedRailNode_ = -1;
    int currentEditRailIndex_ = 0; // 現在編集しているレールの番号

    // マウス編集モード
    bool  railDrawMode_   = false; // true=地面クリックで末尾ノード追加
    float railDrawHeight_ = 1.5f;  // 地面クリック時に置くY高さ(m)

    // グリッド・直角設定（マウスもボタンも共通で使う）
    bool  railSnap_     = true;    // ノードをグリッドに吸着
    float railGridSize_ = 1.0f;    // グリッド間隔(m)
    bool  railAxisLock_ = true;    // 直角モード：新ノードを前ノードから X or Z 軸のみに固定

    // 値をグリッドに丸める（railSnap_ がOFFならそのまま）
    float SnapValue(float v) const;

    // レール表示用のオブジェクトを一括で作り直す関数
    void RebuildRailPoints();

};