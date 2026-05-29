#include "TitleScene.h"

// ゲーム固有のファイル
#include "GamePlayScene.h"

// エンジン側のファイル
#include "Engine/Utils/ImGuiManager.h"
#include "Engine/Utils/Color.h"
#include "Engine/Utils/TextManager.h"
#include "Engine/Audio/AudioManager.h"
#include "game/audio/GameBGM.h"
#include "game/audio/GameSE.h"
#include "Engine/3D/Model/ModelManager.h"
#include "Engine/Particle/ParticleManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Camera/Camera.h"
#include "Engine/Camera/DebugCamera.h"
#include "Engine/2D/Sprite.h"
#include "Engine/3D/Obj/Obj3d.h"
#include "Engine/Base/Input.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "engine/math/VectorMath.h"
#include "engine/postEffect/PostEffect.h"
#include "game/map/MapManager.h"
#include"engine/utils/Level/LevelEditor.h"
#include "engine/utils/EditorManager.h"
#include <cmath>
#include <cstdlib>

#include "engine/graphics/SrvManager.h"
#include "Bloom.h"

using namespace VectorMath;
using namespace MatrixMath;

namespace {
bool HasControllerPromptInput(Input* input) {
	return input &&
		input->GetJoystickState() &&
		(input->PushJoystickButton(0xFFFF) ||
		 input->PushLeftTrigger(0.25f) ||
		 std::fabs(input->GetLeftStickX()) > 0.25f ||
		 std::fabs(input->GetLeftStickY()) > 0.25f ||
		 std::fabs(input->GetRightStickX()) > 0.25f ||
		 std::fabs(input->GetRightStickY()) > 0.25f);
}

bool HasKeyboardPromptInput(Input* input) {
	if (!input) {
		return false;
	}

	const BYTE keys[] = {
		DIK_SPACE, DIK_RETURN, DIK_ESCAPE,
		DIK_W, DIK_A, DIK_S, DIK_D,
		DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT
	};
	for (BYTE key : keys) {
		if (input->Pushkey(key)) {
			return true;
		}
	}

	return false;
}
}

TitleScene::TitleScene() {}

TitleScene::~TitleScene() {}

