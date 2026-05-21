#include "GamePlayScene.h"
// --- ゲーム固有のファイル ---
#include "TitleScene.h"

// --- エンジン側のファイル ---
#include "Engine/Math/Matrix4x4.h"
#include "Engine/Utils/ImGuiManager.h"
#include "Engine/Utils/Color.h"
#include "externals/imgui/imgui.h"
#include "Engine/Audio/AudioManager.h"
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
#include "Engine/2D/SpriteCommon.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "engine/graphics/RenderTexture.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include"engine/utils/Level/LevelEditor.h"
#include "engine/utils/TextManager.h"
#include "game/player/Player.h"
#include "game/player/PlayerManager.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/BossManager.h"
#include "game/card/CardUseSystem.h"
#include "game/map/Minimap.h"
#include "game/map/MapManager.h"
#include "Bloom.h"
#include "engine/3d/model/Model.h"
#include "engine/utils/EditorManager.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/particle/GPUParticleEmitter.h"

#include <array>
#include <cmath>


using namespace VectorMath;
using namespace MatrixMath;

// 初期化
void GamePlayScene::Initialize() {
	DirectXCommon* dxCommon = DirectXCommon::GetInstance();
	WindowProc* windowProc = WindowProc::GetInstance();

	// コマンドリスト取得
	auto commandList = dxCommon->GetCommandList();

	// BGMロード (シングルトン)
	AudioManager::GetInstance()->LoadWave(bgmFile_);
	// モデル読み込み (シングルトン)
	ModelManager::GetInstance()->LoadModel("fence", "resources", "fence.obj");

	ModelManager::GetInstance()->LoadModel("grass", "resources", "terrain.obj");
	ModelManager::GetInstance()->LoadModel("block", "resources/block", "block.obj");
	ModelManager::GetInstance()->LoadModel("wall", "resources/wall", "wall.obj");
	ModelManager::GetInstance()->LoadModel("stairs", "resources/stairs", "stairs.obj");

	// プレイヤーモデル読み込み
	ModelManager::GetInstance()->LoadModel("player", "resources/player", "player.gltf");
	ModelManager::GetInstance()->LoadModel("playerDecoy", "resources/player", "player.gltf");

	// 敵モデル読み込み
	ModelManager::GetInstance()->LoadModel("enemy", "resources/enemy", "enemy.obj");
	ModelManager::GetInstance()->LoadModel("normalEnemy", "resources/enemy", "normalEnemy.gltf");
	ModelManager::GetInstance()->LoadModel("wallEnemy", "resources/enemy", "wallEnemy.obj");
	ModelManager::GetInstance()->LoadModel("cornerEnemy", "resources/enemy", "cornerEnemy.obj");

	// ボスモデル読み込み
	ModelManager::GetInstance()->LoadModel("boss", "resources/boss", "boss.obj");

	// 球モデル作成 (シングルトン) カード用テクスチャモデル
	ModelManager::GetInstance()->CreateSphereModel("sphere", 16);
	ModelManager::GetInstance()->CreateRingModel("cardRing", 64);

	ModelManager::GetInstance()->CreateSphereModel("shield_sphere", 16);
	ModelManager::GetInstance()->CreateSphereModel("fireball_sphere", 8);

	ModelManager::GetInstance()->CreateSphereModel("fang_sphere", 16);

	// パーティクルグループ作成 (シングルトン)
	//ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み
	textures_["uvChecker"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/uvChecker.png", commandList);
	textures_["monsterBall"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/monsterBall.png", commandList);
	textures_["fence"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/fence.png", commandList);
	textures_["circle"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle.png", commandList);
	textures_["noise0"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise0.png", commandList) };
	textures_["noise1"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise1.png", commandList) };

	// 交換フェイズ用の暗転スプライト作成
	textures_["white"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/block/white1x1.png", commandList);
	swapDarkOverlay_ = Sprite::Create(textures_["white"].srvIndex, { 0.0f, 0.0f });
	if (swapDarkOverlay_) {
		swapDarkOverlay_->SetColor({ 0.0f, 0.0f, 0.0f, 0.7f }); // 黒色で透明度70%
	}
	textures_["ChangeUI"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/UI/ChangeUI.png", commandList);
	swapUiSprite_ = Sprite::Create(textures_["ChangeUI"].srvIndex, { 0.0f, 0.0f });

	// カード用の3Dモデルを読み込んでおく（※パスやファイル名はご自身の環境に合わせてください）
	ModelManager::GetInstance()->LoadModel("ground", "resources/Ground", "Ground.obj");
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
	ModelManager::GetInstance()->LoadModel("CardCostBoost", "resources/card", "CardCostBoost.obj");
	ModelManager::GetInstance()->LoadModel("CardSpear", "resources/card", "spearCard.obj");
	ModelManager::GetInstance()->LoadModel("CardSword", "resources/card", "swordCard.obj");
	ModelManager::GetInstance()->LoadModel("CardHammer", "resources/card", "hammerCard.obj");
	ModelManager::GetInstance()->LoadModel("CardKick", "resources/card", "kickCard.obj");


	// 追加のカード用棘モデル
	ModelManager::GetInstance()->LoadModel("Fang", "resources/Fang", "Fang.obj");
	ModelManager::GetInstance()->LoadModel("claw_model", "resources/claw", "claw.obj");
	ModelManager::GetInstance()->LoadModel("Fist_model", "resources/Fist", "Fist.obj");
	ModelManager::GetInstance()->LoadModel("sword_model", "resources/sword", "sword.obj");
	ModelManager::GetInstance()->LoadModel("hammer_model", "resources/hammer", "hammer.obj");
	ModelManager::GetInstance()->LoadModel("spear_model", "resources/spear", "ransu.obj");
	ModelManager::GetInstance()->LoadModel("kick_model", "resources/kick", "kick.obj");

	// CSVからカードデータベースを初期化
	CardDatabase::Initialize("resources/card/CardData.csv");

	CardDatabase::LoadAdditionalCards("resources/card/BossCardData.csv");

	// モデル読み込み (シングルトン)
	// アニメーション
	ModelManager::GetInstance()->LoadModel("animatedCube", "resources/AnimatedCube", "AnimatedCube.gltf");
	testAnimation_ = LoadAnimationFromFile("resources/AnimatedCube", "AnimatedCube.gltf");

	// カメラ生成
	camera_ = std::make_unique<Camera>(windowProc->GetClientWidth(), windowProc->GetClientHeight(), dxCommon);
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });

	//UI専用カメラの初期化
	uiCamera_ = std::make_unique<Camera>(
		windowProc->GetClientWidth(),
		windowProc->GetClientHeight(),
		dxCommon
	);

	// デバッグカメラ生成
	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();

	// 敵
	enemyManager_ = std::make_unique<EnemyManager>();
	enemyManager_->Initialize();

	// プレイヤー管理クラスを作成
	playerManager_ = std::make_unique<PlayerManager>();
	playerManager_->Initialize(camera_.get());

	// 他の処理でも使うので位置とスケールを同期
	if (playerManager_) {
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}

	bossManager_ = std::make_unique<BossManager>();
	bossManager_->Initialize(camera_.get());

	// カード使用システム初期化
	playerCardSystem_ = std::make_unique<CardUseSystem>();
	playerCardSystem_->Initialize(camera_.get());

	// ファイル名を指定するだけで、読み込み・生成・配置
	// 引数: (ファイルパス, 座標)
	sprite_ = Sprite::Create(textures_["uvChecker"].srvIndex, spritePos_);
	// プレイヤーオブジェクト生成



	// デプスステンシル作成 (TextureManagerシングルトン)
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(), windowProc->GetClientHeight()
	);

	mapManager_ = std::make_unique<MapManager>();
	mapManager_->SetCamera(camera_.get());
	mapManager_->Initialize();
	mapManager_->SetCurrentFloor(1);
	mapManager_->SetNoiseTexture(textures_["noise0"].srvIndex);




	

	// 手札マネージャーの初期化
	handManager_.Initialize(uiCamera_.get(), textures_["noise0"].srvIndex);

	//最初から手札にID１を追加する
	handManager_.AddCard(CardDatabase::GetCardData(1));

	// これだけでOK
	RegenerateDungeonAndRespawnPlayer(8);

	minimap_ = std::make_unique<Minimap>();
	minimap_->Initialize();
	minimap_->SetLevelData(&mapManager_->GetLevelData());
	EditorManager::GetInstance()->SetCamera(camera_.get());

	// 初期ロード時のマップ変更通知を消す
	if (mapManager_) {
		mapManager_->ConsumeMapChanged();
	}

	
	// ★追加：カードシステムにミニマップを教える
	if (playerCardSystem_ && minimap_) {
		playerCardSystem_->SetMinimap(minimap_.get());
	}

	TextManager::GetInstance()->Initialize();

	TextManager::GetInstance()->SetPosition("PlayerHP", 40, 560);
	TextManager::GetInstance()->SetPosition("PlayerCost", 40, 600);
	TextManager::GetInstance()->SetPosition("PlayerLevel", 40, 640);
	TextManager::GetInstance()->SetPosition("PlayerEXP", 40, 680);
	TextManager::GetInstance()->SetPosition("Floor", 40, 250);
	TextManager::GetInstance()->SetScale("Floor", 1.2f);

	// 画面サイズ取得
	float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
	TextManager::GetInstance()->SetPosition("FloorTransition", screenW * 0.5f, screenH * 0.5f);
	TextManager::GetInstance()->SetScale("FloorTransition", 2.0f);
	TextManager::GetInstance()->SetCentered("FloorTransition", true);
	TextManager::GetInstance()->SetColor("FloorTransition", 1.0f, 1.0f, 1.0f, 1.0f);
	TextManager::GetInstance()->SetText("FloorTransition", "");

	// 中央に配置（少し上に出すなら -50 くらい）
	TextManager::GetInstance()->SetPosition("CostLack", screenW * 0.5f - 100.0f, screenH * 0.5f - 50.0f);
	TextManager::GetInstance()->SetScale("CostLack", 1.6f);
	TextManager::GetInstance()->SetColor("CostLack", 1.0f, 0.2f, 0.2f, 1.0f);

	// 左下のステータス背景
	playerStatusBgSprite_ = Sprite::Create("resources/white1x1.png", { 170.0f, 625.0f });
	playerStatusBgSprite_->SetSize({ 340.0f, 190.0f });
	playerStatusBgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.65f });
	playerHpGaugeFrameSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	playerHpGaugeBackSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	playerHpGaugeFillSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	playerCostGaugeFrameSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	playerCostGaugeBackSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	playerCostGaugeFillSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
	if (playerHpGaugeFrameSprite_) {
		playerHpGaugeFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.28f });
	}
	if (playerHpGaugeBackSprite_) {
		playerHpGaugeBackSprite_->SetColor({ 0.08f, 0.03f, 0.03f, 0.85f });
	}
	if (playerHpGaugeFillSprite_) {
		playerHpGaugeFillSprite_->SetAnchorPoint({ 0.0f, 0.5f });
		playerHpGaugeFillSprite_->SetColor({ 0.95f, 0.12f, 0.13f, 0.95f });
	}
	if (playerCostGaugeFrameSprite_) {
		playerCostGaugeFrameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.28f });
	}
	if (playerCostGaugeBackSprite_) {
		playerCostGaugeBackSprite_->SetColor({ 0.03f, 0.05f, 0.09f, 0.85f });
	}
	if (playerCostGaugeFillSprite_) {
		playerCostGaugeFillSprite_->SetAnchorPoint({ 0.0f, 0.5f });
		playerCostGaugeFillSprite_->SetColor({ 0.18f, 0.68f, 1.0f, 0.95f });
	}

	// スプライト作成（座標 X:100, Y:500）
	descBgSprite_ = Sprite::Create("resources/white1x1.png", { 100.0f, 500.0f });

	// 大きさを幅600, 高さ100の長方形にする
	descBgSprite_->SetSize({ 600.0f, 100.0f });

	// 色を半透明の黒にする（Vector4 で R, G, B, A）
	descBgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.75f });// 画面全体を覆うフェード用スプライトの作成 (座標0,0)
	
	  // GPUパーティクル初期化 (テクスチャを指定する)
	GPUParticleManager::GetInstance()->Initialize(
		dxCommon, SrvManager::GetInstance(), "resources/circle.png");

	fadeSprite_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, screenH * 0.5f });
	// 画面サイズに合わせる (ウィンドウサイズに合わせて変更してください)
	fadeSprite_->SetSize({ screenW, screenH });
	// 初期状態は透明の黒
	fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f });

	// ポーズ画面用の文字位置
	TextManager::GetInstance()->SetPosition("PauseTitle", screenW * 0.5f, screenH * 0.5f - 140.0f);
	TextManager::GetInstance()->SetPosition("PauseResume", screenW * 0.5f, screenH * 0.5f - 40.0f);
	TextManager::GetInstance()->SetPosition("PauseToTitle", screenW * 0.5f, screenH * 0.5f);
	TextManager::GetInstance()->SetCentered("PauseTitle", true);
	TextManager::GetInstance()->SetCentered("PauseResume", true);
	TextManager::GetInstance()->SetCentered("PauseToTitle", true);


	// ポーズ中の半透明背景
	pauseBgSprite_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, screenH * 0.5f });
	pauseBgSprite_->SetSize({ screenW, screenH });
	pauseBgSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.5f });
	pauseBgSprite_->Update();

	bossIntroTopBar_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, 0.0f });
	bossIntroBottomBar_ = Sprite::Create("resources/white1x1.png", { screenW * 0.5f, screenH });
	if (bossIntroTopBar_) {
		bossIntroTopBar_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	}
	if (bossIntroBottomBar_) {
		bossIntroBottomBar_->SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });
	}

	levelUpBonusManager_.Initialize();
	// エミッターの初期設定
	//GPUParticleEmitterData emitterData;
	//emitterData.position = { 0.0f, 0.0f, 0.0f };
	//emitterData.emitRate = 20.0f;
	//emitter_.SetData(emitterData);

	// エディタにエミッターを渡す（F1で開くエディタで操作できるようになる）
	EditorManager::GetInstance()->SetParticleEmitter(&emitter_);

	// チュートリアルの初期化
	tutorial_ = std::make_unique<Tutorial>();
	tutorial_->Initialize({
		.mapManager = mapManager_.get(),
		.playerManager = playerManager_.get(),
		.enemyManager = enemyManager_.get(),
		.cardPickupManager = &cardPickupManager_,
		.camera = camera_.get(),
		.minimap = minimap_.get()
		});

	if (ConsumeTutorialStartRequest()) {
		tutorial_->Start();
		isFloorTransitionTextVisible_ = false;
		TextManager::GetInstance()->SetText("FloorTransition", "");
	}
	else {
		transitionState_ = TransitionState::BlackHold;
		fadeAlpha_ = 1.0f;
		floorTransitionHoldTimer_ = 45;
		shouldAdvanceFloorOnBlack_ = false;
		isFloorTransitionTextVisible_ = true;
		floorTransitionDisplayFloor_ = mapManager_ ? mapManager_->GetCurrentFloor() : 1;
		TextManager::GetInstance()->SetText(
			"FloorTransition",
			"FLOOR " + std::to_string(floorTransitionDisplayFloor_)
		);
	}


}

