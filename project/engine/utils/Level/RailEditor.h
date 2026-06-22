#pragma once

#include "engine/utils/Level/LevelData.h"
#include <vector>

// =====================================================================
//  RailEditor：レール（Yoshi風コース）編集の専用クラス。
//   ・以前は LevelEditor が抱えていたレール編集UI・ロジックをここへ分離。
//   ・レールの実データ（railLines / railTypes / railMotions）は LevelData に
//     置いたまま、ポインタ参照で編集する（マップ保存/読込は LevelEditor 側で一本化）。
//   ・Game View 上のピッキングは EditorManager が GetRailEditor() 経由で呼ぶ。
// =====================================================================
class RailEditor{
public:
    // rail/node 番号の組（複数選択・矩形選択で使う）
    struct NodeRef{ int rail; int node; };

    // 編集対象の LevelData を受け取る（所有しない）
    explicit RailEditor(LevelData* data) : data_(data){}

    // マップ読込/差し替え時に呼ぶ：選択や履歴をリセットし、空なら1本用意する
    void OnMapChanged();

    // レールエディタ用のウィンドウ（管理/作成タブ）を描画する
    void DrawWindow();

    // --- ゲーム側へ公開（EditorManager 経由で GamePlayScene が参照）---
    int GetVersion() const{ return railVersion_; }
    const std::vector<std::vector<Vector3>>& GetRailLines() const{ return data_->railLines; }
    const std::vector<int>& GetRailTypes() const{ return data_->railTypes; }
    const std::vector<Vector4>& GetRailMotions() const{ return data_->railMotions; }
    const std::vector<bool>& GetRailHasGround() const{ return data_->railHasGround; }

    // --- マウス編集サポート（EditorManager が Game View 上で使う）---
    int  GetCurrentRailIndex() const{ return currentEditRailIndex_; }
    int  GetCurrentRailNodeCount() const;
    bool GetRailNodePos(int idx, Vector3& out) const;
    void SetRailNodePos(int idx, const Vector3& p);
    void AppendRailNodeAt(const Vector3& p);
    Vector3 ComputePlacement(const Vector3& raw) const;
    int  GetSelectedRailNode() const{ return selectedRailNode_; }
    void SetSelectedRailNode(int idx){ selectedRailNode_ = idx; }
    bool  IsRailDrawMode() const{ return railDrawMode_; }
    float GetRailDrawHeight() const{ return railDrawHeight_; }
    bool  IsRailSnap() const{ return railSnap_; }
    float GetRailGridSize() const{ return railGridSize_; }

    // ノードの挿入・削除（マウス編集）
    void InsertRailNode(int afterIndex, const Vector3& p);
    void DeleteRailNode(int idx);

    // 既存ノードへのスナップ
    bool    IsNodeSnap() const{ return railNodeSnap_; }
    Vector3 ApplyNodeSnap(const Vector3& p) const;

    // フリーハンド（ドラッグで一筆書き）
    bool IsFreehand() const{ return railFreehand_; }

    // Undo / Redo
    void Undo();
    void Redo();
    // 編集開始時（このマップを開いた直後）の状態へ一発で戻す（この操作自体も元に戻せる）
    void ResetToInitial();
    // 操作履歴・初期復元の可否（UIのボタン活性／グレーアウト用）
    int  GetUndoCount() const{ return ( int ) undoStack_.size(); }
    int  GetRedoCount() const{ return ( int ) redoStack_.size(); }
    bool CanResetToInitial() const;

    // 複数選択（路線まるごと移動・矩形選択でまとめて動かす）
    const std::vector<NodeRef>& GetMultiSelection() const{ return multiSelection_; }
    void ClearMultiSelection(){ multiSelection_.clear(); }
    void AddToSelection(int rail, int node);
    void SelectSingleNode(int rail, int node);
    void SelectWholeRail(int railIdx);
    Vector3 GetSelectionCenter() const;
    void TranslateSelection(const Vector3& delta);
    void SetCurrentRail(int idx);