void TitleScene::Initialize() {
	hasConfirmedSelection_ = false;
	isCreditOpen_ = false;

	// ゲームシーンで付いたままのポストエフェクトをリセット
	auto* pe = PostEffect::GetInstance();
	pe->SetEffectActive(PostEffectType::Vignetting, false);
	pe->SetEffectActive(PostEffectType::Grayscale, false);
	pe->SetEffectActive(PostEffectType::RadialBlur, false);

	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	WindowProc* windowProc = WindowProc::GetInstance();

	// コマンドリスト取得
	auto commandList = dxCommon->GetCommandList();

	// BGMロード
	// タイトル用BGMを再生
	GameBGM::Title();

	// モデル読み込み
	ModelManager::GetInstance()->LoadModel("fence", "resources", "fence.obj");
	ModelManager::GetInstance()->LoadModel("grass", "resources", "terrain.obj");
	ModelManager::GetInstance()->LoadModel("block", "resources/block", "block.obj");
	ModelManager::GetInstance()->LoadModel("wall", "resources/wall", "wall.obj");
	ModelManager::GetInstance()->LoadModel("stairs", "resources/stairs", "stairs.obj");
	ModelManager::GetInstance()->LoadModel("ground", "resources/Ground", "Ground.obj");

	// 球モデル作成
	ModelManager::GetInstance()->CreateSphereModel("sphere", 16);

	// パーティクルグループ作成
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み
	textures_["uvChecker"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/uvChecker.png", commandList);
	textures_["monsterBall"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/monsterBall.png", commandList);
	textures_["fence"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/fence.png", commandList);
	textures_["circle"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle.png", commandList);

	// カメラ生成
	camera_ = std::make_unique<Camera>(
		windowProc->GetClientWidth(),
		windowProc->GetClientHeight(),
		dxCommon
	);
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });

	// デバッグカメラ生成
	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();

	// デプスステンシル作成
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(),
		windowProc->GetClientHeight()
	);

	// 必要ならマップエディタ初期化
	mapManager_ = std::make_unique<MapManager>();
	mapManager_->SetCamera(camera_.get());
	mapManager_->Initialize();

	// タイトル背景用にマップ中央を真上から見る
	const LevelData& level = mapManager_->GetLevelData();

	titleBgCameraPos_ = {
		((float)level.width - 1.0f) * level.tileSize * 0.5f,
		35.0f,
		((float)level.height - 1.0f) * level.tileSize * 0.5f
	};

	titleBgPlayerPos_ = mapManager_->GetMapCenterPosition();

	camera_->SetTranslation(titleBgCameraPos_);
	camera_->SetRotation(titleBgCameraRot_);
	camera_->SetFovY(1.0f);
	camera_->SetFarClip(200.0f);
	camera_->Update();

	titleBgCameraPos_ = { 49.0f, 7.4f, 49.1f };
	titleBgCameraRot_ = { 1.57f, -1.57f, 0.0f };

	// 全部壁ブロックで敷き詰めたい場合だけ使う
	mapManager_->FillAllTiles(1);

	// タイトル用テキストを設定
	
	const float screenW = static_cast<float>(windowProc->GetClientWidth());
	const float screenH = static_cast<float>(windowProc->GetClientHeight());
	

	titleLogoSprite_ = Sprite::Create("resources/UI/Title.png", { screenW * 0.5f, screenH * 0.32f });
	if (!isCreditOpen_ && titleLogoSprite_) {
		const float logoWidth = screenW * 0.78f;
		const float logoHeight = logoWidth * (1280.0f / 1920.0f);
		titleLogoSprite_->SetSize({ logoWidth, logoHeight });
		titleLogoSprite_->Update();
	}

	// プレイ（Yを画面の60%の位置に）
	playBlueSprite_ = Sprite::Create("resources/UI/TitlePlayChoice.png", { screenW * 0.5f, screenH * 0.58f });
	playWhiteSprite_ = Sprite::Create("resources/UI/TitlePlay.png", { screenW * 0.5f, screenH * 0.58f });
	if (playBlueSprite_) {
		playBlueSprite_->SetTextureRect(720.0f, 700.0f, 470.0f, 195.0f);
	}
	if (playWhiteSprite_) {
		playWhiteSprite_->SetTextureRect(720.0f, 700.0f, 470.0f, 195.0f);
	}

	// チュートリアル（Yを画面の75%の位置に）
	tutorialBlueSprite_ = Sprite::Create("resources/UI/TitleTutorialChoice.png", { screenW * 0.5f, screenH * 0.73f });
	tutorialWhiteSprite_ = Sprite::Create("resources/UI/TitleTutorial.png", { screenW * 0.5f, screenH * 0.73f });
	if (tutorialBlueSprite_) {
		tutorialBlueSprite_->SetTextureRect(580.0f, 930.0f, 910.0f, 185.0f);
	}
	if (tutorialWhiteSprite_) {
		tutorialWhiteSprite_->SetTextureRect(580.0f, 930.0f, 910.0f, 185.0f);
	}

	creditBlueSprite_ = Sprite::Create("resources/UI/TitleCreditChoic.png", { screenW * 0.5f, screenH * 0.80f });
	creditWhiteSprite_ = Sprite::Create("resources/UI/TitleCredit.png", { screenW * 0.5f, screenH * 0.80f });
	if (creditBlueSprite_) {
		creditBlueSprite_->SetTextureRect(650.0f, 930.0f, 685.0f, 190.0f);
	}
	if (creditWhiteSprite_) {
		creditWhiteSprite_->SetTextureRect(650.0f, 930.0f, 685.0f, 190.0f);
	}

	// 操作説明の読み込み
	operateSprite_ = Sprite::Create("resources/UI/TitleOperate.png", { screenW * 0.5f, screenH * 0.5f });
	controllerOperateSprite_ = Sprite::Create("resources/UI/TitleOperateC.png", { screenW * 0.5f, screenH * 0.5f });
	isControllerUiMode_ = Input::GetInstance()->GetJoystickState();

	creditDimSprite_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, screenH * 0.5f }, { 0.0f, 0.0f, 0.0f, 0.65f });
	creditSprite_ = Sprite::Create("resources/UI/Credit.png", { screenW * 0.5f, screenH * 0.5f });
	creditUiSprite_ = Sprite::Create("resources/UI/CreditUI.png", { screenW * 0.5f, screenH * 0.5f });

	// モデル読み込み（カード）
	ModelManager::GetInstance()->LoadModel("cardR", "resources/card", "CardR.obj");
	ModelManager::GetInstance()->LoadModel("cardF", "resources/card", "cardF.obj");
	ModelManager::GetInstance()->LoadModel("cardFire", "resources/card", "CardFire.obj");
	ModelManager::GetInstance()->LoadModel("cardPotion", "resources/card", "CardPotion.obj");
	ModelManager::GetInstance()->LoadModel("cardSpeedUp", "resources/card", "CardSpeedUp.obj");
	ModelManager::GetInstance()->LoadModel("CardShield", "resources/card", "CardShield.obj");
	ModelManager::GetInstance()->LoadModel("CardIce", "resources/card", "CardIce.obj");
	ModelManager::GetInstance()->LoadModel("CardFang", "resources/card", "CardFang.obj");
	ModelManager::GetInstance()->LoadModel("CardDecoy", "resources/card", "CardDecoy.obj");
	ModelManager::GetInstance()->LoadModel("CardAtkDown", "resources/card", "CardAtkDown.obj");
	ModelManager::GetInstance()->LoadModel("CardClaw", "resources/card", "CardClaw.obj");
	ModelManager::GetInstance()->LoadModel("CardScanner", "resources/card", "MapOpen.obj");

	// CSVからカードデータベースを初期化
	InitializeTitleCardRain();

}