void GamePlayScene::Update() {

	// デバッグカメラ更新
	if (debugCamera_) {
		debugCamera_->Update(camera_.get());
	}

	Input* input = Input::GetInstance();
	bool isEditingDebugText = false;

#ifdef USE_IMGUI
	isEditingDebugText = ImGui::GetIO().WantTextInput;
#endif

	// ==========================================
	// ★ ここに追加：さっき下で消した「F1の判定」をここに引っ越し！
	// これでレベルアップ中だろうが何だろうが、絶対にF1キーが効くようになります
	// ==========================================
#ifdef _DEBUG
	if (input->Triggerkey(DIK_F1)) {
		isInfiniteMode_ = !isInfiniteMode_;
	}
#else
	isInfiniteMode_ = false;
#endif

#ifdef USE_IMGUI
	if (!isEditingDebugText && input->Triggerkey(DIK_F2)) {
		showCharacterHitboxes_ = !showCharacterHitboxes_;
	}
#endif

	if (playerManager_) {
		playerManager_->ApplyInfiniteMode(isInfiniteMode_);
	}
	// プレイヤー本体を取得
	Player* player = playerManager_ ? playerManager_->GetPlayer() : nullptr;

	// BossManagerから必要なものだけ取る
	Boss* boss = bossManager_ ? bossManager_->GetBoss() : nullptr;
	Sprite* bossHpBackSprite = bossManager_ ? bossManager_->GetBossHpBackSprite() : nullptr;
	Sprite* bossHpFillSprite = bossManager_ ? bossManager_->GetBossHpFillSprite() : nullptr;

	UpdateBossIntroLetterbox();

	// ポーズ切り替え
	if (!isEditingDebugText && input->Triggerkey(DIK_ESCAPE)) {
		isPaused_ = !isPaused_;
		pauseSelection_ = 0; // 開くたびに先頭へ戻す
	}

	// ポーズ中は専用更新だけして止める
	if (isPaused_) {
		UpdatePause(input);
		return;
	}

	// ==========================================
// FadeOut中だけゲーム更新を止める
// 真っ黒になったら階層切り替えして、FadeInから更新再開
// ==========================================
	if (transitionState_ == TransitionState::FadeOut) {
		fadeAlpha_ += kFadeSpeed;

		if (fadeAlpha_ >= 1.0f) {
			fadeAlpha_ = 1.0f;

			if (shouldAdvanceFloorOnBlack_) {
				// 真っ黒になった瞬間に階層切り替え
				mapManager_->AdvanceFloor(
					enemyManager_.get(),
					bossManager_.get(),
					minimap_.get(),
					[this]() { ResetBattleDebug(); }
				);
				shouldAdvanceFloorOnBlack_ = false;
			}

			TextManager::GetInstance()->SetText(
				"FloorTransition",
				"FLOOR " + std::to_string(floorTransitionDisplayFloor_)
			);
			isFloorTransitionTextVisible_ = !(tutorial_ && tutorial_->IsActive());
			floorTransitionHoldTimer_ = 45;
			transitionState_ = TransitionState::BlackHold;
		}

	}
	else if (transitionState_ == TransitionState::BlackHold) {
		fadeAlpha_ = 1.0f;
		if (floorTransitionHoldTimer_ > 0) {
			floorTransitionHoldTimer_--;
		}
		if (floorTransitionHoldTimer_ <= 0) {
			isFloorTransitionTextVisible_ = false;
			TextManager::GetInstance()->SetText("FloorTransition", "");
			transitionState_ = TransitionState::FadeIn;
		}
	}
	else if (transitionState_ == TransitionState::FadeIn) {
		fadeAlpha_ -= kFadeSpeed; // 画面を明るくしていく

		if (fadeAlpha_ <= 0.0f) {
			fadeAlpha_ = 0.0f;
			transitionState_ = TransitionState::None;
			isFloorTransitionTextVisible_ = false;
			TextManager::GetInstance()->SetText("FloorTransition", "");
		}
	}

	// フェードスプライトの更新（FadeOutでもFadeInでも絶対に実行する！）
	if (fadeSprite_) {
		fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, fadeAlpha_ });
		fadeSprite_->Update();
	}

	// ★ FadeOut中（真っ黒に向かっている最中）だけ、ゲームの進行を止める
	if (transitionState_ == TransitionState::FadeOut || transitionState_ == TransitionState::BlackHold) {
		return;
	}

	// マップ切り替えがあったら戦闘ごとリセット
	if (mapManager_ && mapManager_->ConsumeMapChanged()) {

		// 先にリセット
		ResetBattleDebug();

		// ボス部屋ならプレイヤーを入口側へずらす
		if (mapManager_->IsBossMap() && playerManager_) {
			Vector3 newPos = playerManager_->GetPosition(); // 今の正しい床上位置を使う

			// XZだけ調整
			newPos.z -= 30.0f;

			playerManager_->SetPosition(newPos);
			playerPos_ = newPos;
		}

		// イントロ開始
		if (bossManager_) {
			if (mapManager_->IsBossMap()) {
				bossManager_->StartBossIntro();
			} else {
				bossManager_->EndBossIntro();
			}
		}

		return;
	}

	if (isCardSwapMode_) {
		if (tutorial_ && tutorial_->IsActive()) {
			tutorial_->SetTextSuppressed(true);
		}

		// 追加：暗転背景を画面サイズに合わせる
		if (swapDarkOverlay_) {
			float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
			float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
			swapDarkOverlay_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
			swapDarkOverlay_->SetSize({ screenW, screenH });
			swapDarkOverlay_->Update();
		}

		if (swapUiSprite_) {
			float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
			float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());

			// 画面の中央に配置
			swapUiSprite_->SetPosition({ screenW * 0.5f, screenH * 0.5f });
			// 画面全体のサイズに引き伸ばす（ピッタリ合わせる）
			swapUiSprite_->SetSize({ screenW, screenH });

			swapUiSprite_->Update();
		}

		UpdateCardSwapMode(input);
		return;
	}

	if (tutorial_ && tutorial_->IsActive()) {
		tutorial_->SetTextSuppressed(false);
	}

	// ==========================================
	// ★ レベルアップの処理（フリーズしない正しい止め方）
	// ==========================================
	// ① Updateを呼ぶ「前」に、選択画面が開いているかを記憶しておく！
	bool wasSelecting = levelUpBonusManager_.IsSelecting();

	LevelUpResult levelUpResult = levelUpBonusManager_.Update(playerManager_.get(), &handManager_, input);
	if (levelUpResult.needCardSwap) {
		isCardSwapMode_ = true;
		pendingCard_ = levelUpResult.droppedCard;
		pendingPickup_ = nullptr;
		handManager_.AddPendingCard(pendingCard_);
	}

	// ② 今選択画面を開いている、または「このフレームで選択し終わったばかり」ならリターン！
	// （これでボタンの入力がここで完全に吸収されて、下へ貫通しません）
	if (wasSelecting) {
		return;
	}

	// 選択画面中なら、以降のゲーム処理（プレイヤーや敵の移動など）をストップ！
	if (levelUpBonusManager_.IsSelecting()) {
		return;
	}
#if USE_IMGUI


	// デバッグ用リセット
	if (!isEditingDebugText && input->Triggerkey(DIK_R)) {
		ResetBattleDebug();
	}

	// BGM再生
	if (!isEditingDebugText && input->Triggerkey(DIK_SPACE)) {
		//AudioManager::GetInstance()->PlayWave(bgmFile_);
	}

	// タイトルシーンへ移動
	if (!isEditingDebugText && input->Triggerkey(DIK_T)) {
		SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
	}

