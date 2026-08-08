#pragma once

#include "engine/utils/Level/LevelData.h"
#include <vector>
#include <cstdint>

class SplineRail; // ブロックのノード錨の更新に使う（実体は engine/rail/SplineRail.h）

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
    // レール編集の毎フレーム処理（配列同期・Undo確定・Ctrl+Z/Y）。ウィンドウ非表示でも毎フレーム呼ぶ
    void TickEditing();
    void DrawWindow();

    // カメラ演出専用のウィンドウ（ゾーンの追加・編集・プレビュー）を描画する
    void DrawCameraWindow();

    // 配置エディタ（コイン・ブロック専用ウィンドウ）。置く系の作業を集約
    void DrawItemWindow();

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
    const std::vector<int>& GetRailEndPlazas() const{ return data_->railEndPlazas; } // 端の丸広場
    const std::vector<int>& GetRailGuideRails() const{ return data_->railGuideRails; } // ガイド追従の対象
    bool IsRailVisible(int rail) const;
    const std::vector<int>&   GetRailMotionTypes() const{ return data_->railMotionTypes; }
    const std::vector<float>& GetRailMotionPhases() const{ return data_->railMotionPhases; }
    const std::vector<int>&   GetRailMotionTriggers() const{ return data_->railMotionTriggers; } // 0=最初から/1=乗ったら
    const std::vector<int>&   GetRailAppearTriggers() const{ return data_->railAppearTriggers; } // -1=通常/N=レールNに乗ると出現
    const std::vector<float>& GetRailGuideStarts() const{ return data_->railGuideStarts; } // ガイド区間の開始(m)
    const std::vector<float>& GetRailGuideEnds() const{ return data_->railGuideEnds; }     // ガイド区間の終了(m)。-1=終点
    const std::vector<int>&   GetRailGuideModes() const{ return data_->railGuideModes; }   // 0=一周/1=往復
    const std::vector<int>&   GetRailGuideAligns() const{ return data_->railGuideAligns; } // 0=向き固定/1=列車式回転
    const std::vector<float>& GetRailGuideDwells() const{ return data_->railGuideDwells; } // 停車時間(秒)
    const std::vector<int>&   GetRailOneWay() const{ return data_->railOneWay; }
    const std::vector<float>& GetRailSpeedMuls() const{ return data_->railSpeedMuls; }
    int GetStartRail() const{ return data_->startRailIndex; }
    int GetStartNode() const{ return data_->startNodeIndex; }
    int GetGoalRail() const{ return data_->goalRailIndex; }
    int GetGoalNode() const{ return data_->goalNodeIndex; }
    const std::vector<LevelCameraZone>& GetCameraZones() const{ return data_->cameraZones; }
    const std::vector<CoinData>& GetCoins() const{ return data_->coins; } // 収集物（コイン）の配置

    // --- ブロック（乗れる/ぶつかる1m角。マリオメーカー風のGame Viewペイント配置）---
    const std::vector<BlockData>& GetBlocks() const{ return data_->blocks; }
    int  GetBlockVersion() const{ return blockVersion_; }   // ブロック編集の世代番号（ゲーム側が再生成を検知）
    bool IsBlockPaintMode() const{ return blockPaintMode_; } // Game View クリックで配置/削除するモード
    bool IsBlockEraseMode() const{ return blockPaintErase_; } // true=消しゴム（クリックで必ず消す）
    int  GetBlockPaintType() const{ return blockPaintType_; } // 置くブロックの種類（BlockData::type）
    int  GetBlockPaintShape() const{ return blockPaintShape_; } // 塗り方（0=1個/1=柱/2=階段）
    int  FindBlock(int rail, float dist, int level, float side) const; // セル一致するブロック番号（-1=なし）
    void AddBlock(int rail, float dist, int level, float side, int type = 0); // セルが空なら追加
    bool RemoveBlock(int rail, float dist, int level, float side);     // セル一致を削除（true=消した）
    // コインを1枚追加（ゲームビューの右クリック配置用。呼び出し側で CoinSystem::Sync を行うこと）
    void AddCoinAt(int rail, float dist){
        if ( !data_ || rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
        CoinData coin; coin.rail = rail; coin.dist = dist; coin.height = 1.0f;
        data_->coins.push_back(coin);
    }
    // コインを1枚削除（ゲームビューの右クリックメニュー用）。index は GetCoins() の番号
    void RemoveCoinAt(int index){
        if ( !data_ || index < 0 || index >= ( int ) data_->coins.size() ) return;
        data_->coins.erase(data_->coins.begin() + index);
    }
    // --- 任意レールのノード直接操作（ゲームビューのガイドハンドル用。版を進めてライブ同期）---
    void SetNodePosOf(int rail, int idx, const Vector3& p){
        if ( !data_ || rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
        auto& line = data_->railLines[rail];
        if ( idx < 0 || idx >= ( int ) line.size() ) return;
        line[idx] = p;
        ++railVersion_;
    }
    void InsertNodeOf(int rail, int idx, const Vector3& p){ // idx の位置に挿入（既存 idx は後ろへ）
        if ( !data_ || rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
        auto& line = data_->railLines[rail];
        if ( idx < 1 || idx > ( int ) line.size() - 1 ) return; // 端の外には挿入しない
        line.insert(line.begin() + idx, p);
        if ( rail < ( int ) data_->railNodeHoles.size()
            && idx <= ( int ) data_->railNodeHoles[rail].size() ) {
            data_->railNodeHoles[rail].insert(data_->railNodeHoles[rail].begin() + idx, 0);
        }
        ++railVersion_;
    }
    void DeleteNodeOf(int rail, int idx){ // 最低2点は残す
        if ( !data_ || rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
        auto& line = data_->railLines[rail];
        if ( ( int ) line.size() <= 2 || idx < 0 || idx >= ( int ) line.size() ) return;
        line.erase(line.begin() + idx);
        if ( rail < ( int ) data_->railNodeHoles.size()
            && idx < ( int ) data_->railNodeHoles[rail].size() ) {
            data_->railNodeHoles[rail].erase(data_->railNodeHoles[rail].begin() + idx);
        }
        ++railVersion_;
    }

    // コインを1枚更新（ゲームビューのドラッグ移動用）。呼び出し側で CoinSystem::Sync を行うこと
    void SetCoinAt(int index, int rail, float dist, float height){
        if ( !data_ || index < 0 || index >= ( int ) data_->coins.size() ) return;
        if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
        data_->coins[index].rail   = rail;
        data_->coins[index].dist   = dist;
        data_->coins[index].height = height;
    }
    // ブロックの「ノード錨」を維持する（毎フレーム呼んでよい）。
    //   未計算のブロックには錨を記録し、レール編集で曲線長が変わったブロックは
    //   錨ノードからの相対距離を保つよう dist を引き直す＝置いた場所から滑らない
    void UpdateBlockAnchors(const std::vector<SplineRail>& splineRails);

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

    // ============================================================
    // レール同士の接続情報（GameViewの補足コメント・接続情報パネル用）。
    //   どのレールと「溶接/T字/交差」していて、その座標はどこかが分かる。
    //   railVersion_ でキャッシュするので毎フレーム呼んでも軽い。
    // ============================================================
    struct RailConnectionInfo {
        int     type = 0;      // 0=溶接(端点同士) / 1=T字(端点→相手の途中) / 2=交差(本体×本体)
        int     otherRail = -1; // 相手レール番号
        Vector3 pos {};         // 接続/交差しているワールド座標
    };
    const std::vector<RailConnectionInfo>& GetRailConnections(int rail) const;

    // レール末端からのジャンプ弾道を予測し、着地できるレール番号を返す（-1=届かない）。
    //   outArc に軌跡が入るので Game View で弧を描ける。useFlutter=ふんばり滞空込み。
    //   物理定数は Player と同じ値を使う（実装内のコメント参照）。
    int PredictJumpLanding(int rail, bool front, bool useFlutter,
                           std::vector<Vector3>* outArc = nullptr) const;

    // シェイプのスタンプ配置（生成→マウスに追従→クリックで設置）
    bool HasPendingStamp() const{ return !pendingStamp_.empty() || !pendingMultiStamp_.empty(); }
    const std::vector<Vector3>& GetPendingStamp() const{ return pendingStamp_; }
    void PlaceStamp(const Vector3& at);
    void CancelStamp(){ pendingStamp_.clear(); pendingMultiStamp_.clear(); pendingMultiMeta_.clear(); }

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
    // FindSnapTarget の「真上から見た距離(XZ)」版。3D距離では拾えない
    // 「位置は合っているのに高さだけズレている」相手を見つける（高さ合わせスナップ用）。
    //   maxYDiff: 高さ差の上限。別の階のレールを誤って拾わないための保険
    SnapTarget FindSnapTargetXZ(int rail, bool front, float radiusXZ, float maxYDiff = 5.0f) const;
    // 候補へ実際に接続する（isEndpoint→溶接 / それ以外→ConnectNearbyLines と同じ途中連結）
    //   pos は相手側の点なので、高さ違いの候補でも端点のYが相手に一致する
    void ConnectToTarget(int rail, bool front, const SnapTarget& target);

    // 自動スナップの設定（Global 設定。EditorManager のドラッグ処理と RoadMesh が参照）
    bool  IsAutoSnap() const{ return railAutoSnap_; }
    float GetSnapDistance() const{ return railSnapDistance_; }
    bool  IsSnapMatchHeight() const{ return railSnapMatchHeight_; } // 高さ違いでもXZが近ければ合わせて接続
    bool  IsEndGuideVisible() const{ return railEndGuide_; }        // 選択レールの端点に足元ガイドを出す
    bool  IsReachLinesVisible() const{ return railReachLines_; }    // ジャンプ予測線を出す
    bool  IsMotionPreview() const{ return railMotionPreview_; }     // 動くレールをエディタ中も再生する
    void  SetMotionPreview(bool v){ railMotionPreview_ = v; }       // ゲームビュー右クリックメニューからの切替用
    bool  IsGuidePanelDragging() const{ return guidePanelDragging_; } // リフト経路エディタでドラッグ中か（同期間引き用）
    // 編集対象の固定：ONの間は Game View のクリックで編集対象レールが切り替わらない
    //   （リフト調整中に他のレールへ触れて、動きタブの内容が消えてしまう事故を防ぐ）
    bool  IsEditTargetLocked() const{ return lockEditTarget_; }
    int   GetJointVisible() const{ return railJointVisible_; } // 0=エディタのみ/1=常に/2=非表示

private:
    // 編集対象（所有しない）。アドレスは LevelEditor の levelData_ メンバで安定。
    LevelData* data_ = nullptr;

    // 複数選択中のノード（rail番号＋ノード番号の組）
    std::vector<NodeRef> multiSelection_;

    // 配置待ちのシェイプ（原点基準の相対座標。空なら配置待ちなし）
    std::vector<Vector3> pendingStamp_;
    // 複数レール一括テンプレート（登り足場など）。空でなければ PlaceStamp で全ポリラインを路線化する
    std::vector<std::vector<Vector3>> pendingMultiStamp_;
public:
    // 動くテンプレート用のメタ設定（ポリラインごとの 可視/道/波形/親子/位相。空なら既定値）
    struct StampMeta {
        int   visible    = 1;
        int   roadMode   = 0;   // 0=道あり / 1=なし
        int   lineMode   = 0;   // 0=スプライン / 1=直線
        int   motionType = 0;   // 3=ガイドレール追従
        int   guideRel   = -1;  // テンプレ内の相対番号（0=1本目がガイド。-1=なし）
        float phase      = 0.0f;
        float period     = 12.0f;
    };
    // 配置前ゴーストの全体表示用（EditorManager が参照。骨組みガイドは薄く描く）
    const std::vector<std::vector<Vector3>>& GetPendingMultiStamp() const{ return pendingMultiStamp_; }
    const std::vector<StampMeta>& GetPendingMultiMeta() const{ return pendingMultiMeta_; }

private:
    std::vector<StampMeta> pendingMultiMeta_;

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
    bool  railSnapMatchHeight_ = true; // 真上から見て近ければ高さを相手に合わせて接続（OFF=立体交差用）
    bool  railEndGuide_        = true; // 選択レールの端点から地面へ縦線＋Y値を表示（高さの目視用）
    bool  railMotionPreview_   = false; // 動くレール（親子付け含む）をエディタ中も再生して確認する
    bool  railReachLines_      = false; // ジャンプ予測の弾道線（Gapが多いと密になるので既定OFF。見たい時だけON）

    // 2D形状エディタ（ETOS風。作成タブ=選択路線 / 動きタブ=ガイドレール の点をつかんで編集する）
    bool lockEditTarget_ = false;     // 編集対象の固定（動き/作成タブのチェックボックスで切替）
    int  guidePanelDragNode_ = -1;    // ドラッグ中のノード番号（-1=なし）
    int  guidePanelDragMark_ = 0;     // ドラッグ中の区間マーカー（0=なし/1=始点◆/2=終点◆）
    int  guidePanelDragRail_ = -1;    // ドラッグ対象のレール（タブ切替等で別レールに化けた時の安全弁）
    bool guidePanelDragging_ = false; // パネル内ドラッグ中（ゲーム側が道の再生成を間引く合図）
    int  pathEditPlane_ = 0;          // 断面（0=自動 / 1=横見図X-Y / 2=横見図Z-Y / 3=上からX-Z）
    // 2Dエディタの表示・操作状態（ズーム/パン/選択/引っ張り挿入）
    float pathEditZoom_ = 1.0f;       // ホイールズーム（1=全体が収まる倍率）
    float pathEditPanX_ = 0.0f;       // 中ボタンドラッグのパン（キャンバスpx）
    float pathEditPanY_ = 0.0f;
    int   pathEditLastRail_ = -1;     // 表示中レール（変わったらズーム/パン/選択をリセット）
    int   pathEditSelNode_  = -1;     // パネル内で最後に触ったノード（数値入力欄の対象）
    bool  pathEditPullPending_ = false; // 線を押した直後（引っ張るかクリックかの判定待ち）
    // 表示範囲と断面のスナップショット（ドラッグ中は凍結して「ノードが飛んでいく」暴走を防ぐ）
    int   pathViewPlane_ = 1;
    float pathViewMinH_ = -10.0f, pathViewMaxH_ = 10.0f;
    float pathViewMinV_ = -10.0f, pathViewMaxV_ = 10.0f;
    int   pathEditPullSeg_ = 0;       // 押した線分番号
    float pathEditPullT_   = 0.0f;    // 線分上の位置(0〜1)
    float pathEditPullX_ = 0.0f, pathEditPullY_ = 0.0f; // 押した位置（判定用キャンバスpx）
    // 汎用の2D形状エディタ。motionOverlay=true でガイド追従用の区間◆と動きの青丸も描く
    void DrawRailPathEditor(int railIdx, bool motionOverlay);
    int   railJointVisible_  = 1;     // ジョイント表示：0=エディタのみ / 1=常に / 2=非表示

    // 接続一覧（接続タブ）で選択中の行（-1=なし）
    int   selectedConnection_ = -1;

    // ブロック編集（配置物タブ／Game Viewペイント）
    int  blockVersion_ = 0;       // 編集のたびに増やし、ゲーム側が作り直しを検知する
    bool blockPaintMode_ = false; // Game View クリックで配置/削除するモード
    bool blockPaintErase_ = false; // true=消しゴムモード（クリックで必ず消す）
    int  blockPaintType_ = 0;      // 置くブロックの種類（BlockData::type）
    int  blockPaintShape_ = 0;     // 塗り方（0=1個ずつ/1=柱：下まで縦に埋める/2=階段：ドラッグで1段ずつ）

    // 値をグリッドに丸める（railSnap_ がOFFならそのまま）
    float SnapValue(float v) const;

    // 「レールと同数であるべき全設定配列」をレール数に揃える（既定値で埋める）。
    // 配列ズレによる書き込み事故の一元対策。DrawWindow 冒頭で毎フレーム呼ぶ
    void SyncRailArraySizes();
    // レールを1本追加し、全設定配列を既定値で揃える。追加したレール番号を返す
    int AppendRail(std::vector<Vector3> line, const std::string& group = "");
    // レール1本と全設定配列の同じ番号を削除する（ガイド参照の付け替えも行う）
    void EraseRail(int idx);

public:
    // リスト上でホバー中のレール（Game View で黄色ハイライトするため。-1=なし）
    int GetHoveredListRail() const{ return hoveredListRail_; }
    // 接続一覧のホバー：相手側レールと接続点（両方光らせて場所を示す）
    int  GetHoveredListRailB() const{ return hoveredListRailB_; }
    bool GetHoveredConnPos(Vector3& out) const{ out = hoveredConnPos_; return hoveredConnValid_; }

    // 到達チェックの経路（スタート→ゴールのワールドポリライン。空=未チェック/到達不可）
    const std::vector<Vector3>& GetRoutePoints() const{ return routePoints_; }
    // 各点への区間がジャンプ弧かどうか（表示色分け用。routePoints_ と同じ長さ）
    const std::vector<char>& GetRouteJumpFlags() const{ return routeJumpFlags_; }
    // ゴースト走行の現在位置（走行中のみ true）
    bool GetGhostPos(Vector3& out) const;
private:
    int hoveredListRail_ = -1;
    int hoveredListRailB_ = -1;
    Vector3 hoveredConnPos_ {};
    bool hoveredConnValid_ = false;

    // --- スタート→ゴール到達チェック＆ゴースト走行 ---
    void BuildRouteCheck();               // BFSで経路を求め routePoints_ を作る
    std::vector<Vector3> routePoints_;    // 経路のワールドポリライン
    std::vector<char>    routeJumpFlags_; // 各点への区間がジャンプ弧か（1=ジャンプ）
    std::vector<float>   routeCum_;       // 経路の累積距離（ゴースト位置計算用）
    bool  routeAutoRecheck_ = true;       // 編集のたびに自動で再チェックする
    int   lastRouteVersion_ = -1;         // 自動再チェック用（railVersion_ の控え）
    bool  routeChecked_   = false;
    bool  routeReachable_ = false;
    int   routeViaCount_  = 0;
    bool  ghostRun_   = false;            // ゴースト走行中か
    float ghostDist_  = 0.0f;             // 経路上の進行距離(m)
    float ghostSpeed_ = 5.0f;             // ゴーストの速度(m/s)。プレイヤーの通常速度と同じ既定値

    // レール a,b が接続しているか（端から合流1.2m / 別タイプ交差0.9m。溶接0.7mは1.2mに含まれる）
    bool AreRailsLinked(int a, int b) const;

    // 通行可否のキャッシュ。railVersion_ が変わった時だけ BFS で作り直す。
    //   const な表示系メソッドから触るため mutable（表示用キャッシュの常套手段）。
    mutable int reachCacheVersion_ = -1;
    mutable std::vector<uint8_t> reachable_;
    void EnsureReachableCache() const;

    // 接続情報のキャッシュ（railVersion_ が変わった時だけ全ペア走査で作り直す）
    mutable int connCacheVersion_ = -1;
    mutable std::vector<std::vector<RailConnectionInfo>> connCache_;
    void EnsureConnectionCache() const;

    // 前のノードから相対(dx,dy,dz)に新ノードを追加（方向ボタン用・スナップ適用）
    void AppendRailNodeRelative(float dx, float dy, float dz);

    // --- Undo/Redo（操作が落ち着いたら自動でチェックポイントを作る方式）---
    struct RailSnapshot{
        std::vector<std::vector<Vector3>> lines;
        std::vector<int>     types;
        std::vector<Vector4> motions; // 動くレール設定も履歴に含める
        std::vector<CoinData> coins;  // コイン配置も履歴に含める（レール削除のUndoでズレないように）
        std::vector<BlockData> blocks; // ブロック配置も履歴に含める（ペイントのやり直しができる）
    };
    std::vector<RailSnapshot> undoStack_;
    std::vector<RailSnapshot> redoStack_;
    RailSnapshot committed_;        // 直近の安定状態（チェックポイント）
    bool committedInit_ = false;

    // 編集開始時（マップ読込直後）の状態。「最初期に戻す」で復元する。
    std::vector<std::vector<Vector3>> initialLines_;
    std::vector<int>     initialTypes_;
    std::vector<Vector4> initialMotions_;
    std::vector<CoinData> initialCoins_;
    std::vector<BlockData> initialBlocks_;
    bool hasInitial_ = false;
    void CommitIfStable();          // マウス非操作時に変化を検知して履歴へ積む
    void RestoreSnapshot(const RailSnapshot& s); // 状態を復元

    // 編集の世代番号だけを進めて変化を通知する（描画はゲーム側）
    void RebuildRailPoints();
};