void TitleScene::Update() {
	// デバッグカメラ更新
	if (debugCamera_) {
		debugCamera_->Update(camera_.get());
	}

	titleBgCameraPos_ = { 49.0f, 16.0f, 49.1f };
	titleBgCameraRot_ = { 1.57f, -1.57f, 0.0f };

	camera_->SetTranslation(titleBgCameraPos_);
	camera_->SetRotation(titleBgCameraRot_);
	camera_->SetFovY(1.0f);
	camera_->SetFarClip(200.0f);
	camera_->Update();



	auto *light = Obj3dCommon::GetInstance()->GetLightData();
	/*if (light) {
		light->direction = Normalize({ 0.038f, -0.997f, -0.074f });
		light->color = { 1.0f, 1.0f, 1.0f, 1.0f };
		light->intensity = 0.43f;
	}*/


	// マップエディタ更新
	if (mapManager_) {
		mapManager_->Update(titleBgPlayerPos_);
	}

	// タイトル背景のカード雨更新
	UpdateTitleCardRain();

	if (titleLogoSprite_) {
		const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
		const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
		const float logoWidth = screenW * 0.78f;
		const float logoHeight = logoWidth * (1280.0f / 1920.0f);
		titleLogoSprite_->SetPosition({ screenW * 0.5f, screenH * 0.32f });
		titleLogoSprite_->SetSize({ logoWidth, logoHeight });
		titleLogoSprite_->Update();
	}

	Input *input = Input::GetInstance();
	const bool canAcceptMenuInput = !SceneManager::GetInstance()->IsFading();
	if (HasControllerPromptInput(input)) {
		isControllerUiMode_ = true;
	} else if (HasKeyboardPromptInput(input)) {
		isControllerUiMode_ = false;
	}



	// BGM再生はBキーに分ける
	// ==========================================
	// W/Sキー または 上下矢印キーで選択
	// ==========================================
	// 左スティックの倒し込みを1回入力として扱うための前フレーム保持
	static bool wasStickUp = false;
	static bool wasStickDown = false;

	// 今フレームで上/下に倒れているかを判定する
	bool isStickUp = input->GetLeftStickY() > 0.5f;
	bool isStickDown = input->GetLeftStickY() < -0.5f;
	const TitleChoice previousSelection = currentSelection_;

	if (!isCreditOpen_) {
		auto moveSelection = [this](int direction) {
			int current = static_cast<int>(currentSelection_);
			const int count = static_cast<int>(TitleChoice::Count);
			current = (current + direction + count) % count;
			currentSelection_ = static_cast<TitleChoice>(current);
			};

		// 上入力は W / ↑ / 上D-Pad / 左スティック上
		if (canAcceptMenuInput && (
			input->Triggerkey(DIK_W) ||
			input->Triggerkey(DIK_UP) ||
			input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_UP) ||
			(isStickUp && !wasStickUp)
			)) {
			moveSelection(-1);
		}

		// 下入力は S / ↓ / 下D-Pad / 左スティック下
		if (canAcceptMenuInput && (
			input->Triggerkey(DIK_S) ||
			input->Triggerkey(DIK_DOWN) ||
			input->TriggerJoystickButton(XINPUT_GAMEPAD_DPAD_DOWN) ||
			(isStickDown && !wasStickDown)
			)) {
			moveSelection(1);
		}
		if (currentSelection_ != previousSelection) {
			GameSE::CursorMove();
		}


		// 次フレーム用に保存する
		wasStickUp = isStickUp;
		wasStickDown = isStickDown;

		if (isCreditOpen_ &&
			(input->Triggerkey(DIK_ESCAPE) || input->TriggerJoystickButton(XINPUT_GAMEPAD_B))) {
			isCreditOpen_ = false;
		}

		// ==========================================
		// スプライトのサイズ変更と更新
		// ==========================================
		const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
		const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());

		// 元のキャンバスの縦横比（1280 / 1920）
		float baseRatio = 1280.0f / 1920.0f;

		// キャンバス全体を画面の約80%の大きさで描画する（Title.pngの0.78fに合わせる）
		float normalW = screenW * 0.78f;

		Vector2 normalSize = { normalW, normalW * baseRatio };

		// 置きたい高さ（画面割合）
		float centerX = screenW * 0.5f;
		float centerY = screenH * 0.5f;
		const float selectedScale = 1.09f;
		const float menuLineGap = screenH * 0.095f;
		const float playY = centerY + screenH * 0.03f;
		const float tutorialY = playY + menuLineGap;
		const float creditY = tutorialY + menuLineGap;
		const float operateX = centerX + screenW * 0.13f;
		const float operateY = creditY + screenH * 0.10f;
		const Vector2 creditScreenSize = { screenW * 0.95f, screenW * 0.95f * baseRatio };
		const Vector2 playSize = { screenW * 0.19f, screenW * 0.19f * (195.0f / 470.0f) };
		const Vector2 tutorialSize = { screenW * 0.37f, screenW * 0.37f * (185.0f / 910.0f) };
		const Vector2 creditSize = { screenW * 0.28f, screenW * 0.28f * (190.0f / 685.0f) };
		const Vector2 playActiveSize = { playSize.x * selectedScale, playSize.y * selectedScale };
		const Vector2 tutorialActiveSize = { tutorialSize.x * selectedScale, tutorialSize.y * selectedScale };
		const Vector2 creditActiveSize = { creditSize.x * selectedScale, creditSize.y * selectedScale };

		// --- プレイボタンの更新 ---
		if (playBlueSprite_ && playWhiteSprite_) {
			if (currentSelection_ == TitleChoice::StartGame) {
				playBlueSprite_->SetSize(playActiveSize);
				playBlueSprite_->SetPosition({ centerX, playY });
				playBlueSprite_->Update();
			} else {
				playWhiteSprite_->SetSize(playSize);
				playWhiteSprite_->SetPosition({ centerX, playY });
				playWhiteSprite_->Update();
			}
		}

		// --- チュートリアルボタンの更新 ---
		if (tutorialBlueSprite_ && tutorialWhiteSprite_) {
			if (currentSelection_ == TitleChoice::Tutorial) {
				tutorialBlueSprite_->SetSize(tutorialActiveSize);
				tutorialBlueSprite_->SetPosition({ centerX, tutorialY });
				tutorialBlueSprite_->Update();
			} else {
				tutorialWhiteSprite_->SetSize(tutorialSize);
				tutorialWhiteSprite_->SetPosition({ centerX, tutorialY });
				tutorialWhiteSprite_->Update();
			}
		}

		// 操作説明の更新
		if (creditBlueSprite_ && creditWhiteSprite_) {
			if (currentSelection_ == TitleChoice::Credit) {
				creditBlueSprite_->SetSize(creditActiveSize);
				creditBlueSprite_->SetPosition({ centerX, creditY });
				creditBlueSprite_->Update();
			} else {
				creditWhiteSprite_->SetSize(creditSize);
				creditWhiteSprite_->SetPosition({ centerX, creditY });
				creditWhiteSprite_->Update();
			}
		}

		if (creditDimSprite_) {
			creditDimSprite_->SetPosition({ centerX, centerY });
			creditDimSprite_->SetSize({ screenW, screenH });
			creditDimSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.65f });
			creditDimSprite_->Update();
		}
		if (creditSprite_) {
			creditSprite_->SetPosition({ centerX, centerY });
			creditSprite_->SetSize(creditScreenSize);
			creditSprite_->Update();
		}
		if (creditUiSprite_) {
			creditUiSprite_->SetPosition({ centerX, centerY });
			creditUiSprite_->SetSize(creditScreenSize);
			creditUiSprite_->Update();
		}

		if (operateSprite_) {
			// 非選択状態と同じ「通常サイズ（normalSize）」で中央に置く
			operateSprite_->SetSize(normalSize);
			operateSprite_->SetPosition({ centerX, centerY });
			operateSprite_->Update();
		}
		if (controllerOperateSprite_) {
			controllerOperateSprite_->SetSize(normalSize);
			controllerOperateSprite_->SetPosition({ centerX, centerY });
			controllerOperateSprite_->Update();
		}

		// SPACEでゲーム開始
		// SPACE に加えて A ボタンでも決定できるようにする
		if (!hasConfirmedSelection_ &&
			!isCreditOpen_ &&
			(input->Triggerkey(DIK_SPACE) || input->TriggerJoystickButton(XINPUT_GAMEPAD_A))) {
			if (currentSelection_ != TitleChoice::Credit) {
				hasConfirmedSelection_ = true;
			}
			GameSE::Confirm();
			if (currentSelection_ == TitleChoice::StartGame) {
				// 通常プレイ
				GamePlayScene::RequestTutorialStart(false);
				SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
			} else if (currentSelection_ == TitleChoice::Tutorial) {
				// チュートリアル
				GamePlayScene::RequestTutorialStart(true);
				SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
			} else if (currentSelection_ == TitleChoice::Credit) {
				isCreditOpen_ = true;
			}
			return;
		}


		// パーティクル更新
		ParticleManager::GetInstance()->Update(camera_.get());

		// 3Dオブジェクト更新
		for (auto &obj : object3ds_) {
			if (obj) {
				obj->Update();
			}
		}
	}
}