#endif // USE_IMGUI
	//// パーティクル発生
	//if (input->Triggerkey(DIK_P)) {
	//	ParticleManager::GetInstance()->Emit("Circle", { 0.0f, 0.0f, 0.0f }, 10);
	//}

	//// パーティクル更新
	//ParticleManager::GetInstance()->Update(camera_.get());

	// チュートリアルの更新と、ゴール判定
	if (tutorial_ && tutorial_->IsActive()) {
		// チュートリアル中はプレイヤーが死亡しないようにする
		if (player) {
			const bool tutorialActive = tutorial_ && tutorial_->IsActive();
			player->SetTutorialNoDeath(tutorialActive);
		}

		tutorial_->Update(input);
		tutorial_->CheckPlayerGoal(playerPos_);

		if (tutorial_->ConsumeAdvanceInputRequest()) {
			return;
		}

		if (tutorial_->ConsumeReturnToTitleRequest()) {
			SceneManager::GetInstance()->ChangeScene("TITLE");
			return;
		}

		if (tutorial_->IsGameplayPausedByTutorial()) {
			return;
		}
	}


	// ==========================================
	// プレイヤーの更新処理
	// ==========================================
	const bool isBossIntroPlayingNow = bossManager_ ? bossManager_->IsBossIntroPlaying() : false;
	if (playerManager_) {


		playerManager_->ApplyInfiniteMode(isInfiniteMode_);

		// 登場演出中は入力だけ止めて、見た目の同期は続ける
		if (isBossIntroPlayingNow && playerManager_->GetPlayer()) {
			playerManager_->GetPlayer()->LockAction(1);
		}

		// intro中もUpdateは通す
		playerManager_->Update(input, mapManager_.get(), debugCamera_.get(), bossManager_.get());

		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}

	// プレイヤーが死亡したらゲームオーバーへ遷移
	if (playerManager_ && playerManager_->IsDeathAnimationFinished()) {
		SceneManager::GetInstance()->ChangeScene("GAMEOVER");
		return;
	}




	// ==========================================
	// 階段タイル(3)との判定
	// ==========================================

	if (player && mapManager_) {
		const LevelData& level = mapManager_->GetLevelData();
		int gridX = static_cast<int>(std::round(playerPos_.x / level.tileSize));
		int gridZ = static_cast<int>(std::round(playerPos_.z / level.tileSize));

		if (gridX >= 0 && gridX < level.width && gridZ >= 0 && gridZ < level.height) {
			if (level.tiles[gridZ][gridX] == 3) {
				const bool tutorialActive = tutorial_ && tutorial_->IsActive();
				if (!tutorialActive && transitionState_ == TransitionState::None) {
					transitionState_ = TransitionState::FadeOut;
					fadeAlpha_ = 0.0f;
					isFloorTransitionTextVisible_ = false;
					shouldAdvanceFloorOnBlack_ = true;
					floorTransitionDisplayFloor_ = mapManager_->GetCurrentFloor() + 1;
					TextManager::GetInstance()->SetText("FloorTransition", "");
				}
			}
		}
	}


	// ==========================================
	// 雑魚敵の更新処理
	// ==========================================

	// ターゲットを決める！
	Vector3 targetPos = playerPos_; // 基本はプレイヤーの位置を狙う
	const bool isBossIntroPlaying = bossManager_ ? bossManager_->IsBossIntroPlaying() : false;
	if (playerCardSystem_ && playerCardSystem_->IsDecoyActive()) {
		targetPos = playerCardSystem_->GetDecoyPosition(); // 身代わりがいたら身代わりを狙う！
	}

	// EnemyManager に更新をお願いする
	if (enemyManager_ && !isBossIntroPlaying) {
		enemyManager_->Update(player, &cardPickupManager_, mapManager_.get(), boss, targetPos);
	}

	// ==========================================
	// ボスの更新処理
	// ==========================================

	// ボス関連の更新をまとめてBossManagerに任せる
	if (bossManager_) {


		bossManager_->Update(
			player,
			enemyManager_.get(),
			&cardPickupManager_,
			mapManager_.get(),
			camera_.get(),
			playerPos_,
			targetPos
		);
	}


	// ボスを倒していたらゲームクリアへ遷移
	if (bossManager_ && bossManager_->ShouldTriggerGameClear(mapManager_.get())) {
		// 10階ボス撃破後にゲームクリアへ遷移する
		SceneManager::GetInstance()->ChangeScene("GAMECLEAR");
		return;
	}

	// 敵とプレイヤーの当たり判定
	if (enemyManager_ && !isBossIntroPlaying) {
		enemyManager_->CheckCollisions(player, mapManager_.get());
	}

	// ==========================================
	// プレイヤーとボスの接触押し出し
	// ==========================================
	if (playerManager_ && bossManager_ && mapManager_ && !isBossIntroPlaying) {
		Vector3 playerPos = playerManager_->GetPosition();
		Vector3 oldPlayerPos = playerPos; // 押し出し前の位置を保存して壁補正に使う
		const LevelData& level = mapManager_->GetLevelData();

		// プレイヤーが壁に当たるかを調べる
		auto isPlayerHitWall = [&](const Vector3& checkPos) -> bool {
			AABB playerAABB;
			playerAABB.min = { checkPos.x - 0.5f, checkPos.y - 0.5f, checkPos.z - 0.5f };
			playerAABB.max = { checkPos.x + 0.5f, checkPos.y + 0.5f, checkPos.z + 0.5f };

			int centerGridX = static_cast<int>(std::round(checkPos.x / level.tileSize));
			int centerGridZ = static_cast<int>(std::round(checkPos.z / level.tileSize));

			int startX = std::max(0, centerGridX - 1);
			int endX = std::min(level.width - 1, centerGridX + 1);
			int startZ = std::max(0, centerGridZ - 1);
			int endZ = std::min(level.height - 1, centerGridZ + 1);

			for (int z = startZ; z <= endZ; z++) {
				for (int x = startX; x <= endX; x++) {
					if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
						continue;
					}

					float worldX = x * level.tileSize;
					float worldZ = z * level.tileSize;

					AABB blockAABB;
					blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
					blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

					if (Collision::IsCollision(playerAABB, blockAABB)) {
						return true;
					}
				}
			}

			return false;
			};

		// プレイヤーとボスの重なりを押し戻す
		auto resolveBossPush = [&](const Vector3& bossPos, float bossRadius) {
			Vector3 diff = {
				playerPos.x - bossPos.x,
				0.0f,
				playerPos.z - bossPos.z
			};

			float dist = Length(diff);
			const float playerRadius = 0.8f;
			const float pushRange = playerRadius + bossRadius;

			if (dist < pushRange && dist > 0.001f) {
				Vector3 pushDir = Normalize(diff);
				float pushAmount = pushRange - dist;
				playerPos += pushDir * pushAmount; // まず押し出し候補位置を作る
			}
			};

		if (bossManager_->IsSplitBossBattle()) {
			// 10階は左右それぞれのボスで個別に押し出す
			for (int i = 0; i < 2; ++i) {
				Boss* splitBoss = bossManager_->GetBossAt(i);
				if (!splitBoss || splitBoss->IsDead() || !splitBoss->IsVisible()) {
					continue;
				}

				resolveBossPush(splitBoss->GetPosition(), 2.2f);
			}
		} else {
			// 通常ボスは単体で押し出す
			Boss* normalBoss = bossManager_->GetBoss();
			if (normalBoss && !normalBoss->IsDead() && normalBoss->IsVisible()) {
				resolveBossPush(normalBoss->GetPosition(), 2.2f);
			}
		}

		// 押し出し後の位置を、そのまま確定せず壁に入らない形で補正する
		Vector3 moveDelta = playerPos - oldPlayerPos;
		Vector3 resolvedPos = oldPlayerPos;

		// X方向だけ先に試す
		if (!isPlayerHitWall({ oldPlayerPos.x + moveDelta.x, oldPlayerPos.y, oldPlayerPos.z })) {
			resolvedPos.x = oldPlayerPos.x + moveDelta.x;
		}

		// 次にZ方向を試す
		if (!isPlayerHitWall({ resolvedPos.x, oldPlayerPos.y, oldPlayerPos.z + moveDelta.z })) {
			resolvedPos.z = oldPlayerPos.z + moveDelta.z;
		}

		playerManager_->SetPosition(resolvedPos);
		playerPos_ = resolvedPos;
	}
	// ==========================================
	// カメラ・各種オブジェクトの更新
	// ==========================================
	// メインカメラの更新（プレイヤーに追従）
	if (camera_) {
		if (debugCamera_ && !debugCamera_->IsActive()) {

			// 今のカメラ位置と回転を取得
			Vector3 currentPos = camera_->GetTranslation();
			Vector3 currentRot = camera_->GetRotation();

			bool isBossIntroPlaying = bossManager_ ? bossManager_->IsBossIntroPlaying() : false;
			BossManager::IntroCameraState bossIntroState =
				bossManager_ ? bossManager_->GetBossIntroCameraState() : BossManager::IntroCameraState::None;
			int bossIntroTimer = bossManager_ ? bossManager_->GetBossIntroTimer() : 0;

			// 目標位置と目標回転
			Vector3 targetPos = currentPos;
			Vector3 targetRot = currentRot;

			// ボス部屋突入時の演出カメラ
			if (isBossIntroPlaying && mapManager_ && mapManager_->IsBossMap() && boss) {

				// 分裂ボス時は2体の中心、通常時は通常ボス位置を使う
				Vector3 bossPos = bossManager_->GetBossFocusPosition();


				// 最初に上空からボス出現地点を見る
				if (bossIntroState == BossManager::IntroCameraState::SkyLook) {

					targetPos = {
						bossPos.x,
						bossPos.y + 18.0f,
						bossPos.z - 20.0f
					};

					targetRot = {
						0.95f, 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}
					if (bossIntroTimer <= 0) {
						if (bossManager_) {
							bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::BossReveal);
							bossManager_->SetBossIntroTimer(40);
						}
					}
				}

				// 上空のボスをしっかり見せる
				else if (bossIntroState == BossManager::IntroCameraState::BossReveal) {

					targetPos = {
						bossPos.x,
						bossPos.y + 10.0f,
						bossPos.z - 18.0f
					};

					targetRot = {
						0.70f, 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}
					if (bossIntroTimer <= 0) {
						if (bossManager_) {
							bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::BossDropFollow);
							bossManager_->SetBossIntroTimer(60);
						}
					}
				}

				// 落下中のボスを追う
				else if (bossIntroState == BossManager::IntroCameraState::BossDropFollow) {

					targetPos = {
						bossPos.x,
						bossPos.y + 8.0f,
						bossPos.z - 15.0f
					};

					targetRot = {
						0.58f, 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}
					if (bossIntroTimer <= 0) {
						if (bossManager_) {
							bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::BossLandImpact);
							bossManager_->SetBossIntroTimer(26);
						}
					}
				}

				// 着地の見せ場
				else if (bossIntroState == BossManager::IntroCameraState::BossLandImpact) {

					float impactT = 1.0f - static_cast<float>(bossIntroTimer) / 26.0f;
					float punch = std::sinf(impactT * 3.14159f);

					if (boss) {
						float poseT = 1.0f - static_cast<float>(bossIntroTimer) / 26.0f;
						poseT = std::clamp(poseT, 0.0f, 1.0f);
						boss->PlayPreBattlePose(poseT);
					}

					targetPos = {
						bossPos.x,
						bossPos.y + 5.0f - punch * 1.2f,
						bossPos.z - 9.0f + punch * 1.6f
					};

					targetRot = {
						0.40f - punch * 0.08f, 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}
					if (bossIntroTimer <= 0) {
						if (bossIntroTimer <= 0) {
							if (bossManager_) {
								if (bossManager_->IsSplitBossBattle()) {
									// 10階だけ着地後に分裂演出へ進む
									bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::BossSplit);
									bossManager_->SetBossIntroTimer(28);
								} else {
									// 5階など通常ボスはそのまま戦闘カメラへ
									bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::ToBattle);
									bossManager_->SetBossIntroTimer(40);
								}
							}
						}


					}

				}
				// 10階用の分裂演出
				else if (bossIntroState == BossManager::IntroCameraState::BossSplit) {
					Boss* leftBoss = bossManager_ ? bossManager_->GetBossAt(0) : nullptr;
					Boss* rightBoss = bossManager_ ? bossManager_->GetBossAt(1) : nullptr;

					Vector3 centerPos = bossManager_->GetSplitBossCenterPosition();

					// 0.0 -> 1.0 で中央から左右へ分裂する
					float splitT = 1.0f - static_cast<float>(bossIntroTimer) / 28.0f;
					splitT = std::clamp(splitT, 0.0f, 1.0f);

					// 少し勢いを付けるための補間
					float eased = splitT * splitT * (3.0f - 2.0f * splitT);

					// 分裂時に一度少し膨らんでから小さく落ち着く
					float splitScale = 2.0f + 0.25f * std::sinf(splitT * 3.14159f);
					float finalScale = splitScale + (1.45f - splitScale) * eased;

					if (leftBoss) {
						Vector3 target = bossManager_->GetSplitBossTargetPosition(0);
						Vector3 pos = {
							centerPos.x + (target.x - centerPos.x) * eased,
							centerPos.y,
							centerPos.z
						};

						leftBoss->SetPosition(pos);
						leftBoss->SetScale({ finalScale, finalScale, finalScale });
					}

					if (rightBoss) {
						Vector3 target = bossManager_->GetSplitBossTargetPosition(1);
						Vector3 pos = {
							centerPos.x + (target.x - centerPos.x) * eased,
							centerPos.y,
							centerPos.z
						};

						rightBoss->SetPosition(pos);
						rightBoss->SetScale({ finalScale, finalScale, finalScale });
					}

					// カメラは着地位置をそのまま見せて、分裂だけ強調する
					targetPos = {
						centerPos.x,
						centerPos.y + 5.2f,
						centerPos.z - 9.5f
					};

					targetRot = {
						0.40f, 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}

					if (bossIntroTimer <= 0) {
						// 最後は分裂後サイズに固定する
						if (leftBoss) {
							leftBoss->SetScale({ 1.45f, 1.45f, 1.45f });
						}
						if (rightBoss) {
							rightBoss->SetScale({ 1.45f, 1.45f, 1.45f });
						}

						if (bossManager_) {
							bossManager_->SetBossIntroCameraState(BossManager::IntroCameraState::ToBattle);
							bossManager_->SetBossIntroTimer(40);
						}
					}
					}


				// 最後だけ通常のボス戦カメラへ寄せる
				else if (bossIntroState == BossManager::IntroCameraState::ToBattle) {

					Vector3 toBoss = {
						bossPos.x - playerPos_.x,
						0.0f,
						bossPos.z - playerPos_.z
					};

					float distance = Length(toBoss);
					Vector3 bossDir = distance > 0.01f ? Normalize(toBoss) : Vector3{ 0.0f, 0.0f, 1.0f };
					float sideLead = (std::min)(distance * 0.18f, 4.0f);
					float extraBack = (std::min)(distance * 0.25f, 6.0f);
					float extraHeight = (std::min)(distance * 0.12f, 4.0f);

					Vector3 focus = {
						playerPos_.x + bossDir.x * sideLead,
						playerPos_.y + 2.0f,
						playerPos_.z + bossDir.z * sideLead
					};

					targetPos = {
						focus.x,
						focus.y + 15.0f + extraHeight,
						focus.z - (14.0f + extraBack)
					};

					targetRot = {
						0.88f - (std::min)(distance * 0.01f, 0.10f), 0.0f, 0.0f
					};

					bossIntroTimer--;
					if (bossManager_) {
						bossManager_->SetBossIntroTimer(bossIntroTimer);
					}

					if (bossIntroTimer <= 0) {
						if (bossManager_) {
							bossManager_->EndBossIntro();
						}
						if (boss && !boss->IsDead()) {
							boss->ClearPreBattlePose();
							boss->SetState(Boss::State::Chase);
						}
					}
				}
			}

			// 通常のボス戦カメラ
			else if (mapManager_ && mapManager_->IsBossMap() && boss && !boss->IsDead()) {

				// 分裂ボス時は2体の中心、通常時は通常ボス位置を返す
				Vector3 bossPos = bossManager_->GetBossFocusPosition();

				Vector3 toBoss = {
					bossPos.x - playerPos_.x,
					0.0f,
					bossPos.z - playerPos_.z
				};
				float distance = Length(toBoss);
				Vector3 bossDir = distance > 0.01f ? Normalize(toBoss) : Vector3{ 0.0f, 0.0f, 1.0f };
				float sideLead = (std::min)(distance * 0.18f, 4.0f);
				float extraBack = (std::min)(distance * 0.25f, 6.0f);
				float extraHeight = (std::min)(distance * 0.12f, 4.0f);
				Vector3 focus = {
					playerPos_.x + bossDir.x * sideLead,
					playerPos_.y + 2.0f,
					playerPos_.z + bossDir.z * sideLead
				};

				targetPos = {
					focus.x,
					focus.y + 15.0f + extraHeight,
					focus.z - (14.0f + extraBack)
				};

				targetRot = {
					0.88f - (std::min)(distance * 0.01f, 0.10f), 0.0f, 0.0f
				};
			}

			// 通常時
			else {
				targetPos = {
					playerPos_.x,
					playerPos_.y + 15.0f,
					playerPos_.z - 15.0f
				};

				targetRot = {
					0.9f, 0.0f, 0.0f
				};
			}

			// 補間率
			float followRate = 0.08f;

			// 登場演出中は状態ごとに速さを変える
			if (isBossIntroPlaying) {
				if (bossIntroState == BossManager::IntroCameraState::SkyLook) {
					followRate = 0.35f;
				} else if (bossIntroState == BossManager::IntroCameraState::BossReveal) {
					followRate = 0.22f;
				} else {
					followRate = 0.16f;
				}
			}

			// 位置をなめらかに移動
			currentPos.x += (targetPos.x - currentPos.x) * followRate;
			currentPos.y += (targetPos.y - currentPos.y) * followRate;
			currentPos.z += (targetPos.z - currentPos.z) * followRate;

			// 回転もなめらかに移動
			currentRot.x += (targetRot.x - currentRot.x) * followRate;
			currentRot.y += (targetRot.y - currentRot.y) * followRate;
			currentRot.z += (targetRot.z - currentRot.z) * followRate;

			camera_->SetTranslation(currentPos);
			camera_->SetRotation(currentRot);
		}

		camera_->Update();
	}


	// ==========================================
	// ドロップアイテム(カード)の取得判定
	// ==========================================

	cardPickupManager_.Update();

	for (auto& pickup : cardPickupManager_.GetPickups()) {
		if (!pickup.isActive) {
			continue;
		}

		// プレイヤーとの距離計算
		Vector3 playerDiff = {
			playerPos_.x - pickup.position.x,
			playerPos_.y - pickup.position.y,
			playerPos_.z - pickup.position.z
		};

		float playerDist = Length(playerDiff);

		// プレイヤーが拾う処理
		if (player && !player->IsDead() && playerDist < 2.0f) {

			// 使ったカードが消滅中なら、拾うのを一瞬だけ保留する！
			if (handManager_.IsSelectedCardDissolving()) {
				continue;
			}

			bool success = handManager_.AddCard(pickup.card);
			if (success) {
				pickup.isActive = false;
				continue;
			}
			else {
				// 手札が一杯ならカード交換モードへ移行
				isCardSwapMode_ = true;
				pendingCard_ = pickup.card;
				// フィールドのどのアイテムを拾おうとしているかを記憶しておく
				pendingPickup_ = &pickup;

				// 手札の右端に仮置きする
				handManager_.AddPendingCard(pendingCard_);
				break;
			}
		}


	}

	// ボス頭上HPバー更新
	if (bossManager_ && bossManager_->IsSplitBossBattle() && mapManager_ && mapManager_->IsBossMap() && camera_) {
		const float splitBackWidth = 120.0f;
		const float splitBackHeight = 14.0f;
		const float splitFillMaxWidth = 112.0f;
		const float splitFillHeight = 8.0f;

		for (int i = 0; i < 2; ++i) {
			Boss* splitBoss = bossManager_->GetBossAt(i);
			Sprite* splitBackSprite = bossManager_->GetSplitBossHpBackSprite(i);
			Sprite* splitFillSprite = bossManager_->GetSplitBossHpFillSprite(i);
			if (!splitBoss || splitBoss->IsDead() || !splitBackSprite || !splitFillSprite) {
				continue;
			}

			Vector3 bossHeadPos = splitBoss->GetPosition();
			bossHeadPos.y += 2.6f;
			Vector2 screenPos = WorldToScreen(bossHeadPos);

			float hpRate = bossManager_->GetBossHpRateAt(i);
			if (hpRate < 0.0f) hpRate = 0.0f;
			if (hpRate > 1.0f) hpRate = 1.0f;

			splitBackSprite->SetPosition(screenPos);
			splitBackSprite->SetSize({ splitBackWidth, splitBackHeight });
			splitBackSprite->Update();

			Vector4 hpColor{};
			if (hpRate > 0.6f) {
				hpColor = { 0.2f, 1.0f, 0.2f, 1.0f };
			}
			else if (hpRate > 0.3f) {
				hpColor = { 1.0f, 0.9f, 0.2f, 1.0f };
			}
			else {
				hpColor = { 1.0f, 0.2f, 0.2f, 1.0f };
			}

			float fillWidth = splitFillMaxWidth * hpRate;
			float backLeft = screenPos.x - splitBackWidth * 0.5f;
			float fillLeft = backLeft + 4.0f;
			float fillCenterX = fillLeft + fillWidth * 0.5f;
			Vector2 fillPos = {
				fillCenterX,
				screenPos.y + 1.0f
			};

			splitFillSprite->SetPosition(fillPos);
			splitFillSprite->SetSize({ fillWidth, splitFillHeight });
			splitFillSprite->SetColor(hpColor);
			splitFillSprite->Update();
		}
	}

	if (bossManager_ && !bossManager_->IsSplitBossBattle() &&
		boss && !boss->IsDead() && mapManager_ && mapManager_->IsBossMap() &&
		bossHpBackSprite && bossHpFillSprite && camera_) {

		// 分裂ボス時は2体の中心位置にHPバーを出す
		Vector3 bossHeadPos = bossManager_->GetBossFocusPosition();
		bossHeadPos.y += 2.8f;


		Vector2 screenPos = WorldToScreen(bossHeadPos);

		// バーサイズ
		const float backWidth = 160.0f;
		const float backHeight = 16.0f;
		const float fillMaxWidth = 152.0f;
		const float fillHeight = 10.0f;

		// HP割合
				// 分裂ボス時は2体合計HPの割合を使う
		float hpRate = bossManager_->GetBossHpRate();

		if (hpRate < 0.0f) hpRate = 0.0f;
		if (hpRate > 1.0f) hpRate = 1.0f;

		// 背景バーは中央基準でそのまま配置
		bossHpBackSprite->SetPosition(screenPos);
		bossHpBackSprite->SetSize({ backWidth, backHeight });
		bossHpBackSprite->Update();

		// HP割合で色変更
		Vector4 hpColor{};
		if (hpRate > 0.6f) {
			hpColor = { 0.2f, 1.0f, 0.2f, 1.0f }; // 緑
		}
		else if (hpRate > 0.3f) {
			hpColor = { 1.0f, 0.9f, 0.2f, 1.0f }; // 黄
		}
		else {
			hpColor = { 1.0f, 0.2f, 0.2f, 1.0f }; // 赤
		}

		// 本体バーの現在幅
		float fillWidth = fillMaxWidth * hpRate;

		// 背景バーの左端を基準に、本体バーの中心を計算
		float backLeft = screenPos.x - backWidth * 0.5f;
		float fillLeft = backLeft + 4.0f;
		float fillCenterX = fillLeft + fillWidth * 0.5f;

		// 背景の中央から少し下に本体バーを置く
		Vector2 fillPos = {
			fillCenterX,
			screenPos.y + 1.0f
		};

		bossHpFillSprite->SetPosition(fillPos);
		bossHpFillSprite->SetSize({ fillWidth, fillHeight });
		bossHpFillSprite->SetColor(hpColor);
		bossHpFillSprite->Update();
	}
	// その他3Dオブジェクトの更新
	for (auto& obj : object3ds_) {
		obj->Update();
	}

	// スプライトの更新
	if (sprite_) {
		sprite_->SetPosition(spritePos_);
		sprite_->Update();
	}

	// ==========================================
	// ★ プレイヤーステータスUIの更新
	// ==========================================
	float currentScreenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());

	// ステータス背景（黒い帯）の更新
	if (playerStatusBgSprite_) {
		// 1. サイズの決定
	// 横幅を 800px、高さを 50px に変更する例
		float bgW = 900.0f;
		float bgH = 76.0f;
		playerStatusBgSprite_->SetSize({ bgW, bgH });

		// 2. 位置の決定 (アンカーポイントが中心 0.5 の場合)
		// 左端に寄せるなら、中心座標は「横幅の半分」にする
		float posX = bgW * 0.5f + 250.0f; // 左に 20px の余白
		float posY = bgH * 0.5f + 10.0f; // 上に 10px の余白

		playerStatusBgSprite_->SetPosition({ posX, posY });
		playerStatusBgSprite_->Update();
	}
	UpdatePlayerStatusGaugeUI();

	// テストオブジェクトの更新
	if (testObj_) {
		testObj_->Update();
	}

	// レベル(マップ)・ポストエフェクトの更新
	mapManager_->Update(playerPos_);
	PostEffect::GetInstance()->Update();

	// ミニマップ更新
	mapManager_->UpdateMinimap(minimap_.get(), playerPos_, &cardPickupManager_);

	// プレイヤーが生存中なら手札UIを更新
	if (playerManager_ && !playerManager_->IsDead()) {
		if (fistCooldownTimer_ > 0) {
			fistCooldownTimer_--;
		}
		handManager_.SetCooldownDisplay(1, fistCooldownTimer_, fistCooldownDuration_);
		if (uiCamera_) {
			uiCamera_->Update();
		}
		handManager_.Update();
	}

	// コスト不足メッセージ更新 ←ここ追加
	if (costLackMessageTimer_ > 0) {
		costLackMessageTimer_--;

		TextManager::GetInstance()->SetText("CostLack", "コスト不足です");
	}
	else {
		TextManager::GetInstance()->SetText("CostLack", "");
	}

	// プレイヤーステータス表示更新
	if (playerManager_) {
		std::string hpText =
			"HP:" + std::to_string(playerManager_->GetHP()) + "/" + std::to_string(playerManager_->GetMaxHP());

		std::string costText =
			"COST:" + std::to_string(playerManager_->GetCost()) + "/" + std::to_string(playerManager_->GetMaxCost());

		std::string levelText =
			"LV : " + std::to_string(playerManager_->GetLevel());

		std::string expText =
			"EXP : " + std::to_string(playerManager_->GetExp()) + " / " + std::to_string(playerManager_->GetNextLevelExp());

		auto textMgr = TextManager::GetInstance();
		textMgr->SetText("PlayerHP", hpText);
		textMgr->SetText("PlayerCost", costText);
		textMgr->SetText("PlayerLevel", levelText);
		textMgr->SetText("PlayerEXP", expText);

		// ★ 横並びに配置するための計算
		float topY = 28.0f;
		float startX = 260.0f;   // 左端からのX座標

		// テキストの座標を最新のウィンドウ幅に合わせて更新
		textMgr->SetPosition("PlayerHP", startX, topY);
		textMgr->SetScale("PlayerHP", 0.68f);
		textMgr->SetScale("PlayerCost", 0.68f);
		textMgr->SetScale("PlayerLevel", 0.9f);
		textMgr->SetScale("PlayerEXP", 0.9f);
		textMgr->SetPosition("PlayerCost", startX, 58.0f);
		textMgr->SetPosition("PlayerLevel", 770.0f, 30.0f);
		textMgr->SetPosition("PlayerEXP", 870.0f, 30.0f);
	}

	if (tutorial_ && tutorial_->IsActive()) {
		TextManager::GetInstance()->SetText("Floor", "");
	}
	else if (mapManager_) {
		TextManager::GetInstance()->SetText(
			"Floor",
			"FLOOR:" + std::to_string(mapManager_->GetCurrentFloor())
		);
	}

	// チュートリアル中でない時だけ、ゲームプレイ用の操作説明とクリア条件を右下へ表示する
	if (!(tutorial_ && tutorial_->IsActive())) {
		TextManager* text = TextManager::GetInstance();

		text->SetPosition("TutorialGuide", 1450.0f, 750.0f);
		text->SetCentered("TutorialGuide", false);
		text->SetScale("TutorialGuide", 0.8f);
		text->SetColor("TutorialGuide", 1.0f, 1.0f, 1.0f, 0.9f);
		text->SetText(
			"TutorialGuide",
			"操作説明\n"
			"WASD:移動\n"
			"LShift:回避\n"
			"Space:カード使用\n"
			"矢印キー:カード選択\n\n"
			"クリア条件:5階層まで進みボスを倒す"
		);
	}


	// ==========================================
	// カードシステム用のターゲット検索と更新
	// ==========================================



	Boss* targetBoss = nullptr;
	Boss* extraTargetBoss = nullptr;
	Vector3 bossPos{};

	if (bossManager_ && bossManager_->IsSplitBossBattle()) {
		// 分裂戦では左右の個体をそのままカード側へ渡す
		targetBoss = bossManager_->GetBossAt(0);
		extraTargetBoss = bossManager_->GetBossAt(1);
		bossPos = bossManager_->GetBossFocusPosition();
	}
	else if (boss && !boss->IsDead()) {
		targetBoss = boss;
		bossPos = boss->GetPosition();
	}

	if (!isBossIntroPlaying) {
		UpdateCardUse(input);
	}
	UpdateCardUseFlash();

	// ==========================================
	// 攻撃カードの「撃ち放題」タイマーと発動処理
	// ==========================================
	if (isCardReady_ && !isBossIntroPlaying) {
		cardReadyTimer_--; // 毎フレーム時間を減らす

		// 画面に表示する文字を作る
		std::string displayText = "Eキーで発動：\n" + readiedCard_.name;

		if (cardReadyTimer_ > 120) {
			// 【残り2秒(120フレーム)より多いとき】
			// まだ余裕があるので、ずっと常時表示しておく
			TextManager::GetInstance()->SetText("ReadyCardT", displayText);

		}
		else {
			// 【残り2秒以下になったとき】
			// 時間切れが近いので、チカチカ点滅させてプレイヤーを焦らす！
			// （% 20 >= 10 にすることで、少し早めのスピードで点滅します）
			if (cardReadyTimer_ % 20 >= 10) {
				TextManager::GetInstance()->SetText("ReadyCardT", displayText);
			}
			else {
				TextManager::GetInstance()->SetText("ReadyCardT", "");
			}
		}
		float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());

		// カードUIと同じ設定値を使います
		float bgWidth = 390.0f;
		float bgHeight = 200.0f;
		float marginRight = 50.0f;
		float marginTop = 10.0f;

		// X座標：カード説明の文字の左端に揃える
		float textPosX = screenW - bgWidth - marginRight;
		// Y座標：カードUIの黒枠の下端に、少し余白(20px)を足す
		float textPosY = marginTop + bgHeight + 20.0f;

		TextManager::GetInstance()->SetPosition("ReadyCardT", textPosX, textPosY);

		// Eキーで構え中の攻撃カードを発動
		if (input->Triggerkey(DIK_E)) {
			if (playerCardSystem_ && playerManager_ && !playerManager_->IsDodging()) {
				StartCardUseFlash(readiedCard_);
				playerCardSystem_->UseCard(
					readiedCard_,
					playerPos_,
					playerManager_->GetRotationY(),
					true,
					playerManager_->GetPlayer()
				);
			}
		}

		// 時間切れになったら構え状態（撃ち放題）を終了する
		if (cardReadyTimer_ <= 0) {
			isCardReady_ = false;

			// 時間切れになったら文字を空（非表示）にする
			TextManager::GetInstance()->SetText("ReadyCardT", "");
		}
	}

	// プレイヤー用カードシステム更新
	if (playerCardSystem_ && !isBossIntroPlaying) {
		playerCardSystem_->Update(
			player,
			enemyManager_.get(),
			targetBoss,
			extraTargetBoss,
			playerPos_,
			{ 0.0f, 0.0f, 0.0f },
			bossPos,
			mapManager_->GetLevelData()
		);
	}
	for (auto& block : blocks_) {
		block->Update();
	}

		// GPUパーティクル更新
	GPUParticleManager::GetInstance()->Update(1.0f / 60.0f, camera_.get());

	
	//// InstancedGroup に「最新のデータをお願い！」と渡すだけ
	//if (blockGroup_) {
	//	blockGroup_->Update(blocks_);
	//}

	// 背景枠の更新
	if (descBgSprite_) {
		descBgSprite_->Update();
	}

	// 手札がある時だけ処理
	if (handManager_.GetHandSize() > 0) {
		// 1. 今選んでいるカードの番号を取得
		int selectedIdx = handManager_.GetSelectedCardIndex();

		// 2. その番号のカード情報（CSVのデータ）をごっそり取得
		Card selectedCard = handManager_.GetCard(selectedIdx);

		// 3. 説明文だけを変数に入れる
		std::string descText = selectedCard.description;
		size_t pos = descText.find("\\n");
		while (pos != std::string::npos) {
			descText.replace(pos, 2, "\n");
			pos = descText.find("\\n", pos + 1);
		}

		// カード名とコストを合体させた文字列を作る！
		std::string displayText = "【" + selectedCard.name + "】\n  Cost : " + std::to_string(selectedCard.cost) + "\n" + descText;

		// 4. テキストオブジェクトに文字を流し込む！
		// ※ textObj_ の部分は、チームメンバーさんが作ったテキスト管理の変数名に直してください
		TextManager::GetInstance()->SetText("CardT", displayText);

		// ==========================================
		// ★ 2. 右上への配置計算
		// ==========================================
		float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());

		// 枠のサイズ
		float bgWidth = 390.0f;  // ミニマップの横幅に合わせると綺麗です
		float bgHeight = 200.0f;

		// 右端・上端からの余白
		float marginRight = 20.0f;
		float marginTop = 10.0f; // ミニマップの下に置く場合は 350.0f 前後に調整

		// 背景枠の位置（中心座標）
		float bgPosX = screenW - (bgWidth * 0.5f) - marginRight;
		float bgPosY = marginTop + (bgHeight * 0.5f);

		if (descBgSprite_) {
			descBgSprite_->SetSize({ bgWidth, bgHeight });
			descBgSprite_->SetPosition({ bgPosX, bgPosY });
			descBgSprite_->Update();
		}

		// 3. 文字の位置（枠の左上に合わせる）
		float textPosX = screenW - bgWidth - marginRight ;
		float textPosY = marginTop + 15.0f;
		TextManager::GetInstance()->SetPosition("CardT", textPosX, textPosY);
	}
	else {
		// 手札がない時は文字を消す
		TextManager::GetInstance()->SetText("CardT", "");
		if (descBgSprite_) {
			descBgSprite_->SetPosition({ -1000.0f, -1000.0f });
			descBgSprite_->Update();
		}
	}
}

