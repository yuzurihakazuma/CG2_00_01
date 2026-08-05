#pragma once
#include <memory>
#include <vector>
#include "engine/math/struct.h" // Vector3
#include "engine/utils/Level/LevelData.h" // LevelEnemyData
#include "engine/utils/Level/RailEditor.h" // SnapTarget（自動スナップ接続 §5）

class LevelEditor;
class BlenderImporter;
class Camera;
class DebugCamera;
class GPUParticleEditor;
class GPUParticleEmitter;
class SkinnedObj3d;
class Obj3d;
class PerformanceMonitor;
class FileEditor;
class NodeEditor;


enum class EngineMode{
    Edit, // 時間が止まっていてコースを作れる状態
    Play  // 時間が動き、ゲームとして遊べる状態
};

class EditorManager{
public:

    // シングルトンインスタンスの取得
    static EditorManager* GetInstance();


    // ImGuiのフレーム開始（Game::Update の先頭で呼ぶ）
    void Begin();

    void Initialize();
    void Finalize();
    void Draw();

    // エディタUIの更新・描画
    void Update();

    // ImGuiの描画コマンドを発行（Game::Draw の末尾で呼ぶ）
    void End();

	// カメラをSceneManagerから受け取るためのセッター
    void SetCamera(const Camera* camera);


    // パフォーマンス計測値をSceneManagerから受け取るためのセッター
    void SetCpuTimes(float updateMs, float drawMs){
        cpuUpdateTimeMs_ = updateMs;
        cpuDrawTimeMs_ = drawMs;
    }

	// Game View に表示するテクスチャのSRVインデックスをSceneManagerから受け取るためのセッター
    void SetGameViewSrvIndex(uint32_t srvIndex) { gameViewSrvIndex_ = srvIndex; }

    void SetParticleEmitter(GPUParticleEmitter* emitter);

    // レベルエディタへのアクセス（FileEditor / NodeEditor がオブジェクト配置等で使う）
    LevelEditor* GetLevelEditor(){ return levelEditor_.get(); }

    // --- ノードエディタへの「ゲーム値」登録（シーンから転送）---
    //   プレイヤー速度・卵の投げ初速などの float を登録すると「→ ゲーム値」ノードで動かせる。
    //   ポインタは所有しないため、シーン終了時に必ず ClearNodeGameValues() を呼ぶこと。
    void RegisterNodeGameValue(const std::string& label, float* target, float minV, float maxV);
    void ClearNodeGameValues();

    // エディタがアクティブかどうか
    bool IsActive() const{ return isEditorActive_; }

    // ===== UIシェル（F2で切替）=====
    //   Dock : 従来どおりの全パネルドッキング表示（挙動・見た目は完全に従来のまま）
    //   Icon : ゲームビュー全画面＋左端アイコンツールバーで必要なパネルだけ浮遊表示
    enum class UiShell { Dock, Icon };
    // アイコンモードで開閉できるパネルの分類（アイコン1個=1パネル）
    enum EditorPanel {
        Panel_Control,     // 操作（Play/Stop・タイムスケール）
        Panel_Hierarchy,   // ヒエラルキー（配置リスト）
        Panel_Assets,      // アセットブラウザ
        Panel_Inspector,   // インスペクター（詳細設定）※9クラス合流の束
        Panel_Transform,   // インスペクター（Transform）
        Panel_Rail,        // レールエディタ
        Panel_Camera,      // カメラエディタ
        Panel_Items,       // 配置エディタ（コイン/ブロック）
        Panel_Enemy,       // 敵配置エディタ
        Panel_GpuParticle, // GPUパーティクル
        Panel_Sdf,         // SDF（フォント/画像）
        Panel_Perf,        // パフォーマンスモニター
        Panel_File,        // ファイルエディタ
        Panel_Count
    };
    UiShell GetUiShell() const{ return uiShell_; }
    // このパネルを今フレーム描くべきか。Dockモードは常に true（＝従来と同一）、
    // Iconモードではアイコンで選んだものだけ true（非表示パネルは Begin ごと呼ばれない＝コストゼロ）
    bool IsPanelVisible(int panel) const{
        if ( uiShellFrame_ == UiShell::Dock ) return true; // フレーム先頭で確定した値で判定
        return panel >= 0 && panel < Panel_Count && panelVisible_[panel];
    }

	// シーンから SkinnedObj3d を登録する。シーン終了時は必ず nullptr を渡してリセットすること
	void SetTargetSkinnedObj(SkinnedObj3d* obj){ targetSkinnedObj_ = obj; }

