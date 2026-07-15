#pragma once

#include "engine/utils/Level/LevelData.h"
#include <vector>
#include <cstdint>

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

    // カメラ演出専用のウィンドウ（ゾーンの追加・編集・プレビュー）を描画する
    void DrawCameraWindow();

    // 「この画角をプレビュー」の要求をシーンが受け取る（BlenderImporter と同じパターン）。
    //   要求があれば true を返し、メインカメラに設定すべき位置・回転を出す（1回で消費）。
    bool ConsumeCameraPreviewRequest(Vector3& outPos, Vector3& outRot);

    // --- ゲーム側へ公開（EditorManager 経由で GamePlayScene が参照）---
    int GetVersion() const{ return railVersion_; }
    const std::vector<std::vector<Vector3>>& GetRailLines() const{ return data_->railLines; }
    const std::vector<int>& GetRailTypes() const{ return data_->railTypes; }
    const std::vector<Vector4>& GetRailMotions() const{ return data_->railMotions; }
    const std::vector<int>& GetRailGroundTypes() const{ return data_->railGroundTypes; }
    const std::vector<std::vector<int>>& GetRailNodeHoles() const{ return data_->railNodeHoles; }
    const std::vector<int>& GetRailVisible() const{ return data_->railVisible; }
    const std::vector<int>& GetRailLineModes() const{ return data_->railLineModes; }
    const std::vector<int>& GetRailRoadModes() const{ return data_->railRoadModes; }
    bool IsRailVisible(int rail) const;
    const std::vector<int>&   GetRailMotionTypes() const{ return data_->railMotionTypes; }
    const std::vector<float>& GetRailMotionPhases() const{ return data_->railMotionPhases; }
    const std::vector<int>&   GetRailOneWay() const{ return data_->railOneWay; }
    const std::vector<float>& GetRailSpeedMuls() const{ return data_->railSpeedMuls; }
    int GetStartRail() const{ return data_->startRailIndex; }
    int GetStartNode() const{ return data_->startNodeIndex; }
    int GetGoalRail() const{ return data_->goalRailIndex; }
    int GetGoalNode() const{ return data_->goalNodeIndex; }
    const std::vector<LevelCameraZone>& GetCameraZones() const{ return data_->cameraZones; }

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

    // 選択中レールの端を延長する形でノードを1個追加（front=true で先頭側）。
    //   引き終わったレールに後からノードを足すためのボタン用。穴配列も同期する。
    void ExtendRailNode(bool front);

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
    // 現在の複数選択ノードの「穴」フラグをまとめて設定する（マウス箱選択→穴指定）
    void SetSelectionHole(bool hole);
    // 選択ノードに穴指定が1つでもあるか（ボタン表示の切替用）
    bool SelectionHasHole() const;
    Vector3 GetSelectionCenter() const;
    void TranslateSelection(const Vector3& delta);
    void SetCurrentRail(int idx);

    // 全レール横断アクセス（Game View のピッキング・表示用）
    int  GetRailCount() const{ return ( int ) data_->railLines.size(); }
    int  GetNodeCountOf(int rail) const;
    bool GetNodePosOf(int rail, int node, Vector3& out) const;
    int  GetRailDisplayType(int rail) const;
    // 指定ノードが「穴」指定か（エディタの線・ノードを赤く表示するため）
    bool IsNodeHole(int rail, int node) const;

    // --- 通行判定（プレイヤーがスタートのレールから辿り着けるか）---
    //   Game View の線色（紫=到達不能）とパネルの「×通れない」表示で使う。
    //   溶接/合流/乗り換えに加え、Gap レール端からのジャンプ（一方通行）も接続として辿る。
    bool IsRailReachable(int rail) const;

    // 端が他レールへ接続しているか（端から1.2m以内に相手の本体がある＝ゲームで合流できる）。
    //   ループ（先頭と末尾がくっついたレール）は端が無い扱いで true を返す。
    bool IsRailEndConnected(int rail, bool front) const;

    // レール末端からのジャンプ弾道を予測し、着地できるレール番号を返す（-1=届かない）。
    //   outArc に軌跡が入るので Game View で弧を描ける。useFlutter=ふんばり滞空込み。
    //   物理定数は Player と同じ値を使う（実装内のコメント参照）。
    int PredictJumpLanding(int rail, bool front, bool useFlutter,
                           std::vector<Vector3>* outArc = nullptr) const;

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

    // ============================================================
    // 自動スナップ接続（仕様書_自動レール接続 §1）
    //   端点ドラッグ中に他レールの端点/本体を探し、マウスアップで自動接続する。
    //   検出（Find〜）は変更なしの const、確定（Connect/Weld〜）は既存処理と同じ編集。
    // ============================================================
    struct SnapTarget {
        bool    valid = false;
        bool    isEndpoint = false; // true=端点同士（溶接） / false=本体の途中（T字連結）
        int     rail = -1;          // 接続先レール
        int     node = -1;          // isEndpoint 時の相手ノード（0 or 末尾）
        int     seg  = -1;          // 本体連結時の相手セグメント番号
        Vector3 pos {};             // スナップ後の端点位置
    };
    // rail の端点(front/back)から snapDistance 以内の接続候補を探す（端点同士を優先）
    SnapTarget FindSnapTarget(int rail, bool front, float radius) const;
    // 候補へ実際に接続する（isEndpoint→溶接 / それ以外→ConnectNearbyLines と同じ途中連結）
    void ConnectToTarget(int rail, bool front, const SnapTarget& target);

    // 自動スナップの設定（Global 設定。EditorManager のドラッグ処理と RoadMesh が参照）
    bool  IsAutoSnap() const{ return railAutoSnap_; }
    float GetSnapDistance() const{ return railSnapDistance_; }
    int   GetJointVisible() const{ return railJointVisible_; } // 0=エディタのみ/1=常に/2=非表示

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
    int selectedCamZone_ = -1;     // カメラ演出：一覧で選択中のゾーン番号

    // カメラプレビュー要求（DrawCameraWindow で積み、シーンが Consume で受け取る）
    bool    camPreviewPending_ = false;
    bool    camPreviewLive_ = false; // ON の間、毎フレーム要求を出す＝スライダー操作が即カメラに反映
    Vector3 camPreviewPos_ { 0.0f, 0.0f, 0.0f };
    Vector3 camPreviewRot_ { 0.0f, 0.0f, 0.0f };

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

    // 自動スナップ接続（§5。プラレール風：近づけるだけでカチッと繋がる）
    bool  railAutoSnap_      = true;  // 端点ドラッグで自動接続する
    float railSnapDistance_  = 1.2f;  // 検出半径(m)。ランタイムの合流距離と同じ既定
    int   railJointVisible_  = 1;     // ジョイント表示：0=エディタのみ / 1=常に / 2=非表示

    // 接続一覧（接続タブ）で選択中の行（-1=なし）
    int   selectedConnection_ = -1;

    // 値をグリッドに丸める（railSnap_ がOFFならそのまま）
    float SnapValue(float v) const;

    // レール a,b が接続しているか（端から合流1.2m / 別タイプ交差0.9m。溶接0.7mは1.2mに含まれる）
    bool AreRailsLinked(int a, int b) const;

    // 通行可否のキャッシュ。railVersion_ が変わった時だけ BFS で作り直す。
    //   const な表示系メソッドから触るため mutable（表示用キャッシュの常套手段）。
    mutable int reachCacheVersion_ = -1;
    mutable std::vector<uint8_t> reachable_;
    void EnsureReachableCache() const;

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