void GamePlayScene::Draw() {
	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList(); // ← 1回だけ！

	Boss* boss = bossManager_ ? bossManager_->GetBoss() : nullptr;
	const bool isBossIntroPlaying = bossManager_ ? bossManager_->IsBossIntroPlaying() : false;

	// GPUパーティクルの描画準備（DispatchでComputeシェーダーを実行して、描画に必要なデータをGPU側で更新してもらう）
	GPUParticleManager::GetInstance()->Dispatch(commandList);


	// 1. 【MRT開始】キャンバスを2枚(色用とマスク用)セットする！
	PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

	// 3D描画の前準備
	Obj3dCommon::GetInstance()->PreDraw(commandList);
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

	// プレイヤー描画
	if (playerManager_) {
		playerManager_->Draw();
	}

	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::SkinningObject3D);
	if (playerManager_) {
		playerManager_->Draw();
	}

	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::SkinningObject3D_Blend);
	if (playerManager_) {
		playerManager_->DrawAfterimage();
	}

	// ボス描画
	Obj3dCommon::GetInstance()->PreDraw(commandList);
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);
	if (bossManager_) {
		bossManager_->Draw(mapManager_.get());
	}

	// 敵描画
	if (enemyManager_) {
		enemyManager_->Draw(camera_.get(), minimap_.get());
	}

	// カード使用演出描画
	if (playerCardSystem_) {
		// カードエフェクトの3Dオブジェクトが正しいパイプラインで描画されるようにする
		Obj3dCommon::GetInstance()->PreDraw(commandList);
		PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

		playerCardSystem_->Draw();
	}
	DrawCardUseFlash();

	cardPickupManager_.Draw();

	// 3Dオブジェクト描画
	for (auto& obj : object3ds_) {
		obj->Draw();
	}

	
	// InstancedGroup
	if (blockGroup_) {
		blockGroup_->Draw(camera_.get());
	}

	

	// マップ描画
	mapManager_->Draw(playerPos_);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);

	// 手札カードもBloom対象にしたいので、MRT描画中に3Dとして描く
	if (!isBossIntroPlaying) {

		// ==========================================
		// ★ 修正1：黒幕を「手札」より先に描画する！
		// ==========================================
		if (isCardSwapMode_) {
			SpriteCommon::GetInstance()->PreDraw(commandList);
			if (swapDarkOverlay_) {
				swapDarkOverlay_->Draw();
			}
			if (swapUiSprite_) {
				swapUiSprite_->Draw();
			}
		}

		commandList->ClearDepthStencilView(dxCommon->GetDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		Obj3dCommon::GetInstance()->PreDraw(commandList);
		PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);
		handManager_.Draw();
	}

	// =========================================
	// 2. MRT終了
	// =========================================
	PostEffect::GetInstance()->PostDrawSceneMRT(commandList);

	// =========================================
	// 3. ポストエフェクト適用（グレースケール等）
	// =========================================
	PostEffect::GetInstance()->Draw(commandList);

	// =========================================
	// 4. Bloomパス
	// =========================================
	uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
	uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();
	Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
	uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

	// =========================================
	// 5. バックバッファへ最終出力
	// =========================================
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
	commandList->DrawInstanced(3, 1, 0, 0);

	// =========================================
	// 6. UI描画（バックバッファに直接）
	// =========================================
	SpriteCommon::GetInstance()->PreDraw(commandList);

	if (!isBossIntroPlaying) {

		if (!isCardSwapMode_) {
			handManager_.DrawCooldownOverlays();
		}

		if (handManager_.GetHandSize() > 0 && descBgSprite_) {
			descBgSprite_->Draw();
		}

		if (bossManager_) {
			bossManager_->DrawHpBar(mapManager_.get());
		}

		if (playerStatusBgSprite_) {
			playerStatusBgSprite_->Draw();
		}
		DrawPlayerStatusGaugeUI();

		if (minimap_) {
			minimap_->Draw();
		}

		levelUpBonusManager_.Draw();

		
		DrawPauseUI();

		SpriteCommon::GetInstance()->PreDraw(commandList);
		TextManager::GetInstance()->Draw();
	}

	// チュートリアルのポーズ背景だけは必要なら個別で残す
	if (tutorial_ && tutorial_->IsActive() && tutorial_->IsGameplayPausedByTutorial() && pauseBgSprite_) {
		pauseBgSprite_->Draw();
	}

	// =========================================
	// 7. フェードスプライト（最前面）
	// =========================================
	if (fadeSprite_ && transitionState_ != TransitionState::None) {
		fadeSprite_->Draw();
	}
	if (isFloorTransitionTextVisible_ &&
		transitionState_ == TransitionState::BlackHold &&
		!(tutorial_ && tutorial_->IsActive())) {
		TextManager::GetInstance()->DrawText("FloorTransition");
	}

	DrawCharacterHitboxesDebug();
	DrawBossIntroLetterbox();
}


