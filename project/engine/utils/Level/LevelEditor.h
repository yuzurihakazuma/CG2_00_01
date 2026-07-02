#pragma once

#include "engine/utils/Level/LevelData.h"
#include "LevelManager.h"
#include <vector>
#include <memory>
#include <string>

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

    // Blenderインポータ等の外部から変換済みデータを受け取って反映する
    //   additive: true=既存マップに追記 / false=置き換え
    void ApplyImportedData(const LevelData& data, bool additive);

	// デバッグ用UIの描画
    void DrawDebugUI();

    // resources/ を走査してモデルアセット一覧を更新する
    void ScanAssets();

    // resources/map/ を走査してマップファイル一覧を更新する
    void ScanMaps();

    // 現在開いているファイルに上書き保存する（名前入力不要）
    void QuickSave();

    // 見える位置（カメラ前方）にモデルを1個配置する
    // （ファイルエディタなど外部からも使えるよう public）
    void SpawnObject(const std::string& type);

    // --- Undo / Redo ---
    void PushUndo();   // 変更「前」に呼んで履歴に積む
    void Undo();       // Ctrl+Z
    void Redo();       // Ctrl+Y

    // --- ノードエディタ用アクセス（配置オブジェクトをノードから動かす） ---
    int         GetObjectCount() const;
    std::string GetObjectLabel(int index) const;    // 表示用 "0: block"
    void        SetObjectPosY(int index, float y);  // Y座標を反映
    void        SetObjectRotY(int index, float r);  // Y回転を反映（ラジアン）
    void        SetObjectScale(int index, float s); // 均一スケールを反映
    void        SetObjectShaderParam(int index, float v); // シェーダーパラメータ(0〜1)を反映
    Obj3d*      GetObject3d(int index);             // 表示オブジェクトを取得（シェーダー適用用）

private:
    // カメラの前方にあるスポーン地点を計算する
    Vector3 CalcSpawnPoint() const;
    // levelData_ の内容から表示用 object3ds_ を作り直す（Undo/Redo用）
    void RebuildObjects();

    // Undo/Redo 履歴（LevelData のスナップショット）
    std::vector<LevelData> undoStack_;
    std::vector<LevelData> redoStack_;

    // アセットブラウザ用：resources/ から見つけたモデル1件分
    struct AssetEntry{
        std::string name;     // モデル名（FindModel/LoadModel に渡すキー＝ファイル名から拡張子を除いたもの）
        std::string dir;      // ディレクトリ（LoadModel 用）
        std::string file;     // ファイル名（LoadModel 用）
        std::string display;  // 表示用（resources からの相対パス）
    };

    // モデルが未ロードならアセット一覧から探してロードする
    bool EnsureAssetLoaded(const std::string& name);

	// カメラは所有しない参照（描画のときに使う）
    const Camera* camera_ = nullptr;

    // アセットブラウザ（実フォルダ走査の結果）
    std::vector<AssetEntry> assetList_;

    LevelData levelData_; // 現在のマップデータ
    std::vector<std::unique_ptr<Obj3d>> object3ds_; // 配置された3Dオブジェクト
    int selectedObjectIndex_ = -1; // 選択中のオブジェクト番号

    std::string saveFileName_ = "map01.json"; // 別名保存/新規作成用のファイル名
    bool snapToGrid_ = true;

    // --- マップファイル選択 / 保存 ---
    std::vector<std::string> mapList_;      // resources/map/ 内の *.json
    int selectedMapIndex_ = -1;             // 一覧で選択中のマップ
    std::string currentMapFile_ = "resources/map/map01.json"; // 今開いているファイル
    bool  dirty_ = false;                   // 未保存の変更があるか
    bool  autoSave_ = false;                // 自動保存 ON/OFF
    float autoSaveTimer_ = 0.0f;            // 自動保存までのフレームカウンタ
    int   spawnCounter_ = 0;                // 連続配置時に少しずらすためのカウンタ

	//bool isEditorActive = true; // エディタのアクティブ状態

};