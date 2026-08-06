#pragma once

#include "engine/utils/Level/LevelData.h"
#include "LevelManager.h"
#include <vector>
#include <memory>
#include <string>

#include "engine/rail/SplineRail.h"

class Obj3d;
class Camera;
class RailEditor;

// マップエディタ専用クラス（オブジェクト配置・マップ保存／読込担当）。
// レール編集は RailEditor クラスへ分離し、ここからは GetRailEditor() で公開する。
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
    // エディタUIの描画。引数でウィンドウ単位の表示を絞れる（アイコンモードのカテゴリ開閉用。
    //   既定は全true＝従来どおり。非表示でもレール編集の毎フレーム処理(TickEditing)は必ず動く）
    void DrawDebugUI(bool showHierarchy = true, bool showInspector = true, bool showAssets = true,
                     bool showRail = true, bool showCamera = true, bool showItems = true);

    // Blenderインポータ等の外部から変換済みデータを受け取って反映する
    //   additive: true=既存マップに追記 / false=置き換え
    void ApplyImportedData(const LevelData& data, bool additive);

    // resources/ を走査してモデルアセット一覧を更新する
    void ScanAssets();

    // --- レール編集（専用クラス）への入口 ---
    // EditorManager が Game View のピッキングで、ゲーム側がレール参照で使う。
    RailEditor* GetRailEditor() const{ return railEditor_.get(); }

    // --- 敵の配置（マップと一緒に保存/読込する）。シーンの EnemyEditor と同期する ---
    void SetEnemyData(const std::vector<LevelEnemyData>& e){
        // 内容が変わった時だけ未保存マーク（これが無いと敵の配置だけでは自動保存が発動せず、
        // 「敵を置いたのに保存されていない」事故になる）
        if ( levelData_.enemies != e ) { dirty_ = true; }
        levelData_.enemies = e;
    }
    // 外部（ゲームビューのコイン配置/移動/削除など）からの未保存マーク。
    //   コインは RailEditor が LevelData を直接書くため、ここで dirty を立てないと
    //   [未保存] 表示も自動保存も発動しない（SetXxxData系と同じ落とし穴）
    void MarkDirty(){ dirty_ = true; }
    const std::vector<LevelEnemyData>& GetEnemyData() const{ return levelData_.enemies; }
    // マップを読み込んだ回数（増えたら＝新しいマップが読み込まれた合図）
    int GetMapLoadVersion() const{ return mapLoadVersion_; }

    // --- マップファイル管理（master_engine から移植） ---
    void ScanMaps();    // resources/map/ の *.json 一覧を更新する
    void QuickSave();   // 今開いているファイルへ上書き保存（Ctrl+S / 自動保存もここを通る）

    // --- オブジェクト配置の Undo/Redo（レールは RailEditor 側の履歴が担当） ---
    void PushUndo();    // 変更「前」に呼んで履歴に積む
    void Undo();        // Ctrl+Z
    void Redo();        // Ctrl+Y

    // --- 外部ツール連携（FileEditor / NodeEditor から使う。master_engine から移植） ---
    // モデル名を指定してカメラの前に1個配置する（FileEditor のダブルクリック配置用）
    void SpawnObject(const std::string& type);
    // 配置オブジェクトへのアクセス（NodeEditor のターゲット選択用）
    int         GetObjectCount() const;
    std::string GetObjectLabel(int index) const;    // 表示用 "0: block"
    Obj3d*      GetObject3d(int index);             // 表示オブジェクトを取得（シェーダー適用用）
    // ノード駆動でオブジェクトを動かす（Undo履歴には積まない）
    void        SetObjectPosY(int index, float y);  // Y座標を反映
    void        SetObjectRotY(int index, float r);  // Y回転を反映（ラジアン）
    void        SetObjectScale(int index, float s); // 均一スケールを反映
    void        SetObjectShaderParam(int index, float v); // シェーダーパラメータ(0〜1)を反映

private:

    // カメラの視線の先の配置位置を計算する（SpawnObject 用）
    Vector3 CalcSpawnPoint() const;
    int spawnCounter_ = 0; // 連続配置時に位置をずらすカウンタ

    // levelData_.objects の内容から表示用 object3ds_ を作り直す（Undo/Redo用）
    void RebuildObjects();

    // Undo/Redo 履歴（オブジェクト配置のみのスナップショット。
    //   レールまで戻すと RailEditor の選択・履歴と衝突するため objects に限定）
    std::vector<std::vector<LevelObjectData>> undoStack_;
    std::vector<std::vector<LevelObjectData>> redoStack_;

    // --- マップファイル選択 / 保存状態 ---
    std::vector<std::string> mapList_;      // resources/map/ 内の *.json
    int selectedMapIndex_ = -1;             // 一覧で選択中のマップ
    std::string currentMapFile_ = "resources/map/map01.json"; // 今開いているファイル
    bool  dirty_ = false;                   // 未保存の変更があるか
    int   lastBlockVersion_ = 0;            // ブロック編集の監視用（変化→dirty_）
    bool  autoSave_ = false;                // 自動保存 ON/OFF
    float autoSaveTimer_ = 0.0f;            // 自動保存までのフレームカウンタ

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

    LevelData levelData_; // 現在のマップデータ（レールの実データもここに置く）
    int mapLoadVersion_ = 0; // マップ読込のたびに増やす（シーンが敵を読み直す合図）
    std::vector<std::unique_ptr<Obj3d>> object3ds_; // 配置された3Dオブジェクト
    int selectedObjectIndex_ = -1; // 選択中のオブジェクト番号

    std::string saveFileName_ = "map01.json"; // ファイル名
    bool snapToGrid_ = true;

    // レール編集の専用クラス（levelData_ を参照して編集する）
    std::unique_ptr<RailEditor> railEditor_ = nullptr;

};