void GamePlayScene::DrawDebugUI() {

#ifdef USE_IMGUI
	Player* player = playerManager_ ? playerManager_->GetPlayer() : nullptr;
	Boss* boss = bossManager_ ? bossManager_->GetBoss() : nullptr;
	// 3Dオブジェクト、カメラ、パーティクルのUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if (camera_) { camera_->DrawDebugUI(); }
	if (debugCamera_) { debugCamera_->DrawDebugUI(); }
	//ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

	if (player) {
		player->DrawAnimationDebugUI();
	}

	if (mapManager_) {
		mapManager_->DrawDebugUI();
	}

	ImGui::Begin("Block Dissolve Test");

	// スライダーで 0.0(通常) 〜 1.0(消滅) を操作
	if (ImGui::SliderFloat("ブロックの消滅度", &dissolveThreshold_, 0.0f, 1.0f)) {
		if (testObj_) {
			// スライダーを動かすと、このブロックの閾値だけが書き換わる
			testObj_->SetDissolveThreshold(dissolveThreshold_);
		}
	}

	// 便利なリセットボタン
	if (ImGui::Button("元に戻す")) {
		dissolveThreshold_ = 0.0f;
		if (testObj_) {

			testObj_->SetDissolveThreshold(0.0f);
		}

	}
	ImGui::SameLine();
	if (ImGui::Button("完全に消す")) {
		dissolveThreshold_ = 1.0f;
		if (testObj_) {
			testObj_->SetDissolveThreshold(1.0f);
		}
	}

	ImGui::End();

	ImGui::Begin("Card System Test");

	ImGui::Separator();
	ImGui::Text("[Card Pickups]");
	for (size_t i = 0; i < cardPickupManager_.GetPickups().size(); ++i) {
		const auto& pickup = cardPickupManager_.GetPickups()[i];
		ImGui::Text("%s : pos(%.1f, %.1f, %.1f) active=%s",
			pickup.card.name.c_str(),
			pickup.position.x,
			pickup.position.y,
			pickup.position.z,
			pickup.isActive ? "true" : "false");
	}

	// プレイヤー状態を表示
	if (playerManager_) {
		ImGui::Text("Player Level: %d", playerManager_->GetLevel());
		ImGui::Text("Player EXP: %d / %d", playerManager_->GetExp(), playerManager_->GetNextLevelExp());
		ImGui::Text("Player Cost: %d / %d", playerManager_->GetCost(), playerManager_->GetMaxCost());
		ImGui::Text("Player HP: %d / %d", playerManager_->GetHP(), playerManager_->GetMaxHP());
		ImGui::Text("Player Dead: %s", playerManager_->IsDead() ? "true" : "false");
		ImGui::Text("Player Hit: %s", playerManager_->IsHit() ? "true" : "false");
		ImGui::Text("Player Invincible: %s", playerManager_->IsInvincible() ? "true" : "false");
	}

	if (boss) {
		ImGui::Separator();
		ImGui::Text("[Boss]");
		ImGui::Text("Boss HP: %d / %d", boss->GetHP(), boss->GetMaxHP());
		ImGui::Text("Boss Dead: %s", boss->IsDead() ? "true" : "false");
	}

	// デバッグ用に経験値を加算
	if (ImGui::Button("Add EXP +1")) {
		if (playerManager_) {
			playerManager_->AddExp(1);
		}
	}

	ImGui::Separator();
	ImGui::Text("[Minimap Debug]");

	if (minimap_ && ImGui::Button("Reveal Full Minimap")) {
		minimap_->RevealAllMap();
	}

	if (minimap_) {
		ImGui::SameLine();
		if (ImGui::Button("Reset Minimap Discovery")) {
			minimap_->ResetDiscoveryMap();
		}
	}


	ImGui::Separator();
	ImGui::Text("[Dungeon Floor]");

	ImGui::Text("Current Floor: %d F", mapManager_ ? mapManager_->GetCurrentFloor() : 0);

	if (ImGui::Button("Go to Next Floor (Stairs)")) {
		mapManager_->AdvanceFloor(
			enemyManager_.get(),
			bossManager_.get(),
			minimap_.get(),
			[this]() { ResetBattleDebug(); }
		); // ボタンを押したら次の階層へ
	}
	if (mapManager_ && ImGui::Button("Go to 5F Boss")) {
		// 4階にしてから次の階層遷移を使って5階へ飛ばす
		mapManager_->SetCurrentFloor(4);
		mapManager_->AdvanceFloor(
			enemyManager_.get(),
			bossManager_.get(),
			minimap_.get(),
			[this]() { ResetBattleDebug(); }
		);
	}

	if (mapManager_ && ImGui::Button("Go to 10F Boss")) {
		// 9階にしてから次の階層遷移を使って10階へ飛ばす
		mapManager_->SetCurrentFloor(9);
		mapManager_->AdvanceFloor(
			enemyManager_.get(),
			bossManager_.get(),
			minimap_.get(),
			[this]() { ResetBattleDebug(); }
		);
	}

	ImGui::Separator();


	// 図鑑（CardDatabase）からIDを指定して正しいデータを拾う！
	ImGui::SameLine();
	if (ImGui::Button("ファイヤーボール (ID: 2)")) {
		handManager_.AddCard(CardDatabase::GetCardData(2));
	}
	if (ImGui::Button(" ポーション (ID: 3)")) {
		handManager_.AddCard(CardDatabase::GetCardData(3));
	}
	if (ImGui::Button(" スピードアップ (ID: 4)")) {
		handManager_.AddCard(CardDatabase::GetCardData(4));
	}
	if ( ImGui::Button(" シールド (ID: 5)") ) {
		handManager_.AddCard(CardDatabase::GetCardData(5));
	}
	if (ImGui::Button(" アイスボール (ID: 6)")) {
		handManager_.AddCard(CardDatabase::GetCardData(6));
	}
	if (ImGui::Button(" トゲ (ID: 7)")) {
		handManager_.AddCard(CardDatabase::GetCardData(7));
	}

	if (ImGui::Button(" 身代わり (ID: 8)")) {
		handManager_.AddCard(CardDatabase::GetCardData(8));
	}
	if ( ImGui::Button(" 攻撃力減少 (ID: 9)") ) {
		handManager_.AddCard(CardDatabase::GetCardData(9));
	}
	if ( ImGui::Button(" クロー (ID: 10)") ) {
		handManager_.AddCard(CardDatabase::GetCardData(10));
	}

	if (ImGui::Button(" マップ開示 (ID: 11)")) {
		handManager_.AddCard(CardDatabase::GetCardData(11));
	}

	if (ImGui::Button("コストブースト(ID:12)")) {
		handManager_.AddCard(CardDatabase::GetCardData(12));
	}

	if (ImGui::Button("蹴り(ID:13)")) {
		handManager_.AddCard(CardDatabase::GetCardData(13));
	}

	if (ImGui::Button("剣(ID:14)")) {
		handManager_.AddCard(CardDatabase::GetCardData(14));
	}

	if (ImGui::Button("ハンマー(ID:15)")) {
		handManager_.AddCard(CardDatabase::GetCardData(15));
	}

	if (ImGui::Button("槍(ID:16)")) {
		handManager_.AddCard(CardDatabase::GetCardData(16));
	}

	ImGui::Separator();

	ImGui::Text("[Player Hand] : %d/10", handManager_.GetHandSize());

	//手札の数だけループしてボタンを作る
	for (int i = 0; i < handManager_.GetHandSize(); ++i) {
		Card card = handManager_.GetCard(i);

		//ボタンの名前
		std::string btnName = card.name + "(Cost:" + std::to_string(card.cost) + ")##" + std::to_string(i);

		// 使う処理を入れる場合はこのif文の中に書く
		if (ImGui::Button(btnName.c_str())) {
			// 例：手札を使用する処理
		}
	}

	ImGui::End();


	


#endif

}