    // 全レール横断アクセス（Game View のピッキング・表示用）
    int  GetRailCount() const{ return ( int ) data_->railLines.size(); }
    int  GetNodeCountOf(int rail) const;
    bool GetNodePosOf(int rail, int node, Vector3& out) const;
    int  GetRailDisplayType(int rail) const;

    // シェイプのスタンプ配置（生成→マウスに追従→クリックで設置）
    bool HasPendingStamp() const{ return !pendingStamp_.empty(); }
    const std::vector<Vector3>& GetPendingStamp() const{ return pendingStamp_; }
    void PlaceStamp(const Vector3& at);
    void CancelStamp(){ pendingStamp_.clear(); }

    // キーボード操作（EditorManager から呼ばれる）
    void DeleteSelectedNodes();
    void DuplicateRail(int railIdx);

    // 近いレール同士を連結する：各レールの端点が他レール「本体（途中含む）」の
    // すぐ近くにあれば、その最近点へ端点を寄せて相手にも共有ノードを挿入する。
    // 端点溶接（端点同士）では届かない「線の途中での合流」も繋げられる。
    void ConnectNearbyLines();

private:
    // 編集対象（所有しない）。アドレスは LevelEditor の levelData_ メンバで安定。
    LevelData* data_ = nullptr;

    // 複数選択中のノード（rail番号＋ノード番号の組）
    std::vector<NodeRef> multiSelection_;

    // 配置待ちのシェイプ（原点基準の相対座標。空なら配置待ちなし）
    std::vector<Vector3> pendingStamp_;

    // レール編集の世代番号（編集のたびに増やし、ゲーム側が変化を検知する）
    int railVersion_ = 0;

    int selectedRailNode_ = -1;
    int currentEditRailIndex_ = 0; // 現在編集しているレールの番号

    // マウス編集モード
    bool  railDrawMode_   = false; // true=地面クリックで末尾ノード追加
    float railDrawHeight_ = 1.5f;  // 地面クリック時に置くY高さ(m)

    // グリッド・直角設定（マウスもボタンも共通で使う）
    bool  railSnap_     = true;    // ノードをグリッドに吸着
    float railGridSize_ = 1.0f;    // グリッド間隔(m)
    bool  railAxisLock_ = false;   // 直角モード：新ノードを前ノードから X or Z 軸のみに固定

    // 既存ノードへのスナップ・フリーハンド
    bool  railNodeSnap_       = true; // 他レールの端点へ吸着
    float railNodeSnapRadius_ = 0.7f; // 吸着半径(m)
    bool  railFreehand_       = false; // ドラッグで一筆書き

    // 値をグリッドに丸める（railSnap_ がOFFならそのまま）
    float SnapValue(float v) const;

    // 前のノードから相対(dx,dy,dz)に新ノードを追加（方向ボタン用・スナップ適用）
    void AppendRailNodeRelative(float dx, float dy, float dz);

    // --- Undo/Redo（操作が落ち着いたら自動でチェックポイントを作る方式）---
    struct RailSnapshot{
        std::vector<std::vector<Vector3>> lines;
        std::vector<int>     types;
        std::vector<Vector4> motions; // 動くレール設定も履歴に含める
    };
    std::vector<RailSnapshot> undoStack_;
    std::vector<RailSnapshot> redoStack_;
    RailSnapshot committed_;        // 直近の安定状態（チェックポイント）
    bool committedInit_ = false;

    // 編集開始時（マップ読込直後）の状態。「最初期に戻す」で復元する。
    std::vector<std::vector<Vector3>> initialLines_;
    std::vector<int>     initialTypes_;
    std::vector<Vector4> initialMotions_;
    bool hasInitial_ = false;
    void CommitIfStable();          // マウス非操作時に変化を検知して履歴へ積む
    void RestoreSnapshot(const RailSnapshot& s); // 状態を復元

    // 編集の世代番号だけを進めて変化を通知する（描画はゲーム側）
    void RebuildRailPoints();
};