void TitleScene::DrawDebugUI() {
#ifdef USE_IMGUI
	Obj3dCommon::GetInstance()->DrawDebugUI();

	if (camera_) {
		camera_->DrawDebugUI();
	}
	if (debugCamera_) {
		debugCamera_->DrawDebugUI();
	}

	ParticleManager::GetInstance()->DrawDebugUI();

	if (mapManager_) {
		mapManager_->DrawDebugUI();
	}



	ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiCond_FirstUseEver);
	ImGui::Begin("TitleScene Debug");
	ImGui::Text("Enter : Start Game");
	ImGui::End();
#endif
}

void TitleScene::Draw(){
	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList();

	// 1. MRT開始（色とマスク用キャンバス）
	PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

	// --- ここから3Dやパーティクルの描画 ---
	Obj3dCommon::GetInstance()->PreDraw(commandList);
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

	for ( auto& obj : object3ds_ ) {
		if ( obj ) {
			obj->Draw();
		}
	}

	if (mapManager_) {
		mapManager_->Draw(titleBgPlayerPos_);
	}

	Obj3dCommon::GetInstance()->PreDraw(commandList);
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

	for (auto& card : titleRainCards_) {
		if (IsTitleRainCardFrontFacing(card)) {
			if (card.frontObj) {
				card.frontObj->Draw();
			}
		} else {
			if (card.backObj) {
				card.backObj->Draw();
			}
		}
	}
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);
	// --- 描画ここまで ---

	// 2. MRT終了
	PostEffect::GetInstance()->PostDrawSceneMRT(commandList);

	// 3. ポストエフェクト適用
	PostEffect::GetInstance()->Draw(commandList);

	// 4. Bloomパス
	uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
	uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();
	Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
	uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

	// 5. バックバッファへ最終出力
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
	commandList->DrawInstanced(3, 1, 0, 0);

	if (titleLogoSprite_) {
		titleLogoSprite_->Draw();
	}

	// 選択状況に応じて描画するスプライトを切り替える
	if (!isCreditOpen_ && currentSelection_ == TitleChoice::StartGame) {
		// プレイを選択中
		if (playBlueSprite_) playBlueSprite_->Draw();
		if (tutorialWhiteSprite_) tutorialWhiteSprite_->Draw();
		if (creditWhiteSprite_) creditWhiteSprite_->Draw();
	} else if (!isCreditOpen_ && currentSelection_ == TitleChoice::Tutorial) {
		// チュートリアルを選択中
		if (playWhiteSprite_) playWhiteSprite_->Draw();
		if (tutorialBlueSprite_) tutorialBlueSprite_->Draw();
		if (creditWhiteSprite_) creditWhiteSprite_->Draw();
	} else if (!isCreditOpen_ && currentSelection_ == TitleChoice::Credit) {
		if (playWhiteSprite_) playWhiteSprite_->Draw();
		if (tutorialWhiteSprite_) tutorialWhiteSprite_->Draw();
		if (creditBlueSprite_) creditBlueSprite_->Draw();
	}

	// 操作説明の描画
	Sprite* activeOperateSprite = isControllerUiMode_ ? controllerOperateSprite_.get() : operateSprite_.get();
	if (!isCreditOpen_ && activeOperateSprite) {
		activeOperateSprite->Draw();
	}

	if (isCreditOpen_) {
		if (creditDimSprite_) creditDimSprite_->Draw();
		if (creditSprite_) creditSprite_->Draw();
		if (creditUiSprite_) creditUiSprite_->Draw();
	}

	// エディタ用の出力設定
	EditorManager::GetInstance()->SetGameViewSrvIndex(finalSrv);
}