void GamePlayScene::ResetBattleDebug() {

	// プレイヤー状態をリセット
	if (playerManager_) {
		playerManager_->Reset();
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();

		if (playerManager_->GetPlayer()) {
			playerManager_->GetPlayer()->SetEnemyAtkDebuffed(false);
		}
	}

	if (bossManager_) {
		bossManager_->Reset();
	}

	// カード使用システムの状態をリセット
	if (playerCardSystem_) {
		playerCardSystem_->Reset();
	}
	fistCooldownTimer_ = 0;
	handManager_.SetCooldownDisplay(1, fistCooldownTimer_, fistCooldownDuration_);

	/*for (auto &system : enemyCardSystems_) {
		if (system) {
			system->Reset();
		}
	}*/

	// 手札を初期化
	/*handManager_.Initialize(uiCamera_.get(), textures_["noise0"].srvIndex);
	handManager_.AddCard(CardDatabase::GetCardData(1));*/

	// ダンジョン生成 + プレイヤー再配置 + 敵/カード再生成 + ボス再配置
	RegenerateDungeonAndRespawnPlayer(5);

	// 再配置後のプレイヤー位置とスケールを取り直す
	if (playerManager_) {
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}

	if (minimap_ && mapManager_) {
		minimap_->SetLevelData(&mapManager_->GetLevelData());
	}

	// 交換モードも戻しておく
	isCardSwapMode_ = false;
	pendingCard_ = Card{};

	// レベルボーナスのリセット
	int currentLevel = playerManager_ ? playerManager_->GetLevel() : 1;
	levelUpBonusManager_.Reset(currentLevel);
}


