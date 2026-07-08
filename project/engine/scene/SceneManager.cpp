#include "SceneManager.h"
// --- 標準ライブラリ ---
#include <cassert>
// --- エンジン側のファイル ---
#include "engine/scene/IScene.h"
#include "engine/scene/AbstractSceneFactory.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/Input.h"
#include "engine/base/WindowProc.h"
#include "engine/base/TimeManager.h"
#include "engine/2d/Sprite.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/graphics/TextureManager.h"
#include "engine/utils/Easing.h"



// シングルトンクラスの実装
SceneManager* SceneManager::GetInstance(){
	static SceneManager instance;
	return &instance;
}
// デストラクタ
SceneManager::~SceneManager(){
	// 現在のシーンを終了
	if ( currentScene_ ) {
		// 現在のシーンを終了
		currentScene_->Finalize();
		// シーン情報をクリア
		currentScene_ = nullptr;
	}
}
// シーンマネージャーの更新
void SceneManager::Update(){

#ifdef _DEBUG
	if ( Input::GetInstance()->Triggerkey(DIK_R) ) {
		if ( currentScene_ ) {
			currentScene_->Reload(); // 現在のシーンが何であれリロードを叩く！
		}
	}
#endif

	// --- フェード遷移の進行（実時間で。ポーズ中でも進む）---
	if ( fadeState_ != FadeState::None ) {
		const float dt = Time::GetInstance()->GetUnscaledDeltaTime();
		fadeTimer_ += dt;

		if ( fadeState_ == FadeState::FadeOut ) {
			float t = ( fadeOutDuration_ > 1e-6f ) ? ( fadeTimer_ / fadeOutDuration_ ) : 1.0f;
			fadeAlpha_ = Easing::EaseInOutQuad(t);
			if ( t >= 1.0f ) {
				fadeAlpha_ = 1.0f;
				// 画面が真っ黒なうちにシーンを切り替える
				if ( sceneFactory_ ) { nextScene_ = sceneFactory_->CreateScene(pendingSceneName_); }
				DoSceneSwitch();
				fadeState_ = FadeState::FadeIn;
				fadeTimer_ = 0.0f;
			}
		} else { // FadeIn
			float t = ( fadeInDuration_ > 1e-6f ) ? ( fadeTimer_ / fadeInDuration_ ) : 1.0f;
			fadeAlpha_ = 1.0f - Easing::EaseInOutQuad(t);
			if ( t >= 1.0f ) { fadeAlpha_ = 0.0f; fadeState_ = FadeState::None; }
		}
	}

	// 即時切替（フェード中以外）：従来どおり nextScene_ をそのまま反映
	if ( fadeState_ == FadeState::None && nextScene_ ) {
		DoSceneSwitch();
	}
	if (currentScene_) {
		auto updateStartTime = std::chrono::steady_clock::now();
		
		currentScene_->Update();

		auto updateEndTime = std::chrono::steady_clock::now();
		cpuUpdateTimeMs_ = std::chrono::duration<float, std::milli>(updateEndTime - updateStartTime).count();
		

	}


}
// シーンマネージャーの描画
void SceneManager::Draw(){
	if ( currentScene_ ) {

		auto drawStartTime = std::chrono::steady_clock::now();
		// 現在のシーンを描画
		currentScene_->Draw();

		auto drawEndTime = std::chrono::steady_clock::now();
		cpuDrawTimeMs_ = std::chrono::duration<float, std::milli>(drawEndTime - drawStartTime).count();
	}

	// シーンの上に黒フェードを重ねる（フェード中のみ）
	if ( fadeState_ != FadeState::None && fadeAlpha_ > 0.001f ) {
		DrawFadeOverlay();
	}
}

// nextScene_ への実切替（即時・フェード共通）
void SceneManager::DoSceneSwitch(){
	if ( !nextScene_ ) return;
	if ( currentScene_ ) { currentScene_->Finalize(); }
	currentScene_ = std::move(nextScene_);
	nextScene_ = nullptr;

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	dxCommon->BeginCommandRecording();
	currentScene_->Initialize();
	dxCommon->EndCommandRecording();
}

// フェード用の全画面スプライトを必要時に生成
void SceneManager::EnsureFadeSprite(){
	if ( fadeSprite_ ) return;
	uint32_t white = TextureManager::GetInstance()->Load("resources/block/white1x1.png").srvIndex;
	// 左上アンカーで画面いっぱい。色は黒・アルファは描画時に設定
	fadeSprite_ = Sprite::Create(white, { 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f });
}

// 黒オーバーレイ描画
void SceneManager::DrawFadeOverlay(){
	EnsureFadeSprite();
	if ( !fadeSprite_ ) return;

	float w = static_cast<float>( WindowProc::GetInstance()->GetClientWidth() );
	float h = static_cast<float>( WindowProc::GetInstance()->GetClientHeight() );
	fadeSprite_->SetSize({ w, h });
	fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, fadeAlpha_ });
	fadeSprite_->Update();

	auto commandList = DirectXCommon::GetInstance()->GetCommandList();
	SpriteCommon::GetInstance()->PreDraw(commandList);
	fadeSprite_->Draw();
}

// 文字列を使ってファクトリーにシーンを作ってもらう
void SceneManager::ChangeScene(const std::string& sceneName) {
	assert(sceneFactory_ && "SceneManager: SceneFactory is not set!");

	// ファクトリーが unique_ptr を作ってくれるので、そのまま受け取る
	nextScene_ = sceneFactory_->CreateScene(sceneName);
}

// シーン変更（シーン名で指定）
void SceneManager::ChangeScene(std::unique_ptr<IScene> nextScene){
	nextScene_ = std::move(nextScene);
}

// フェード付きシーン切替
void SceneManager::ChangeSceneWithFade(const std::string& sceneName, float fadeOutSec, float fadeInSec){
	assert(sceneFactory_ && "SceneManager: SceneFactory is not set!");
	if ( fadeState_ != FadeState::None ) return; // フェード中の多重要求は無視

	// 切り替え元のシーンが無い（起動直後）なら即時で
	if ( !currentScene_ ) { ChangeScene(sceneName); return; }

	pendingSceneName_ = sceneName;
	fadeOutDuration_ = fadeOutSec;
	fadeInDuration_ = fadeInSec;
	fadeState_ = FadeState::FadeOut;
	fadeTimer_ = 0.0f;
	fadeAlpha_ = 0.0f;
}


void SceneManager::Finalize() {
	// 現在のシーンを破棄する
	if (currentScene_) {	
		currentScene_->Finalize();
		currentScene_.reset();    
	}
}

void SceneManager::DrawCurrentSceneDebugUI(){
	if ( currentScene_ ) {
		currentScene_->DrawDebugUI();
	}
}