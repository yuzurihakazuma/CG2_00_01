#include "SceneManager.h"
// --- 標準ライブラリ ---
#include <algorithm>
#include <cassert>
#include <cmath>
#include <random>
#include <vector>
// --- エンジン側のファイル ---
#include "engine/scene/IScene.h"
#include "engine/scene/AbstractSceneFactory.h"
#include "engine/base/DirectXCommon.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/graphics/SrvManager.h"
#include "engine/base/Input.h"
#include "engine/base/WindowProc.h"
#include "engine/2d/Sprite.h"
#include "engine/2d/SpriteCommon.h"
#include "game/audio/GameSE.h"
#include "engine/graphics/TextureManager.h"



namespace {
std::vector<uint32_t> LoadFadeCardTextures(ID3D12GraphicsCommandList* commandList) {
	const char* texturePaths[] = {
		"resources/card/CardR.png",
		"resources/card/Card.png",
		"resources/card/swordCard.png",
		"resources/card/spearCard.png",
		"resources/card/kickCard.png",
		"resources/card/hammerCard.png",
		"resources/card/CardFire.png",
		"resources/card/CardIce.png",
		"resources/card/CardPotion.png",
		"resources/card/CardShield.png",
		"resources/card/CardSpeedUp.png",
		"resources/card/CardClaw.png",
		"resources/card/CardFang.png",
		"resources/card/CardDecoy.png",
		"resources/card/CardCostBoost.png",
		"resources/card/CardAtkDown.png",
	};

	std::vector<uint32_t> textureIndices;
	textureIndices.reserve(std::size(texturePaths));
	for (const char* texturePath : texturePaths) {
		TextureData texture = TextureManager::GetInstance()->LoadTextureAndCreateSRV(texturePath, commandList);
		textureIndices.push_back(texture.srvIndex);
	}

	return textureIndices;
}
}


// シングルトンクラスの実装
SceneManager* SceneManager::GetInstance() {
	static SceneManager instance;
	return &instance;
}

void SceneManager::EnsureFadeSprites(uint32_t fadeTextureIndex, const std::vector<uint32_t>& cardTextureIndices) {
	const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
	if (!cardTextureIndices.empty()) {
		fadeCardTextureIndices_ = cardTextureIndices;
	}

	// フェードは現在のクライアントサイズ基準で作って、画面サイズ変更後も全面を覆う
	if (!fadeSprite_) {
		fadeSprite_ = std::unique_ptr<Sprite>(Sprite::Create(fadeTextureIndex, { screenW * 0.5f, screenH * 0.5f }));
	}
	if (fadeSprite_) {
		fadeSprite_->SetSize({ screenW, screenH });
		fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, fadeAlpha_ });
		fadeSprite_->Update();
	}

	for (int i = 0; i < kFadeCardCount_; ++i) {
		if (!fadeCardSprites_[i]) {
			fadeCardSprites_[i] = std::unique_ptr<Sprite>(Sprite::Create(fadeTextureIndex, { 0.0f, 0.0f }));
		}
	}
	RandomizeFadeCardTextures();
	UpdateFadeCardSprites();
}

void SceneManager::RandomizeFadeCardTextures() {
	if (fadeCardTextureIndices_.empty()) {
		return;
	}

	static std::mt19937 randomEngine(std::random_device{}());
	std::uniform_int_distribution<int> backOrFace(0, 1);
	const size_t faceCount = fadeCardTextureIndices_.size() - 1;
	std::uniform_int_distribution<size_t> faceIndex(0, faceCount > 0 ? faceCount - 1 : 0);

	for (auto& cardSprite : fadeCardSprites_) {
		if (!cardSprite) {
			continue;
		}

		// 裏面カードを約5割混ぜつつ、表面カードは遷移ごとにランダムで選ぶ
		const bool useBack = backOrFace(randomEngine) == 0;
		const size_t textureIndex = (useBack || faceCount == 0)
			? 0
			: 1 + faceIndex(randomEngine);
		cardSprite->SetTexture(fadeCardTextureIndices_[textureIndex]);
	}
}