void GamePlayScene::UpdateCardSwapMode(Input* input) {

	// 手札の選択と見た目の更新
	handManager_.Update();
	uiCamera_->Update(); // UIカメラの更新もここで行う

	// 交換フェイズ中もカード説明UIを更新する！
	if (handManager_.GetHandSize() > 0) {
		int selectedIdx = handManager_.GetSelectedCardIndex();
		Card selectedCard = handManager_.GetCard(selectedIdx);

		std::string descText = selectedCard.description;
		size_t pos = descText.find("\\n");
		while (pos != std::string::npos) {
			descText.replace(pos, 2, "\n");
			pos = descText.find("\\n", pos + 1);
		}

		std::string displayText = "【" + selectedCard.name + "】\n  Cost : " + std::to_string(selectedCard.cost) + "\n" + descText;
		TextManager::GetInstance()->SetText("CardT", displayText);

		float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
		float bgWidth = 390.0f;
		float bgHeight = 200.0f;
		float marginRight = 20.0f;
		float marginTop = 10.0f;

		float bgPosX = screenW - (bgWidth * 0.5f) - marginRight;
		float bgPosY = marginTop + (bgHeight * 0.5f);

		if (descBgSprite_) {
			descBgSprite_->SetSize({ bgWidth, bgHeight });
			descBgSprite_->SetPosition({ bgPosX, bgPosY });
			descBgSprite_->Update();
		}

		float textPosX = screenW - bgWidth - marginRight;
		float textPosY = marginTop + 15.0f;
		TextManager::GetInstance()->SetPosition("CardT", textPosX, textPosY);
	}

	if (input->Triggerkey(DIK_SPACE)) {
		// 現在選んでいるカードを取得
		int selectedIdx = handManager_.GetSelectedCardIndex();

		// 選んでいるカードがID: 1（初期カード)なら交換をしない
		if (handManager_.GetCard(selectedIdx).id == 1) {
			return;
		}

		handManager_.RemoveCardImmediate(selectedIdx);

		handManager_.AddCard(pendingCard_);

		// 手札の「仮置き（保留）」状態を空っぽにしてリセットする
		handManager_.AddPendingCard({ -1, "", 0 });

		// 交換成功したら、地面に落ちていたアイテムを消す！
		if (pendingPickup_) {
			pendingPickup_->isActive = false;
			pendingPickup_ = nullptr;
		}

		// 交換が終わったら、一番左(0)を選択し直す！
		handManager_.SetSelectedCardIndex(0);

		isCardSwapMode_ = false;
	}
	
}

void GamePlayScene::StartCardUseFlash(const Card& card) {
	if (!camera_ || card.modelName.empty()) {
		return;
	}

	auto flashObj = Obj3d::Create(card.modelName);
	if (!flashObj) {
		return;
	}

	flashObj->SetCamera(camera_.get());
	if (auto it = textures_.find("noise0"); it != textures_.end()) {
		flashObj->SetNoiseTexture(it->second.srvIndex);
	}

	Vector3 dissolveColor = { 1.0f, 0.65f, 0.12f };
	if (card.effectType == CardEffectType::Heal) {
		dissolveColor = { 0.1f, 1.0f, 0.25f };
	} else if (card.effectType == CardEffectType::Defense) {
		dissolveColor = { 0.15f, 0.55f, 1.0f };
	} else if (card.effectType == CardEffectType::Special) {
		dissolveColor = { 0.75f, 0.25f, 1.0f };
	}
	flashObj->SetDissolveColor(dissolveColor);
	flashObj->SetDissolveThreshold(0.0f);
	flashObj->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });

	cardUseFlashObj_ = std::move(flashObj);
	cardUseFlashTimer_ = kCardUseFlashDuration;
	UpdateCardUseFlash();
}

void GamePlayScene::UpdateCardUseFlash() {
	if (!cardUseFlashObj_) {
		return;
	}

	if (cardUseFlashTimer_ <= 0) {
		cardUseFlashObj_.reset();
		return;
	}

	const float progress = 1.0f - static_cast<float>(cardUseFlashTimer_) / static_cast<float>(kCardUseFlashDuration);
	const float yaw = playerManager_ ? playerManager_->GetRotationY() : 0.0f;
	const Vector3 forward = { std::sinf(yaw), 0.0f, std::cosf(yaw) };
	const Vector3 position = {
		playerPos_.x + forward.x * (0.8f + progress * 0.18f),
		playerPos_.y + 1.18f + std::sinf(progress * 3.141592f) * 0.12f,
		playerPos_.z + forward.z * (0.8f + progress * 0.18f)
	};
	const float scale = 0.42f + std::sinf(progress * 3.141592f) * 0.12f;
	const float dissolve = std::clamp((progress - 0.42f) / 0.58f, 0.0f, 1.0f);
	const float flash = 0.75f + std::sinf(progress * 3.141592f) * 0.45f;

	cardUseFlashObj_->SetTranslation(position);
	cardUseFlashObj_->SetRotation({ 0.0f, yaw, 0.0f });
	cardUseFlashObj_->SetScale({ scale, scale, scale });
	cardUseFlashObj_->SetDissolveThreshold(dissolve);
	cardUseFlashObj_->SetColor({ flash, flash, flash, 1.0f });
	cardUseFlashObj_->Update();

	cardUseFlashTimer_--;
}

void GamePlayScene::DrawCardUseFlash() {
	if (cardUseFlashObj_) {
		cardUseFlashObj_->Draw();
	}
}

void GamePlayScene::UpdatePlayerStatusGaugeUI() {
	if (!playerManager_) {
		return;
	}

	const float panelLeft = 250.0f;
	const float labelWidth = 170.0f;
	const float gaugeLeft = panelLeft + labelWidth + 18.0f;
	const float gaugeWidth = 300.0f;
	const float gaugeHeight = 16.0f;
	const float hpY = 42.0f;
	const float costY = 72.0f;

	auto updateGauge = [gaugeLeft, gaugeWidth, gaugeHeight](Sprite* frame, Sprite* back, Sprite* fill, float y, float ratio, const Vector4& fillColor) {
		const float safeRatio = std::clamp(ratio, 0.0f, 1.0f);

		if (frame) {
			frame->SetPosition({ gaugeLeft + gaugeWidth * 0.5f, y });
			frame->SetSize({ gaugeWidth + 4.0f, gaugeHeight + 4.0f });
			frame->Update();
		}
		if (back) {
			back->SetPosition({ gaugeLeft + gaugeWidth * 0.5f, y });
			back->SetSize({ gaugeWidth, gaugeHeight });
			back->Update();
		}
		if (fill) {
			fill->SetPosition({ gaugeLeft, y });
			fill->SetSize({ gaugeWidth * safeRatio, gaugeHeight });
			fill->SetColor(fillColor);
			fill->Update();
		}
	};

	const float hpRatio = playerManager_->GetMaxHP() > 0
		? static_cast<float>(playerManager_->GetHP()) / static_cast<float>(playerManager_->GetMaxHP())
		: 0.0f;
	const float costRatio = playerManager_->GetMaxCost() > 0
		? static_cast<float>(playerManager_->GetCost()) / static_cast<float>(playerManager_->GetMaxCost())
		: 0.0f;
	Vector4 hpColor = { 0.95f, 0.12f, 0.13f, 0.95f };
	if (hpRatio > 0.5f) {
		hpColor = { 0.15f, 0.9f, 0.25f, 0.95f };
	} else if (hpRatio > 0.25f) {
		hpColor = { 1.0f, 0.82f, 0.12f, 0.95f };
	}

	updateGauge(playerHpGaugeFrameSprite_.get(), playerHpGaugeBackSprite_.get(), playerHpGaugeFillSprite_.get(), hpY, hpRatio, hpColor);
	updateGauge(playerCostGaugeFrameSprite_.get(), playerCostGaugeBackSprite_.get(), playerCostGaugeFillSprite_.get(), costY, costRatio, { 0.18f, 0.68f, 1.0f, 0.95f });
}

void GamePlayScene::DrawPlayerStatusGaugeUI() {
	if (playerHpGaugeFrameSprite_) {
		playerHpGaugeFrameSprite_->Draw();
	}
	if (playerHpGaugeBackSprite_) {
		playerHpGaugeBackSprite_->Draw();
	}
	if (playerHpGaugeFillSprite_) {
		playerHpGaugeFillSprite_->Draw();
	}
	if (playerCostGaugeFrameSprite_) {
		playerCostGaugeFrameSprite_->Draw();
	}
	if (playerCostGaugeBackSprite_) {
		playerCostGaugeBackSprite_->Draw();
	}
	if (playerCostGaugeFillSprite_) {
		playerCostGaugeFillSprite_->Draw();
	}
}

void GamePlayScene::UpdateCardUse(Input* input) {

	// PlayerManager が無ければ何もしない
	if (!playerManager_) {
		return;
	}

	// プレイヤー本体を取得
	Player* player = playerManager_->GetPlayer();

	// プレイヤー不在、死亡中、または入力が無ければ何もしない
	if (!player || playerManager_->IsDead() || !input->Triggerkey(DIK_SPACE)) {
		return;
	}

	// 回避中はカードを使えない
	if (playerManager_->IsDodging()) {
		return;
	}

	// 行動ロック中はカードを使えない
	if (playerManager_->IsActionLocked()) {
		return;
	}

	// 手札のディゾルブ中は使えない
	if (handManager_.IsSelectedCardDissolving()) {
		return;
	}

	// 現在選択中のカードを取得
	Card selectedCard = handManager_.GetSelectedCard();
	if (selectedCard.id == -1) {
		return;
	}

	if (selectedCard.id == 1 && fistCooldownTimer_ > 0) {
		return;
	}

	// シールドが有効中は、重ねがけを禁止する
	if (selectedCard.id == 5 && player->GetShieldHits() > 0) {
		return;
	}

	auto isImmediateAttack = [](int id) {
		switch (id) {
		case 1:  // 拳
		case 10: // クロー
		case 13: // 蹴り
		case 14: // 剣
		case 15: // ハンマー
		case 16: // 槍
			return true;
		default:
			return false;
		}
		};

	// 攻撃カード構え中は他の攻撃カードを使えない
	if (isCardReady_ && !isImmediateAttack(selectedCard.id) && static_cast<int>(selectedCard.effectType) == 0) {
		return;
	}

	// コスト不足ならメッセージを出して終了
	if (!playerManager_->CanUseCost(selectedCard.cost)) {
		costLackMessageTimer_ = 60;
		return;
	}

	// コストを消費
	playerManager_->UseCost(selectedCard.cost);
	StartCardUseFlash(selectedCard);

	// 攻撃カードなら構え状態にする
	if (!isImmediateAttack(selectedCard.id) && static_cast<int>(selectedCard.effectType) == 0) {
		isCardReady_ = true;
		readiedCard_ = selectedCard;
		cardReadyTimer_ = 60 * 5;
	}
	else {
		// それ以外のカードは即時発動
		if (playerCardSystem_) {
			playerCardSystem_->UseCard(
				selectedCard,
				playerPos_,
				playerManager_->GetRotationY(),
				true,
				player
			);
		}

		if (selectedCard.id == 9) {
			player->SetEnemyAtkDebuffed(true);

			// ※ 先ほど作成したデバフUIクラス等があれば、ここで一緒に呼び出して文字を表示します
			floorEffectManager_.ActivateDebuff(selectedCard.name);
		}

		if (selectedCard.id == 1) {
			fistCooldownTimer_ = fistCooldownDuration_;
			handManager_.SetCooldownDisplay(1, fistCooldownTimer_, fistCooldownDuration_);
		}
	}

	// 初期カード以外は使用後にディゾルブ開始
	if (selectedCard.id != 1) {
		handManager_.StartDissolveSelectedCard();
	}

	if (selectedCard.id == 9 || selectedCard.id == 11) {
		// ★ デバフ発動をマネージャーに頼む！
		floorEffectManager_.ActivateDebuff(selectedCard.name);
	}
}

