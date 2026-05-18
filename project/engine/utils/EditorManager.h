#pragma once
#include <memory>  

class LevelEditor;      
class Camera;  
class GPUParticleEditor;
class GPUParticleEmitter;
class SkinnedObj3d;


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

	void SetTargetSkinnedObj(SkinnedObj3d* obj){ targetSkinnedObj_ = obj; }

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

    EngineMode currentMode_ = EngineMode::Edit;
};