	// ギズモ／インスペクタで操作する対象オブジェクトを登録する（シーンから渡す）
	void SetGizmoTarget(Obj3d* obj){ gizmoTarget_ = obj; }

	// --- レール編集データの公開（ゲーム側が同じレールを使うため）---
	// 編集の世代番号。ノード移動/追加/削除/直線/カーブ/読込のたびに増える
	int GetRailEditVersion() const;
	// 現在エディタが保持しているレールの節点リスト
	const std::vector<std::vector<Vector3>>& GetEditorRailLines() const;
	// 各レールのタイプ（-1=自動 / 0=横 / 1=縦）。railLines と同じ並び
	const std::vector<int>& GetEditorRailTypes() const;
	// 各レールの動き（x,y,z=振幅 / w=周期）。railLines と同じ並び
	const std::vector<Vector4>& GetEditorRailMotions() const;
	// 各レールの地面タイプ（0=Safe/1=Gap/2=NoGround）。railLines と同じ並び
	const std::vector<int>& GetEditorRailGroundTypes() const;
	// 各レールのノード単位の穴指定（外=レール / 内=ノード・1=穴）
	const std::vector<std::vector<int>>& GetEditorRailNodeHoles() const;
	// 各レールの表示フラグ（1=表示/0=非表示）。railLines と同じ並び
	const std::vector<int>& GetEditorRailVisible() const;
	// 各レールの線のつなぎ方（0=スプライン/1=直線）。railLines と同じ並び
	const std::vector<int>& GetEditorRailLineModes() const;
	// 各レールの道生成モード（0=自動/1=なし）。railLines と同じ並び（§4）
	const std::vector<int>& GetEditorRailRoadModes() const;
	const std::vector<int>& GetEditorRailEndPlazas() const; // 端の丸広場（bit0=始点/bit1=終点）
	const std::vector<int>& GetEditorRailGuideRails() const; // ガイドレール追従の対象（-1=なし）
	const std::vector<CoinData>& GetEditorCoins() const; // 収集物（コイン）の配置

	// --- ブロック（乗れる/ぶつかる1m角。Game Viewペイント配置の橋渡し）---
	const std::vector<BlockData>& GetEditorBlocks() const;
	int  GetEditorBlockVersion() const;                    // 変化したらゲーム側が作り直す
	bool IsEditorBlockPaintMode() const;                   // 配置モード中か（Game Viewクリックの奪い先）
	bool IsEditorBlockEraseMode() const;                   // 消しゴムモード中か
	int  GetEditorBlockPaintType() const;                  // 置くブロックの種類
	int  GetEditorBlockPaintShape() const;                 // 塗り方（0=1個/1=柱/2=階段）
	bool HasEditorBlock(int rail, float dist, int level, float side) const;
	void AddEditorBlock(int rail, float dist, int level, float side, int type);
	void RemoveEditorBlock(int rail, float dist, int level, float side);
	bool GetEditorRailMotionPreview() const; // 動くレールをエディタ中も再生するか
	// デモ展示（回転ブロック/オーラ/SDF卵/見本の人形）を表示するか（表示メニューでON/OFF）
	bool IsDemoVisible() const{ return showDemo_; }
	// レールエディタで選択中のノード（レール番号＋ワールド座標）。
	//   敵エディタの「選択ノードの位置に配置」用。未選択なら false
	bool GetEditorSelectedNode(int& outRail, Vector3& outPos) const;

