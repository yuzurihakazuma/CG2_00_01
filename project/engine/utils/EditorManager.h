#pragma once
#include <memory>
#include <vector>
#include "engine/math/struct.h" // Vector3
#include "engine/utils/Level/LevelData.h" // LevelEnemyData

class LevelEditor;
class BlenderImporter;
class Camera;
class DebugCamera;
class GPUParticleEditor;
class GPUParticleEmitter;
class SkinnedObj3d;
class Obj3d;


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

    // エディタがアクティブかどうか
    bool IsActive() const{ return isEditorActive_; }

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
	// 各レールの地面フラグ（true=安全/false=穴あり）。railLines と同じ並び
	const std::vector<bool>& GetEditorRailHasGround() const;

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

    // エディタ有り(USE_IMGUI)＝編集モードから開始。リリース＝エディタが無いので
    // いきなりプレイモードで開始し、プレイヤーが動けて敵も出る状態にする。
#ifdef USE_IMGUI
    EngineMode currentMode_ = EngineMode::Edit;
#else
    EngineMode currentMode_ = EngineMode::Play;
#endif
};
