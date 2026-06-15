#pragma once
// --- 標準ライブラリ ---
#include <memory>
#include <string>
#include <chrono>
// 前方宣言
class IScene;
class AbstractSceneFactory;
class Sprite;

// シーンマネージャークラス
class SceneManager{
public:
	// シングルトンインスタンスの取得
	static SceneManager* GetInstance();
	// シーンマネージャーの初期化
	void Update();
	// シーンマネージャーの描画
	void Draw();
	// シーン変更予約
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) { sceneFactory_ = sceneFactory; }
	
	void ChangeScene(const std::string& sceneName);

	// シーン変更（シーン名で指定）
	void ChangeScene(std::unique_ptr<IScene> nextScene);

	// フェード付きシーン切替：黒フェードアウト→切替→フェードイン
	void ChangeSceneWithFade(const std::string& sceneName, float fadeOutSec = 0.3f, float fadeInSec = 0.3f);
	// フェード中か
	bool IsFading() const { return fadeState_ != FadeState::None; }

	void Finalize();

	// 現在のシーン固有のデバッグUIを描画
	void DrawCurrentSceneDebugUI();

	// パフォーマンス計測値のゲッター
	float GetCpuUpdateTimeMs() const{ return cpuUpdateTimeMs_; }
	float GetCpuDrawTimeMs() const{ return cpuDrawTimeMs_; }


private:
	// コンストラクタを private にして外部からの生成を禁止
	SceneManager() = default;
	~SceneManager();
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	std::unique_ptr<IScene> currentScene_ = nullptr;
	std::unique_ptr<IScene> nextScene_ = nullptr;

	AbstractSceneFactory* sceneFactory_ = nullptr;

	float cpuUpdateTimeMs_ = 0.0f;
	float cpuDrawTimeMs_ = 0.0f;

	// --- シーン遷移フェード ---
	enum class FadeState{ None, FadeOut, FadeIn };
	FadeState   fadeState_ = FadeState::None;
	float       fadeTimer_ = 0.0f;
	float       fadeOutDuration_ = 0.3f;
	float       fadeInDuration_ = 0.3f;
	float       fadeAlpha_ = 0.0f;          // 黒オーバーレイの不透明度(0〜1)
	std::string pendingSceneName_;          // フェードアウト後に作るシーン名
	std::unique_ptr<Sprite> fadeSprite_;    // 全画面の黒オーバーレイ

	void DoSceneSwitch();      // nextScene_ への実切替（即時・フェード共通）
	void EnsureFadeSprite();   // フェード用スプライトを必要時に生成
	void DrawFadeOverlay();    // 黒オーバーレイ描画
};
