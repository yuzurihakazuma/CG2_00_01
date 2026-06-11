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

    // Blenderインポータ等の外部から変換済みデータを受け取って反映する
    //   additive: true=既存マップに追記 / false=置き換え
    void ApplyImportedData(const LevelData& data, bool additive);

    // resources/ を走査してモデルアセット一覧を更新する
    void ScanAssets();

    // --- レール編集データの公開（ゲーム側が同じレールを参照するため）---
    int GetRailVersion() const { return railVersion_; }
    const std::vector<std::vector<Vector3>>& GetRailLines() const { return levelData_.railLines; }
    const std::vector<int>& GetRailTypes() const { return levelData_.railTypes; }
    // 各レールの動き（x,y,z=振幅 / w=周期）。railLines と同じ並び
    const std::vector<Vector4>& GetRailMotions() const { return levelData_.railMotions; }

    // --- マウス編集サポート（EditorManager が Game View 上で使う）---
    int  GetCurrentRailIndex() const { return currentEditRailIndex_; }
    int  GetCurrentRailNodeCount() const;                 // 現在編集中レールのノード数
    bool GetRailNodePos(int idx, Vector3& out) const;     // ノード座標を取得
    void SetRailNodePos(int idx, const Vector3& p);       // ノードを移動（世代更新）
    void AppendRailNodeAt(const Vector3& p);              // 末尾にノード追加（世代更新・選択）
    Vector3 ComputePlacement(const Vector3& raw) const;  // 直角ロック＋グリッド＋ノード吸着を適用した着地点（プレビュー用）
    int  GetSelectedRailNode() const { return selectedRailNode_; }
    void SetSelectedRailNode(int idx) { selectedRailNode_ = idx; }
    bool  IsRailDrawMode() const { return railDrawMode_; } // 地面クリックで追加するモードか
    float GetRailDrawHeight() const { return railDrawHeight_; } // 地面クリック時のY高さ
    bool  IsRailSnap() const { return railSnap_; }            // グリッド吸着ON/OFF
    float GetRailGridSize() const { return railGridSize_; }   // グリッド間隔(m)
    // 前のノードから相対(dx,dy,dz)に新ノードを追加（方向ボタン用・スナップ適用）
    void AppendRailNodeRelative(float dx, float dy, float dz);

    // ノードの挿入・削除（マウス編集）
    void InsertRailNode(int afterIndex, const Vector3& p); // afterIndex の直後に挿入
    void DeleteRailNode(int idx);                          // 指定ノードを削除

    // 既存ノードへのスナップ
    bool    IsNodeSnap() const { return railNodeSnap_; }
    Vector3 ApplyNodeSnap(const Vector3& p) const;         // 他レールの近い端点へ吸着（無ければそのまま）

    // フリーハンド（ドラッグで一筆書き）
    bool IsFreehand() const { return railFreehand_; }

    // Undo / Redo
    void Undo();
    void Redo();

    // ============================================================
    // 複数選択（路線まるごと移動・矩形選択でまとめて動かす）
    //   ・線をクリック → SelectWholeRail で路線の全ノードを選択
    //   ・矩形ドラッグ → AddToSelection で囲んだノードを選択（レール跨ぎ可）
    //   ・ギズモは GetSelectionCenter をピボットに TranslateSelection で平行移動
    // ============================================================
    struct NodeRef { int rail; int node; };
    const std::vector<NodeRef>& GetMultiSelection() const { return multiSelection_; }
    void ClearMultiSelection(){ multiSelection_.clear(); }
    void AddToSelection(int rail, int node);
    void SelectSingleNode(int rail, int node);     // 1ノード選択（編集対象の路線も切替）
    void SelectWholeRail(int railIdx);             // 路線の全ノードを選択（まとめて移動用）
    Vector3 GetSelectionCenter() const;            // 選択ノード群の中心（ギズモのピボット）
    void TranslateSelection(const Vector3& delta); // 選択ノード群を平行移動（世代更新）
    void SetCurrentRail(int idx);                  // 編集対象の路線を切り替え

    // 全レール横断アクセス（Game View のピッキング・表示用）
    int  GetRailCount() const { return ( int ) levelData_.railLines.size(); }
    int  GetNodeCountOf(int rail) const;
    bool GetNodePosOf(int rail, int node, Vector3& out) const;

private:
    // 複数選択中のノード（rail番号＋ノード番号の組）
    std::vector<NodeRef> multiSelection_;

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

    // 既存ノードへのスナップ・フリーハンド
    bool  railNodeSnap_       = true; // 他レールの端点へ吸着
    float railNodeSnapRadius_ = 0.7f; // 吸着半径(m)
    bool  railFreehand_       = false; // ドラッグで一筆書き

    // 値をグリッドに丸める（railSnap_ がOFFならそのまま）
    float SnapValue(float v) const;

    // --- Undo/Redo（操作が落ち着いたら自動でチェックポイントを作る方式）---
    struct RailSnapshot {
        std::vector<std::vector<Vector3>> lines;
        std::vector<int> types;
    };
    std::vector<RailSnapshot> undoStack_;
    std::vector<RailSnapshot> redoStack_;
    RailSnapshot committed_;        // 直近の安定状態（チェックポイント）
    bool committedInit_ = false;
    void CommitIfStable();          // マウス非操作時に変化を検知して履歴へ積む
    void RestoreSnapshot(const RailSnapshot& s); // 状態を復元

    // レール表示用のオブジェクトを一括で作り直す関数
    void RebuildRailPoints();

};