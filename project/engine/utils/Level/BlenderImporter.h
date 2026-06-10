#pragma once

// --- 標準ライブラリ ---
#include <string>
#include <vector>
#include <filesystem>

// --- エンジン側のファイル ---
#include "engine/utils/Level/LevelData.h"

class LevelEditor;

// =====================================================================
//  BlenderImporter
//   Blenderアドオンが出力したシーンJSONを読み込み、LevelEditor に反映する。
//
//   対応フォーマット（授業標準形式・キーのゆらぎにも対応）:
//   {
//     "name": "scene",
//     "objects": [{
//       "type": "MESH",
//       "name": "Cube",
//       "transform": { "translation": [x,y,z], "rotation": [x,y,z], "scaling": [x,y,z] },
//       "file_name": "block",       // モデル名（カスタムプロパティ）
//       "children": [ ...再帰... ]
//     }]
//   }
//
//   機能:
//   ・resources/blender/ 以下の JSON 一覧から選択してインポート
//   ・file_name からモデルを自動ロード（見つからなければ警告）
//   ・children 階層を親子Transform合成でフラット化
//   ・座標系変換オプション（Blender Z-up右手 → エンジン Y-up左手）
//   ・ホットリロード（ファイル更新を検知して自動再インポート）
// =====================================================================
class BlenderImporter{
public:
    // levelEditor: インポート結果の反映先（所有しない）
    void Initialize(LevelEditor* levelEditor);

    // ImGuiパネルの描画（ホットリロード監視もここで行う）
    void DrawDebugUI();

    // エクスプローラーからD&Dされたファイルを取り込む
    //   .json → シーンとしてインポート / .obj .gltf → モデルとしてロード
    void HandleDroppedFile(const std::string& path);

    // 「カメラに適用」要求を取り出す（シーン側が毎フレーム確認し、trueならカメラに反映する）
    bool ConsumeCameraRequest(Vector3& outPos, Vector3& outRot);

private:
    // 設定の保存・復元（resources/blender_importer_settings.json）
    void SaveSettings() const;
    void LoadSettings();
    // フォルダ内の .json を列挙して files_ を更新
    void ScanFolder();

    // インポート実行（成功で true）
    bool Import(const std::string& path);

    // モデルを自動ロード（FindModel で見つかれば何もしない。成功で true）
    bool EnsureModelLoaded(const std::string& modelName);

private:
    LevelEditor* levelEditor_ = nullptr;

    // --- ファイル選択 ---
    std::string folder_ = "resources/blender"; // 監視・走査するフォルダ（UIで変更可）
    std::vector<std::string> files_;           // 見つかった .json（フォルダからの相対名）
    int selectedFile_ = -1;

    // --- インポートオプション ---
    bool  convertAxes_       = true;  // Blender(Z-up右手) → エンジン(Y-up左手) 変換
    bool  rotationInDegrees_ = false; // JSONの回転を「度」として解釈（OFF=ラジアン）
    float importScale_       = 1.0f;  // 配置座標の全体倍率
    bool  additive_          = false; // true=既存マップに追記 / false=置換

    // --- ホットリロード ---
    bool hotReload_ = true;
    std::string importedPath_; // 最後にインポートしたファイル（監視対象）
    std::filesystem::file_time_type importedTime_ {};
    float pollTimer_ = 0.0f;   // ポーリング間隔用タイマー

    // --- 起動時の自動復元 ---
    bool autoImportOnStartup_ = true; // 起動時に前回のシーンを自動インポート

    // --- Blenderカメラの反映 ---
    bool    hasCamera_ = false;          // 直近のインポートで CAMERA を検出したか
    Vector3 cameraPos_ { 0.0f, 0.0f, 0.0f };
    Vector3 cameraRot_ { 0.0f, 0.0f, 0.0f };
    bool    cameraCorrection_ = true;    // Blenderカメラ(-Z前方)→エンジン(+Z前方)の補正(+90°ピッチ)
    bool    cameraApplyRequested_ = false;

    // --- 結果サマリ（パネルに表示）---
    int importedObjectCount_ = 0;
    int importCount_ = 0;                     // 累計インポート回数（ホットリロード確認用）
    std::string lastImportTimeStr_;
    std::vector<std::string> missingModels_;  // 自動ロードに失敗したモデル名
    std::vector<std::string> skippedTypes_;   // スキップした type（CAMERA等）
    std::string lastError_;
    std::string dropMessage_;                 // 最後のD&D取込結果
};
