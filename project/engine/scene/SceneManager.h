#pragma once
// --- 標準ライブラリ ---
#include <array>
#include <memory>
#include <string>
#include <chrono>
#include <cstdint>
#include <vector>
// 前方宣言
class IScene;
class AbstractSceneFactory;
class Sprite;

// シーンマネージャークラス
class SceneManager {
public:
	// シングルトンインスタンスの取得
	static SceneManager* GetInstance();

	// シーンマネージャーの更新
	void Update();

	// シーンマネージャーの描画
	void Draw();

	// シーン変更予約
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) { sceneFactory_ = sceneFactory; }

	// シーン変更（シーン名で指定）
	void ChangeScene(const std::string& sceneName);

	// シーン変更（直接シーンを渡す）
	void ChangeScene(std::unique_ptr<IScene> nextScene);

	// 終了処理
	void Finalize();

	// 現在のシーン固有のデバッグUIを描画
	void DrawCurrentSceneDebugUI();

	// パフォーマンス計測値のゲッター
	float GetCpuUpdateTimeMs() const{ return cpuUpdateTimeMs_; }
	float GetCpuDrawTimeMs() const{ return cpuDrawTimeMs_; }

	// エディタのアクティブ状態を管理
	bool IsEditorActive() const { return isEditorActive_; }

	// 最初のシーンを設定（ゲーム開始時に一度だけ呼び出す）
	void SetFirstScene(std::unique_ptr<IScene> scene);
private:
	// コンストラクタを private にして外部からの生成を禁止
	SceneManager() = default;
	~SceneManager();
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	void EnsureFadeSprites(uint32_t fadeTextureIndex, const std::vector<uint32_t>& cardTextureIndices);
	void RandomizeFadeCardTextures();
	void UpdateFadeCardSprites();
	void DrawFadeCardSprites();

private:
	// 現在のシーン
	std::unique_ptr<IScene> currentScene_ = nullptr;

	// 次に切り替えるシーン
	std::unique_ptr<IScene> nextScene_ = nullptr;

	// シーンファクトリー
	AbstractSceneFactory* sceneFactory_ = nullptr;

	// エディタ表示フラグ
	bool isEditorActive_ = false;

private:
	// -----------------------------
	// フェード演出用
	// -----------------------------

	// フェード用スプライト
	std::unique_ptr<Sprite> fadeSprite_ = nullptr;

	// タイトルのカード雨のように画面を覆うフェード用カード
	static constexpr int kFadeCardCount_ = 90;
	std::array<std::unique_ptr<Sprite>, kFadeCardCount_> fadeCardSprites_{};
	std::vector<uint32_t> fadeCardTextureIndices_{};

	// フェードの透明度
	float fadeAlpha_ = 0.0f;

	// フェード速度
	float fadeSpeed_ = 0.035f;

	// 真っ黒で停止する時間
	int fadeWaitTimer_ = 0;

	// 真っ黒で停止する最大時間
	int fadeWaitTimeMax_ = 8;

	// フェード状態
	enum class FadeState {
		None,   // 通常
		Out,    // フェードアウト中
		Wait,   // 真っ黒で停止中
		In      // フェードイン中
	};

	FadeState fadeState_ = FadeState::None;
	float cpuUpdateTimeMs_ = 0.0f;
	float cpuDrawTimeMs_ = 0.0f;
};