	// Game View 上のマウス情報（ゲーム側の配置エディタが独自ピッキングをするための橋渡し）
	struct GameViewMouse {
		bool      hovered = false;     // マウスが Game View 画像の上にあるか
		bool      gizmoActive = false; // ギズモ操作中（クリックを奪わないため）
		Vector2   mousePos {};         // マウスのスクリーン座標
		Vector2   imgMin {};           // Game View 画像の左上（スクリーン座標）
		Vector2   imgSize {};          // Game View 画像のサイズ
		Matrix4x4 viewProj {};         // エディタ表示に使ったカメラの VP 行列
	};
	const GameViewMouse& GetGameViewMouse() const{ return gameViewMouse_; }
	// ゲーム側がドラッグ操作中（敵の直接ドラッグ等）にレール編集のマウス操作を止める
	void SetExternalDragActive(bool active){ externalDragActive_ = active; }
	// ジョイント表示モード（0=エディタのみ/1=常に/2=非表示。RoadMesh が参照）
	int GetEditorJointVisible() const;
	// レールノードをギズモ/フリーハンドでドラッグ中か（ドラッグ中は道の再生成を間引く）
	bool IsRailDragging() const{ return railSelDragging_ || railFreehandStroking_; }
	const std::vector<int>&   GetEditorRailMotionTypes() const;  // 動きの波形（0=sin/1=停止つき/2=円）
	const std::vector<float>& GetEditorRailMotionPhases() const; // 動きの位相（0〜1）
	const std::vector<int>&   GetEditorRailOneWay() const;       // 片方向（0=両/1=正/2=逆）
	const std::vector<float>& GetEditorRailSpeedMuls() const;    // 速度倍率
	int GetEditorStartRail() const;  // スタート地点（レール番号）
	int GetEditorStartNode() const;  // スタート地点（ノード番号）
	int GetEditorGoalRail() const;   // ゴール地点（レール番号。-1=未設定）
	int GetEditorGoalNode() const;   // ゴール地点（ノード番号）
	const std::vector<LevelCameraZone>& GetEditorCameraZones() const; // カメラ演出ゾーン

	// --- 敵の配置（マップと一緒に保存/読込）。シーンの EnemyEditor と同期する ---
	void SetEditorEnemyData(const std::vector<LevelEnemyData>& e); // シーン→エディタ（保存に乗せる）
	const std::vector<LevelEnemyData>& GetEditorEnemyData() const; // エディタ→シーン（読込で復元）
	int  GetMapLoadVersion() const;                                // 増えたら新しいマップが読まれた合図

	// メニューバーから ON/OFF するためにシーンのデバッグカメラを登録する（所有しない）
	void SetDebugCamera(DebugCamera* dc){ debugCamera_ = dc; }

	// シーン切り替え時に外部参照をまとめてリセットする（ダングリングポインタ防止）
	void ResetSceneReferences(){
		targetSkinnedObj_ = nullptr;
		gizmoTarget_ = nullptr;
		editorCamera_ = nullptr;
		debugCamera_ = nullptr;
		gameViewSrvIndex_ = 0;
	}

    EngineMode GetMode() const{ return currentMode_; }

    // Blenderインポータの取得（シーン側がカメラ適用要求を受け取るのに使う）
    BlenderImporter* GetBlenderImporter() const{ return blenderImporter_.get(); }

private:

    // シングルトンなので外部からの生成・コピーを禁止
    EditorManager() = default;
    ~EditorManager();  // cpp 側で定義（unique_ptr の前方宣言対応）
    EditorManager(const EditorManager&) = delete;
    EditorManager& operator=(const EditorManager&) = delete;


private:

	// レベルエディタ（SceneManagerから渡してもらう）
    std::unique_ptr<LevelEditor> levelEditor_ = nullptr;

    // Blenderシーンインポータ（levelEditor_ に反映する）
    std::unique_ptr<BlenderImporter> blenderImporter_ = nullptr;

    // 性能モニター（master_engine から移植：FPS/メモリ/VRAM/ドローコール）
    std::unique_ptr<PerformanceMonitor> perfMonitor_ = nullptr;

    // ファイルエディタ（Project風。master_engine から移植）
    std::unique_ptr<FileEditor> fileEditor_ = nullptr;

    // ノードエディタ（ブループリント風。master_engine から移植）
    std::unique_ptr<NodeEditor> nodeEditor_ = nullptr;
    bool showNodeEditor_ = false; // 表示メニューでON/OFF

    // Blenderインポータのパネル表示（普段は邪魔なので非表示。表示メニューでON/OFF。
    //   非表示でもエクスプローラーからのD&Dインポート等の機能自体は生きている）
    bool showBlenderImporter_ = false;

    // デモ展示（回転ブロック/オーラ/SDF卵/見本の人形）と調整項目ウィンドウの表示。
    //   マップ制作には不要なので既定OFF。表示メニューでON/OFF（GamePlaySceneが参照）
    bool showDemo_ = false;
    bool showGlobalVars_ = false;

