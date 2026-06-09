#pragma once
#include <memory>
#include <vector>
#include "engine/math/struct.h" // Vector3

class LevelEditor;
class Camera;
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

	// シーン切り替え時に外部参照をまとめてリセットする（ダングリングポインタ防止）
	void ResetSceneReferences(){
		targetSkinnedObj_ = nullptr;
		gizmoTarget_ = nullptr;
		editorCamera_ = nullptr;
		gameViewSrvIndex_ = 0;
	}

    EngineMode GetMode() const{ return currentMode_; }

private:

    // シングルトンなので外部からの生成・コピーを禁止
    EditorManager() = default;
    ~EditorManager();  // cpp 側で定義（unique_ptr の前方宣言対応）
    EditorManager(const EditorManager&) = delete;
    EditorManager& operator=(const EditorManager&) = delete;


private:

	// レベルエディタ（SceneManagerから渡してもらう）
    std::unique_ptr<LevelEditor> levelEditor_ = nullptr;

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
    int gizmoOperation_ = 7;             // ImGuizmo::TRANSLATE(=7) を既定に
    int gizmoMode_ = 1;                  // ImGuizmo::WORLD(=1)

    bool railFreehandStroking_ = false;  // フリーハンドで描画ストローク中か

    EngineMode currentMode_ = EngineMode::Edit;
};