void GamePlayScene::UpdatePause(Input* input) {

	// 上下で選択
	if (input->Triggerkey(DIK_W)) {
		pauseSelection_--;
		if (pauseSelection_ < 0) {
			pauseSelection_ = 1;
		}
	}

	if (input->Triggerkey(DIK_S)) {
		pauseSelection_++;
		if (pauseSelection_ > 1) {
			pauseSelection_ = 0;
		}
	}

	// 決定
	if (input->Triggerkey(DIK_SPACE) || input->Triggerkey(DIK_RETURN)) {
		if (pauseSelection_ == 0) {
			isPaused_ = false; // ゲームに戻る
		}
		else if (pauseSelection_ == 1) {
			SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
			return;
		}
	}

	// ポーズ中の文字表示
	TextManager::GetInstance()->SetText("PauseTitle", "PAUSE");

	if (pauseSelection_ == 0) {
		TextManager::GetInstance()->SetText("PauseResume", "> Resume");
		TextManager::GetInstance()->SetText("PauseToTitle", "  Title");
	}
	else {
		TextManager::GetInstance()->SetText("PauseResume", "  Resume");
		TextManager::GetInstance()->SetText("PauseToTitle", "> Title");
	}

	// 背景更新
	if (pauseBgSprite_) {
		pauseBgSprite_->Update();
	}

}

void GamePlayScene::DrawPauseUI() {

	if (!isPaused_) {
		TextManager::GetInstance()->SetText("PauseTitle", "");
		TextManager::GetInstance()->SetText("PauseResume", "");
		TextManager::GetInstance()->SetText("PauseToTitle", "");
		return;
	}

	if (pauseBgSprite_) {
		pauseBgSprite_->Draw();
	}
}

void GamePlayScene::UpdateBossIntroLetterbox() {
	const bool isBossIntroPlaying = bossManager_ && bossManager_->IsBossIntroPlaying();

	if (isBossIntroPlaying) {
		bossIntroLetterboxFadeTimer_ = bossIntroLetterboxFadeDuration_;
	}
	else if (wasBossIntroPlaying_) {
		bossIntroLetterboxFadeTimer_ = bossIntroLetterboxFadeDuration_;
	}
	else if (bossIntroLetterboxFadeTimer_ > 0) {
		bossIntroLetterboxFadeTimer_--;
	}

	wasBossIntroPlaying_ = isBossIntroPlaying;
}

void GamePlayScene::DrawBossIntroLetterbox() {
	const bool isBossIntroPlaying = bossManager_ && bossManager_->IsBossIntroPlaying();
	if (!isBossIntroPlaying && bossIntroLetterboxFadeTimer_ <= 0) {
		return;
	}

	float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
	float barH = screenH * 0.13f;
	float fadeT = static_cast<float>(bossIntroLetterboxFadeTimer_) / static_cast<float>(bossIntroLetterboxFadeDuration_);
	float letterboxAlpha = isBossIntroPlaying ? 1.0f : fadeT;
	float slideOffset = isBossIntroPlaying ? 0.0f : (1.0f - fadeT) * barH;

	if (bossIntroTopBar_) {
		bossIntroTopBar_->SetPosition({ screenW * 0.5f, barH * 0.5f - slideOffset });
		bossIntroTopBar_->SetSize({ screenW, barH });
		bossIntroTopBar_->SetColor({ 0.0f, 0.0f, 0.0f, letterboxAlpha });
		bossIntroTopBar_->Update();
		bossIntroTopBar_->Draw();
	}

	if (bossIntroBottomBar_) {
		bossIntroBottomBar_->SetPosition({ screenW * 0.5f, screenH - barH * 0.5f + slideOffset });
		bossIntroBottomBar_->SetSize({ screenW, barH });
		bossIntroBottomBar_->SetColor({ 0.0f, 0.0f, 0.0f, letterboxAlpha });
		bossIntroBottomBar_->Update();
		bossIntroBottomBar_->Draw();
	}
}

GamePlayScene::GamePlayScene() {}

GamePlayScene::~GamePlayScene() {}
// 終了
void GamePlayScene::Finalize() {

	object3ds_.clear();

	// プレイヤー管理クラスを解放
	if (playerManager_) {
		playerManager_->Finalize();
		playerManager_.reset();
	}

	playerCardSystem_.reset();
	//enemyCardSystems_.clear();
	if (bossManager_) {
		bossManager_->Finalize();
		bossManager_.reset();
	}
	pauseBgSprite_.reset();
	bossIntroTopBar_.reset();
	bossIntroBottomBar_.reset();

	TextManager::GetInstance()->Finalize();

	if (tutorial_) {
		tutorial_->Finalize();
		tutorial_.reset();
	}

	
	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}

Vector2 GamePlayScene::WorldToScreen(const Vector3& worldPos) const {
	Vector2 screen{};
	return ProjectWorldToScreen(worldPos, screen) ? screen : Vector2{ -10000.0f, -10000.0f };
}

bool GamePlayScene::ProjectWorldToScreen(const Vector3& worldPos, Vector2& screenPos) const {
	if (!camera_) {
		return false;
	}
	Matrix4x4 viewProjection = camera_->GetViewProjectionMatrix();

	Vector4 clip{};
	clip.x = worldPos.x * viewProjection.m[0][0] + worldPos.y * viewProjection.m[1][0] + worldPos.z * viewProjection.m[2][0] + 1.0f * viewProjection.m[3][0];
	clip.y = worldPos.x * viewProjection.m[0][1] + worldPos.y * viewProjection.m[1][1] + worldPos.z * viewProjection.m[2][1] + 1.0f * viewProjection.m[3][1];
	clip.z = worldPos.x * viewProjection.m[0][2] + worldPos.y * viewProjection.m[1][2] + worldPos.z * viewProjection.m[2][2] + 1.0f * viewProjection.m[3][2];
	clip.w = worldPos.x * viewProjection.m[0][3] + worldPos.y * viewProjection.m[1][3] + worldPos.z * viewProjection.m[2][3] + 1.0f * viewProjection.m[3][3];

	if (clip.w <= 0.001f) {
		return false;
	}

	float invW = 1.0f / clip.w;
	float ndcX = clip.x * invW;
	float ndcY = clip.y * invW;

	screenPos.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
	screenPos.y = (-ndcY * 0.5f + 0.5f) * static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
	return true;
}

void GamePlayScene::DrawCharacterHitboxesDebug() const {
	if (!showCharacterHitboxes_) {
		return;
	}

	if (playerManager_) {
		if (Player* player = playerManager_->GetPlayer(); player && !player->IsDead() && player->IsVisible()) {
			DrawDebugCircleXZ(player->GetPosition(), 0.6f, IM_COL32(80, 255, 120, 230), 2.0f);
		}
	}

	if (enemyManager_) {
		for (const auto& enemy : enemyManager_->GetEnemies()) {
			if (enemy && !enemy->IsDead() && enemy->IsVisible()) {
				const float enemyRadius = 0.9f * (std::max)(enemy->GetScale().x, enemy->GetScale().z);
				DrawDebugCircleXZ(enemy->GetPosition(), enemyRadius, IM_COL32(255, 80, 80, 220), 1.7f);
			}
		}
	}

	if (bossManager_) {
		for (int i = 0; i < 2; ++i) {
			if (Boss* boss = bossManager_->GetBossAt(i); boss && !boss->IsDead() && boss->IsVisible()) {
				DrawDebugCircleXZ(boss->GetPosition(), 2.2f, IM_COL32(190, 110, 255, 230), 2.2f);
			}
		}
	}
}

void GamePlayScene::DrawDebugAABB(const Vector3& center, const Vector3& halfSize, unsigned int color, float thickness) const {
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	if (!drawList) {
		return;
	}

	const std::array<Vector3, 8> corners = {
		Vector3{ center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
		Vector3{ center.x + halfSize.x, center.y - halfSize.y, center.z - halfSize.z },
		Vector3{ center.x + halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
		Vector3{ center.x - halfSize.x, center.y + halfSize.y, center.z - halfSize.z },
		Vector3{ center.x - halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
		Vector3{ center.x + halfSize.x, center.y - halfSize.y, center.z + halfSize.z },
		Vector3{ center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
		Vector3{ center.x - halfSize.x, center.y + halfSize.y, center.z + halfSize.z },
	};

	std::array<Vector2, 8> screenCorners{};
	std::array<bool, 8> visible{};
	for (size_t i = 0; i < corners.size(); ++i) {
		visible[i] = ProjectWorldToScreen(corners[i], screenCorners[i]);
	}

	constexpr std::array<std::array<int, 2>, 12> edges = {
		std::array<int, 2>{0, 1}, {1, 2}, {2, 3}, {3, 0},
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7},
	};

	for (const auto& edge : edges) {
		const int a = edge[0];
		const int b = edge[1];
		if (!visible[a] || !visible[b]) {
			continue;
		}

		drawList->AddLine(
			ImVec2(screenCorners[a].x, screenCorners[a].y),
			ImVec2(screenCorners[b].x, screenCorners[b].y),
			color,
			thickness
		);
	}
}

void GamePlayScene::DrawDebugCircleXZ(const Vector3& center, float radius, unsigned int color, float thickness) const {
	ImDrawList* drawList = ImGui::GetForegroundDrawList();
	if (!drawList) {
		return;
	}

	constexpr int segmentCount = 48;
	Vector2 previous{};
	bool hasPrevious = false;
	for (int i = 0; i <= segmentCount; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(segmentCount);
		float angle = t * 6.28318530718f;
		Vector3 worldPos{
			center.x + std::cos(angle) * radius,
			center.y + 0.05f,
			center.z + std::sin(angle) * radius
		};

		Vector2 current{};
		bool visible = ProjectWorldToScreen(worldPos, current);
		if (hasPrevious && visible) {
			drawList->AddLine(
				ImVec2(previous.x, previous.y),
				ImVec2(current.x, current.y),
				color,
				thickness
			);
		}

		previous = current;
		hasPrevious = visible;
	}
}

void GamePlayScene::RegenerateDungeonAndRespawnPlayer(int roomCount) {
	if (!mapManager_) {
		return;
	}

	mapManager_->RegenerateDungeonAndRespawnPlayer(
		roomCount,
		playerManager_.get(),
		enemyManager_.get(),
		bossManager_.get(),
		&spawnManager_,
		&cardPickupManager_,
		camera_.get(),
		playerPos_,
		playerScale_,
		enemySpawnCount_,
		enemySpawnMargin_,
		cardSpawnCount_,
		cardSpawnMargin_
	);
}

bool GamePlayScene::pendingTutorialStart_ = false;
void GamePlayScene::RequestTutorialStart(bool enable) {
	pendingTutorialStart_ = enable;
}

bool GamePlayScene::ConsumeTutorialStartRequest() {
	const bool requested = pendingTutorialStart_;
	pendingTutorialStart_ = false;
	return requested;
}