void TitleScene::Finalize() {
	object3ds_.clear();
	titleRainCards_.clear();
	titleLogoSprite_.reset();
	playBlueSprite_.reset();
	playWhiteSprite_.reset();
	tutorialBlueSprite_.reset();
	tutorialWhiteSprite_.reset();
	creditBlueSprite_.reset();
	creditWhiteSprite_.reset();
	operateSprite_.reset();
	controllerOperateSprite_.reset();
	creditDimSprite_.reset();
	creditSprite_.reset();
	creditUiSprite_.reset();
	mapManager_.reset();
	debugCamera_.reset();
	camera_.reset();

	// タイトル用テキストを消す
	TextManager::GetInstance()->SetText("SceneMessage", "");
	TextManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}

// minValue以上maxValue未満の範囲でランダムなfloatを生成する関数
float RandomRange(float minValue, float maxValue) {
	float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	return minValue + (maxValue - minValue) * t;
}

// タイトル背景のカード雨の初期化
void TitleScene::InitializeTitleCardRain() {
	titleRainCards_.clear();
	titleRainCards_.resize(titleRainCardCount_);

	for (int i = 0; i < titleRainCardCount_; ++i) {
		ResetTitleRainCard(titleRainCards_[i], false);

		float t = 0.0f;
		if (titleRainCardCount_ > 1) {
			t = static_cast<float>(i) / static_cast<float>(titleRainCardCount_ - 1);
		}

		titleRainCards_[i].position.x =
			titleRainSpawnX_ + (titleRainResetX_ - titleRainSpawnX_) * t;

		titleRainCards_[i].position.z =
			RandomRange(titleRainSpawnMinZ_, titleRainSpawnMaxZ_);

		if (titleRainCards_[i].frontObj) {
			titleRainCards_[i].frontObj->SetTranslation(titleRainCards_[i].position);
			titleRainCards_[i].frontObj->Update();
		}
		if (titleRainCards_[i].backObj) {
			titleRainCards_[i].backObj->SetTranslation(titleRainCards_[i].position);
			titleRainCards_[i].backObj->Update();
		}
	}
}