void SceneManager::UpdateFadeCardSprites() {
	if (fadeState_ == FadeState::None) {
		return;
	}

	const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
	const float coverT = std::clamp(fadeAlpha_, 0.0f, 1.0f);
	float phaseT = coverT;
	if (fadeState_ == FadeState::In) {
		phaseT = 1.0f + (1.0f - coverT);
	} else if (fadeState_ == FadeState::Wait) {
		phaseT = 1.0f;
	}
	const float entryT = std::clamp(phaseT, 0.0f, 1.0f);
	const float exitT = std::clamp((phaseT - 1.18f) / 0.82f, 0.0f, 1.0f);
	const float easedEntryT = entryT * entryT * (3.0f - 2.0f * entryT);
	const float easedExitT = exitT * exitT * (3.0f - 2.0f * exitT);
	const float cardW = screenW * 0.17f;
	const float cardH = cardW * 1.42f;
	const int columnCount = 10;
	const float spacingX = (screenW + cardW * 0.8f) / static_cast<float>(columnCount - 1);
	const float spacingY = cardH * 0.34f;

	for (int i = 0; i < kFadeCardCount_; ++i) {
		if (!fadeCardSprites_[i]) {
			continue;
		}

		const int column = i % columnCount;
		const int row = i / columnCount;
		const float rowDelay = static_cast<float>(row) * 0.038f;
		const float columnDelay = static_cast<float>((column * 3) % columnCount) * 0.012f;
		const float delay = rowDelay + columnDelay;
		const float entryLocalT = std::clamp((easedEntryT - delay) / 0.56f, 0.0f, 1.0f);
		const float exitLocalT = std::clamp((easedExitT - delay * 0.35f) / 0.78f, 0.0f, 1.0f);
		const float targetX = -cardW * 0.42f + static_cast<float>(column) * spacingX + ((row % 2 == 0) ? -cardW * 0.06f : cardW * 0.12f);
		const float targetY = static_cast<float>(row) * spacingY - cardH * 0.42f;
		const float startY = -cardH * (1.35f + static_cast<float>((i * 5) % 9) * 0.16f);
		const float exitY = screenH + cardH * (1.2f + static_cast<float>((i * 7) % 6) * 0.1f);
		const float moveT = (phaseT <= 1.0f) ? entryLocalT : exitLocalT;
		const float wobble = std::sinf(moveT * 3.14159f) * (24.0f + static_cast<float>(i % 4) * 8.0f);
		const float x = targetX + wobble;
		const float y = (phaseT <= 1.0f)
			? startY + (targetY - startY) * entryLocalT
			: targetY + (exitY - targetY) * exitLocalT;
		const float alpha = (phaseT <= 1.0f)
			? std::clamp(entryLocalT * 1.6f, 0.0f, 1.0f)
			: std::clamp(1.0f - exitLocalT * 0.9f, 0.0f, 1.0f);
		const float rotation = -0.35f + static_cast<float>((i * 37) % 100) * 0.007f;

		fadeCardSprites_[i]->SetPosition({ x, y });
		fadeCardSprites_[i]->SetSize({ cardW, cardH });
		fadeCardSprites_[i]->SetRotation(rotation);
		fadeCardSprites_[i]->SetColor({ 1.0f, 1.0f, 1.0f, alpha });
		fadeCardSprites_[i]->Update();
	}
}

void SceneManager::DrawFadeCardSprites() {
	if (fadeState_ == FadeState::None) {
		return;
	}

	for (auto& cardSprite : fadeCardSprites_) {
		if (cardSprite) {
			cardSprite->Draw();
		}
	}
}

// デストラクタ
SceneManager::~SceneManager() {
	// 現在のシーンを終了
	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_ = nullptr;
	}

	// フェード用スプライトを破棄
	fadeSprite_.reset();
	for (auto& cardSprite : fadeCardSprites_) {
		cardSprite.reset();
	}
}

