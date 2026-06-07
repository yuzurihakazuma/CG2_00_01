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

    // レール表示用のオブジェクトを一括で作り直す関数
    void RebuildRailPoints();

};