// タイトル背景のカード雨のリセット（位置や回転をランダムに再設定）
void TitleScene::ResetTitleRainCard(TitleRainCard& card, bool randomY) {
	if (titleRainCardModelNames_.empty()) {
		return;
	}

	int modelIndex = rand() % static_cast<int>(titleRainCardModelNames_.size());
	const std::string& modelName = titleRainCardModelNames_[modelIndex];

	card.frontObj = Obj3d::Create(modelName);
	card.backObj = Obj3d::Create("cardR");

	card.position = {
	titleRainSpawnX_ + RandomRange(-2.0f, 2.0f),
	titleRainY_,
	RandomRange(titleRainSpawnMinZ_, titleRainSpawnMaxZ_)
	};

	card.rotation = {
		RandomRange(0.0f, 6.28f),
		RandomRange(0.0f, 6.28f),
		RandomRange(0.0f, 6.28f)
	};

	card.rotationSpeed = {
		RandomRange(-0.03f, 0.03f),
		RandomRange(-0.05f, 0.05f),
		RandomRange(-0.04f, 0.04f)
	};

	card.fallSpeed = RandomRange(0.04f, 0.04f);

	if (card.frontObj) {
		card.frontObj->SetCamera(camera_.get());
		card.frontObj->SetTranslation(card.position);
		card.frontObj->SetRotation(card.rotation);
		card.frontObj->SetScale({ titleRainScale_, titleRainScale_, titleRainScale_ });
		card.frontObj->Update();
	}
	if (card.backObj) {
		card.backObj->SetCamera(camera_.get());
		card.backObj->SetTranslation(card.position);
		card.backObj->SetRotation(card.rotation);
		card.backObj->SetScale({ titleRainScale_, titleRainScale_, titleRainScale_ });
		card.backObj->Update();
	}
}