    // --- UIシェル（アイコンモード）の状態 ---
    void DrawIconToolbar();          // 左端のアイコンツールバー（Iconモードのみ）
    void DrawUiSettingsWindow();     // フォント・UIスケール設定ウィンドウ
    void ApplyWorkspace(int index);  // ワークスペースプリセット適用（0=設置/1=調整/2=レール/3=敵/4=全部閉じる）
    void SaveUiConfig() const;       // resources/editor_ui.ini へ保存（モード/パネル/フォント）
    void LoadUiConfig();
    UiShell uiShell_ = UiShell::Dock;
    // このフレームで実際に使うシェル（Beginでドッキング有効/無効と同時に確定させる。
    //   F2等でフレーム途中に uiShell_ が変わっても、UI分岐は必ずこちらを使うこと。
    //   途中で切り替えると「ドッキング無効のままドック再構築」が走りImGui内部でクラッシュする）
    UiShell uiShellFrame_ = UiShell::Dock;
    bool panelVisible_[Panel_Count] = {}; // Iconモードで表示中のパネル
    bool dockRelayoutPending_ = false;    // アイコン→ドック復帰時に初期レイアウトを組み直す
    float uiDrawerWidth_ = 460.0f;        // ドロワーの幅（UI設定・縁ドラッグで変更、保存）
    int   uiDrawerSide_ = 0;              // ドロワーの位置（0=右 / 1=左。ツールバーは反対側へ）
    bool  uiAutoShowEditor_ = false;      // 起動時からエディタを表示する（F1不要にする設定）
    bool  uiDrawerWidthDirty_ = false;    // 縁ドラッグ中フラグ（離した時に保存）
    bool uiConfigLoaded_ = false;
    bool showUiSettings_ = false;
    int  uiFontIndex_ = 0;           // ImGuiManager のフォント一覧のインデックス
    float uiFontScale_ = 1.0f;

    // Game View マウス情報（毎フレーム更新。ゲーム側の配置エディタへの橋渡し）
    GameViewMouse gameViewMouse_ {};
    bool externalDragActive_ = false; // ゲーム側がドラッグ中（レール編集のマウス操作を止める）

	// カメラ（SceneManagerから渡してもらう）
    bool isEditorActive_ = false;
    // パフォーマンスモニター用（SceneManagerから渡してもらう）
    float cpuUpdateTimeMs_ = 0.0f;
    float cpuDrawTimeMs_ = 0.0f;

	// Game View に表示するテクスチャのSRVインデックス（SceneManagerから渡してもらう）
    uint32_t gameViewSrvIndex_ = 0;

    std::unique_ptr<GPUParticleEditor> gpuParticleEditor_ = nullptr;

    SkinnedObj3d* targetSkinnedObj_ = nullptr;

    // ギズモ／インスペクタ用
    Obj3d* gizmoTarget_ = nullptr;       // 操作対象（所有しない）
    const Camera* editorCamera_ = nullptr; // ギズモ計算に使うカメラ（所有しない）
    DebugCamera* debugCamera_ = nullptr;   // シーンのデバッグカメラ（所有しない・メニューから操作）
    int gizmoOperation_ = 7;             // ImGuizmo::TRANSLATE(=7) を既定に
    int gizmoMode_ = 1;                  // ImGuizmo::WORLD(=1)

    bool railFreehandStroking_ = false;  // フリーハンドで描画ストローク中か

    // レール編集：矩形選択／まとめて移動用
    bool  railRubberActive_ = false;            // 矩形選択ドラッグ中か
    float railRubberStartX_ = 0.0f;             // 矩形選択の開始位置（スクリーン座標）
    float railRubberStartY_ = 0.0f;
    Vector3 railSelPivot_ { 0.0f, 0.0f, 0.0f }; // 選択ギズモのピボット（ドラッグ中は保持）
    bool  railSelDragging_ = false;             // ギズモで選択群を移動中か

    // 自動スナップ接続（§5）：端点ドラッグ中の接続候補（緑プレビュー→マウスアップで確定）
    RailEditor::SnapTarget railSnapCandidate_ {};
    int   railSnapDragRail_  = -1;   // ドラッグ中の端点が属するレール（-1=候補なし）
    bool  railSnapDragFront_ = true; // ドラッグ中の端点が先頭側か
    bool  railSnapHeightGap_ = false; // 候補が「真上から見れば近いが高さだけズレている」相手か

    // エディタ有り(USE_IMGUI)＝編集モードから開始。リリース＝エディタが無いので
    // いきなりプレイモードで開始し、プレイヤーが動けて敵も出る状態にする。
#ifdef USE_IMGUI
    EngineMode currentMode_ = EngineMode::Edit;
#else
    EngineMode currentMode_ = EngineMode::Play;
#endif
};