// シーンマネージャーの更新
void SceneManager::Update(){
	// -----------------------------
	// フェードアウト処理
	// -----------------------------
	if ( fadeState_ == FadeState::Out ) {
		fadeAlpha_ += fadeSpeed_;

		if ( fadeAlpha_ >= 1.0f ) {
			fadeAlpha_ = 1.0f;

			// 真っ黒になったら待機状態へ移る
			fadeState_ = FadeState::Wait;
			fadeWaitTimer_ = fadeWaitTimeMax_;
		}
	}
	// -----------------------------
	// 真っ黒で停止する処理
	// -----------------------------
	else if ( fadeState_ == FadeState::Wait ) {
		fadeWaitTimer_--;

		if ( fadeWaitTimer_ <= 0 ) {
			// 真っ黒時間が終わったらシーンを切り替える
			if ( nextScene_ ) {
				// 現在のシーンを終了
				if ( currentScene_ ) {
					currentScene_->Finalize();
				}

				// シーンを切り替え
				currentScene_ = std::move(nextScene_);
				nextScene_ = nullptr;

				DirectXCommon* dxCommon = DirectXCommon::GetInstance();
				dxCommon->BeginCommandRecording();

				// フェード用スプライトがまだ無ければここで作成する
				if ( !fadeSprite_ || !fadeCardSprites_[0] ) {
					auto commandList = dxCommon->GetCommandList();

					TextureData fadeTex = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/white1x1.png", commandList);
					std::vector<uint32_t> cardTextureIndices = LoadFadeCardTextures(commandList);
					EnsureFadeSprites(fadeTex.srvIndex, cardTextureIndices);
				}

				currentScene_->Initialize();
				dxCommon->EndCommandRecording();
			}

			// 切り替え後はフェードインへ移る
			fadeState_ = FadeState::In;
		}
	}
	// -----------------------------
	// フェードイン処理
	// -----------------------------
	else if ( fadeState_ == FadeState::In ) {
		fadeAlpha_ -= fadeSpeed_;

		if ( fadeAlpha_ <= 0.0f ) {
			fadeAlpha_ = 0.0f;
			fadeState_ = FadeState::None;
		}
	}

	// フェードスプライトの色を更新
	if ( fadeSprite_ ) {
		const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
		const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
		fadeSprite_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
		fadeSprite_->SetSize({ screenW, screenH });
		fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, fadeAlpha_ });
		fadeSprite_->Update();
	}
	UpdateFadeCardSprites();

	// 現在のシーンを更新
	if ( currentScene_ ) {

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

	// 最後にフェードを上から重ねる
	if (fadeSprite_ && fadeState_ != FadeState::None) {
		auto commandList = DirectXCommon::GetInstance()->GetCommandList();
		SpriteCommon::GetInstance()->PreDraw(commandList);
		fadeSprite_->Draw();
		DrawFadeCardSprites();
	}
		// フェードの上に描いて、暗転中でもローディング表示が見えるようにする
}

// 文字列を使ってファクトリーにシーンを作ってもらう
void SceneManager::ChangeScene(const std::string& sceneName) {
	assert(sceneFactory_ && "SceneManager: SceneFactory is not set!");

	// すでにフェード中なら新しいシーン変更を受け付けない
	if (fadeState_ != FadeState::None) {
		return;
	}

	// 次のシーンを予約してフェードアウト開始
	nextScene_ = sceneFactory_->CreateScene(sceneName);
	//GameSE::SceneTransition();
	RandomizeFadeCardTextures();
	fadeState_ = FadeState::Out;
	fadeAlpha_ = 0.0f;
	fadeWaitTimer_ = 0;
	UpdateFadeCardSprites();
}

// シーン変更（直接シーンを渡す）
void SceneManager::ChangeScene(std::unique_ptr<IScene> nextScene) {
	// すでにフェード中なら新しいシーン変更を受け付けない
	if (fadeState_ != FadeState::None) {
		return;
	}

	nextScene_ = std::move(nextScene);
	//GameSE::SceneTransition();
	RandomizeFadeCardTextures();
	fadeState_ = FadeState::Out;
	fadeAlpha_ = 0.0f;
	fadeWaitTimer_ = 0;
	UpdateFadeCardSprites();
}

void SceneManager::SetFirstScene(std::unique_ptr<IScene> scene) {
	// 既にシーンがある場合は何もしない
	if (currentScene_) {
		return;
	}

	currentScene_ = std::move(scene);

	if (currentScene_) {
		DirectXCommon* dxCommon = DirectXCommon::GetInstance();
		dxCommon->BeginCommandRecording();
		if ( !fadeSprite_ || !fadeCardSprites_[0] ) {
			auto commandList = dxCommon->GetCommandList();
			TextureData fadeTex = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/white1x1.png", commandList);
			std::vector<uint32_t> cardTextureIndices = LoadFadeCardTextures(commandList);
			// 最初のシーン開始時にフェード用スプライトを作っておき、初回遷移から確実に描けるようにする
			EnsureFadeSprites(fadeTex.srvIndex, cardTextureIndices);
		}
		currentScene_->Initialize();
		dxCommon->EndCommandRecording();
	}

	// 最初のシーンなのでフェードは使わない
	nextScene_.reset();
	fadeAlpha_ = 0.0f;
	fadeWaitTimer_ = 0;
	fadeState_ = FadeState::None;
}

void SceneManager::Finalize() {
	// 現在のシーンを破棄する
	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_.reset();
	}

	// 次のシーンも破棄する
	nextScene_.reset();

	// フェード用スプライトを破棄する
	fadeSprite_.reset();
	for (auto& cardSprite : fadeCardSprites_) {
		cardSprite.reset();
	}

	// フェード状態を初期化する
	fadeAlpha_ = 0.0f;
	fadeWaitTimer_ = 0;
	fadeState_ = FadeState::None;

}

void SceneManager::DrawCurrentSceneDebugUI(){
	if ( currentScene_ ) {
		currentScene_->DrawDebugUI();
	}
}