// タイトル背景のカード雨の更新（位置を落下させ、回転させる）
void TitleScene::UpdateTitleCardRain() {
	for (auto& card : titleRainCards_) {
		if (!card.frontObj || !card.backObj) {
			ResetTitleRainCard(card, false);
			continue;
		}

		card.position.x += card.fallSpeed;

		card.rotation.x += card.rotationSpeed.x;
		card.rotation.y += card.rotationSpeed.y;
		card.rotation.z += card.rotationSpeed.z;

		if (card.position.x > titleRainResetX_) {
			ResetTitleRainCard(card, false);
			continue;
		}

		card.frontObj->SetTranslation(card.position);
		card.frontObj->SetRotation(card.rotation);
		card.frontObj->SetScale({ titleRainScale_, titleRainScale_, titleRainScale_ });
		card.frontObj->Update();

		card.backObj->SetTranslation(card.position);
		card.backObj->SetRotation(card.rotation);
		card.backObj->SetScale({ titleRainScale_, titleRainScale_, titleRainScale_ });
		card.backObj->Update();
	}
}

bool TitleScene::IsTitleRainCardFrontFacing(const TitleRainCard& card) const {
	if (!camera_) {
		return true;
	}

	Matrix4x4 rotateX = MakeRotateX(card.rotation.x);
	Matrix4x4 rotateY = MakeRotateY(card.rotation.y);
	Matrix4x4 rotateZ = MakeRotateZ(card.rotation.z);
	Matrix4x4 rotate = Multiply(Multiply(rotateX, rotateY), rotateZ);

	Vector3 frontNormal = Transforms({ 0.0f, 0.0f, -1.0f }, rotate);
	Vector3 toCamera = Normalize(camera_->GetTranslation() - card.position);

	return Dot(frontNormal, toCamera) > 0.0f;
}
