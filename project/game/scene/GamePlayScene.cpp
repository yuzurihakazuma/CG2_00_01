#include "GamePlayScene.h"
// --- ゲーム固有のファイル ---
#include "TitleScene.h"

#include <cmath>
#include <algorithm>

// --- エンジン側のファイル ---
#include "Engine/Utils/ImGuiManager.h"
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
#include "Engine/Base/TimeManager.h"
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "engine/graphics/RenderTexture.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/utils/TextManager.h"
#include "Bloom.h"
#include "engine/3d/model/Model.h"
#include "engine/utils/EditorManager.h"
#include "engine/utils/Level/BlenderImporter.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "game/player/Player.h"
#include "engine/rail/SplineRail.h"
#include "engine/utils/Level/LevelManager.h"
#include "engine/utils/Level/LevelEditor.h"


using namespace VectorMath;
using namespace MatrixMath;

// 初期化
void GamePlayScene::Initialize(){
	// べた書きを段階ごとの関数へ。読み込み → カメラ → 装飾物 → ゲーム部品 の順。
	LoadResources();
	SetupCameras();
	SetupDemoObjects();
	SetupGameplay();
}

// BGM・モデル・テクスチャ・Skybox の読み込み
void GamePlayScene::LoadResources(){
	auto commandList = DirectXCommon::GetInstance()->GetCommandList();

	// BGM
	AudioManager::GetInstance()->LoadWave(bgmFile_);

	// モデル
	ModelManager* model = ModelManager::GetInstance();
	model->LoadModel("fence", "resources", "fence.obj");
	model->LoadModel("grass", "resources", "terrain.obj");
	model->LoadModel("block", "resources/block", "block.obj");
	model->CreateSphereModel("sphere", 16);
	model->CreatePlaneModel("plane");
	model->LoadModel("animatedCube", "resources/AnimatedCube", "AnimatedCube.gltf");
	model->LoadModel("human", "resources/human", "walk.gltf");

	// パーティクルグループ
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");
	// ※ 卵の煙／殻の飛び散りは加算パーティクルだと明るい背景で見えないため、
	//    EggSystem 側で実体(Obj3d)の小球として描画する（StompEffect と同じ確実に見える方式）。

	// テクスチャ（短縮版 Load：commandList を渡さなくてよい）
	TextureManager* tex = TextureManager::GetInstance();
	textures_["uvChecker"]     = tex->Load("resources/uvChecker.png");
	textures_["monsterBall"]   = tex->Load("resources/monsterBall.png");
	textures_["fence"]         = tex->Load("resources/fence.png");
	textures_["circle"]        = tex->Load("resources/circle.png");
	textures_["circle2"]       = tex->Load("resources/circle2.png");
	textures_["noise0"]        = tex->Load("Resources/noise0.png");
	textures_["noise1"]        = tex->Load("Resources/noise1.png");
	textures_["gradationLine"] = tex->Load("Resources/gradationLine.png");
	textures_["skybox"]        = tex->LoadCube("resources/StandardCubeMap.dds");

	// Skybox
	Obj3dCommon::GetInstance()->SetEnvironmentTexture(textures_["skybox"].srvIndex);
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize("resources/StandardCubeMap.dds", commandList);

	// アニメーションデータ
	testAnimation_ = LoadAnimationFromFile("resources/AnimatedCube", "AnimatedCube.gltf");
	skinnedAnimTrack_.LoadFromJson("resources/human_anim.json");
}

// メインカメラ／デバッグカメラの生成・登録
void GamePlayScene::SetupCameras(){
	camera_ = Camera::Create(); // ウィンドウサイズ等は内部で自動取得
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });
	// 既定（アクティブ）カメラに設定 → 以降の Obj3d::Create は自動でこのカメラを使う
	Obj3dCommon::GetInstance()->SetDefaultCamera(camera_.get());

	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();
	EditorManager::GetInstance()->SetDebugCamera(debugCamera_.get()); // メニュー「表示」でON/OFF
	EditorManager::GetInstance()->SetCamera(camera_.get());
}

// 装飾/デモ用オブジェクト（testObj・オーラ・円柱オーラ・スキンメッシュ）
void GamePlayScene::SetupDemoObjects(){
	// 回転キューブ（ディゾルブ＋ブルーム）
	testObj_ = Obj3d::Create("animatedCube");
	if ( testObj_ ){
		testObj_->SetEnvironmentMap(textures_["skybox"].srvIndex);
		testObj_->SetPipelineType(PipelineType::Object3D_CullNone);
		testObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });
		testObj_->SetNoiseTexture(textures_["noise0"].srvIndex);
		testObj_->SetDissolveThreshold(0.0f);
		Bloom::GetInstance()->SetTargetEmissivePower(&testObj_->GetModel()->GetMaterial()->emissive);
		EditorManager::GetInstance()->SetGizmoTarget(testObj_.get()); // ギズモ操作対象
	}

	// リングオーラ（地面に広がる魔法陣風）
	ModelManager::GetInstance()->CreateRingModel("auraRing", 32, 1.0f, 0.2f);
	auraObj_ = Obj3d::Create("auraRing");
	if ( auraObj_ ) {
		auraObj_->GetModel()->SetTexture(textures_["gradationLine"].srvIndex);
		auraObj_->SetNoiseTexture(textures_["gradationLine"].srvIndex); // ディゾルブ無効化のダミー
		auraObj_->GetModel()->GetMaterial()->enableLighting = 0;
		auraObj_->SetRotation({ 1.5708f, 0.0f, 0.0f }); // X軸90度で地面に倒す
		auraObj_->SetTranslation({ 0.0f, 0.1f, 5.0f });
		auraObj_->SetPipelineType(PipelineType::Object3D_Additive);
	}

	// 円柱オーラ
	ModelManager::GetInstance()->CreateCylinderModel("auraCylinderModel", 32, 1.5f, 4.0f);
	auraCylinderObj_ = Obj3d::Create("auraCylinderModel");
	if ( auraCylinderObj_ ) {
		auraCylinderObj_->GetModel()->SetTexture(textures_["gradationLine"].srvIndex);
		auraCylinderObj_->SetDissolveThreshold(-1.0f); // 透明化させない
		auraCylinderObj_->GetModel()->GetMaterial()->enableLighting = 0;
		auraCylinderObj_->SetTranslation({ -5.0f, 2.0f, 5.0f });
		auraCylinderObj_->SetPipelineType(PipelineType::Object3D_Additive);
	}

	// スキンメッシュ（プレイ中はプレイヤーの位置/向きに同期）
	skinnedObj_ = SkinnedObj3d::Create("human", "resources/human", "walk.gltf");
	skinnedObj_->SetEnvironmentMap(textures_["skybox"].srvIndex);
	skinnedObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });
	skinnedObj_->SetScale({ 1.0f, 1.0f, 1.0f });
	skinnedObj_->SetRotation({ 0.0f, 3.14159f, 0.0f });
	EditorManager::GetInstance()->SetTargetSkinnedObj(skinnedObj_.get());
}

// プレイヤー・敵エディタ・レール・各種シーン部品の用意
void GamePlayScene::SetupGameplay(){
	WindowProc* windowProc = WindowProc::GetInstance();

	// デプスステンシル
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(), windowProc->GetClientHeight());

	// スプライト・ブロック群・GPUパーティクル
	sprite_ = Sprite::Create(textures_["uvChecker"].srvIndex, spritePos_);

	// 狙い用カーソル（構え中だけ表示。敵に重なると赤＝ロックオン）
	cursorSprite_ = Sprite::Create(textures_["circle2"].srvIndex, { 640.0f, 360.0f });
	cursorSprite_->SetSize({ 56.0f, 56.0f });
	blockGroup_ = std::make_unique<InstancedGroup>();
	blockGroup_->Initialize("block", 10000);
	blockGroup_->SetNoiseTexture(textures_["uvChecker"].srvIndex);

	GPUParticleManager::GetInstance()->Initialize(
		DirectXCommon::GetInstance(), SrvManager::GetInstance(), "resources/uvChecker.png");
	GPUParticleEmitterData emitterData;
	emitterData.position = { 0.0f, 0.0f, 0.0f };
	emitterData.emitRate = 20.0f;
	emitter_.SetData(emitterData);
	EditorManager::GetInstance()->SetParticleEmitter(&emitter_);

	// プレイヤー・敵エディタ
	player_ = std::make_unique<Player>();
	player_->Initialize();
	enemyEditor_ = std::make_unique<EnemyEditor>();
	enemyEditor_->Initialize();

	// レール可視化用モデル（通常=緑 / 穴=赤 の2モデル。マテリアルはモデル単位で共有のため別モデルが必要）
	ModelManager::GetInstance()->CreateCubeModel("railLineCube", 1.0f);
	ModelManager::GetInstance()->CreateCubeModel("railLineCubeHole", 1.0f);
	textures_["white"] = TextureManager::GetInstance()->Load("resources/block/white1x1.png");

	// レールはエディタ保持の最新データから構築（編集・緑線・プレイヤーを同じデータに一本化）
	SyncRailsFromEditor();
}

// レールをエディタ最新へ作り直し、敵も配置し直す。
//   レール本体・緑線・動きは RailField が担当。敵は RailField の責務外なので Sync 後に生成する。
void GamePlayScene::SyncRailsFromEditor(){
	uint32_t whiteTex = 0;
	auto itWhite = textures_.find("white");
	if ( itWhite != textures_.end() ) { whiteTex = itWhite->second.srvIndex; }

	railField_.Sync(camera_.get(), whiteTex); // レール本体＋緑線を作り直す
	SpawnEnemies();                           // 配置テンプレートを元に敵を生成し直す
}

// エディタに配置された敵情報（テンプレート）に基づいて敵の実体を生成する
void GamePlayScene::SpawnEnemies(){
	enemies_.clear();
	if ( !enemyEditor_ ) return;

	const auto& rails = railField_.GetRails();
	const auto& spawns = enemyEditor_->GetSpawnDatas();
	for ( const auto& spawn : spawns ) {
		if ( spawn.railIndex < 0 || spawn.railIndex >= ( int ) rails.size() ) continue;
		if ( rails[spawn.railIndex].nodes.size() < 2 ) continue;

		auto enemy = std::make_unique<Enemy>();
		enemy->Initialize(spawn.type, spawn.railIndex, spawn.distance);
		// dt=0 で Update を呼び、レール上の初期位置を即座に確定させる（原点に巨大球が出るのを防ぐ）
		enemy->Update(rails, 0.0f);
		enemies_.push_back(std::move(enemy));
	}
}

// ヒット時の手応え：一瞬の停止（ヒットストップ）とカメラ揺れをまとめて発生させる
void GamePlayScene::TriggerHitFeel(float stopSeconds, float shakeMag){
	hitStopTimer_  = stopSeconds;
	camShakeTimer_ = 0.22f;
	camShakeMag_   = shakeMag;
}

// 踏みつけ点を中心にしたポストエフェクト（歪みリップル＋スポットグロー）を毎フレーム更新する。
//   fxWorldPos_ をスクリーンUVへ投影し、MaskedDistortion(slot0)＝衝撃波、MaskedGlow(slot1)＝閃光を出す。
void GamePlayScene::UpdateStompPostEffect(){
	PostEffect* pe = PostEffect::GetInstance();
	const PostEffectType kDist = PostEffectType::MaskedDistortion;
	const PostEffectType kGlow = PostEffectType::MaskedGlow;

	auto disableFx = [&](){
		if ( pe->GetEffectActive(kDist) ) pe->SetEffectActive(kDist, false);
		if ( pe->GetEffectActive(kGlow) ) pe->SetEffectActive(kGlow, false);
		};

	if ( fxTimer_ <= 0.0f ) { disableFx(); return; }

	const float kDuration = 0.4f;
	fxTimer_ -= 1.0f / 60.0f; // リアル時間で進める（ヒットストップ中も波は走る）
	float progress = 1.0f - ( fxTimer_ / kDuration );
	progress = std::clamp(progress, 0.0f, 1.0f);

	// ワールド位置 → スクリーンUV（エディタの投影式と同じ。行ベクトル規約 v*VP）
	const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
	const Vector3& w = fxWorldPos_;
	float cw = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
	if ( cw <= 0.0001f ) { disableFx(); return; } // カメラ後方なら出さない
	float cx = w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0];
	float cy = w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1];
	float uvX = cx / cw * 0.5f + 0.5f;
	float uvY = 1.0f - ( cy / cw * 0.5f + 0.5f );

	// 半径アニメ：歪みは外へ広がる衝撃波、グローはパッと出て消えるパルス
	float t = 1.0f - progress;
	float easeOut = 1.0f - t * t;
	float distRadius = 0.06f + ( 0.55f - 0.06f ) * easeOut;
	float glowRadius = 0.45f * std::sin( progress * 3.14159265f );

	// アスペクト比（正円補正）
	float aspect = 16.0f / 9.0f;
	if ( WindowProc* wp = WindowProc::GetInstance() ) {
		int h = wp->GetClientHeight();
		if ( h > 0 ) aspect = static_cast< float >( wp->GetClientWidth() ) / static_cast< float >( h );
	}

	PostEffectMaskParams mp{};
	mp.slot0X = uvX; mp.slot0Y = uvY; mp.slot0Radius = distRadius; // 歪み(MaskedDistortion)
	mp.slot1X = uvX; mp.slot1Y = uvY; mp.slot1Radius = glowRadius; // グロー(MaskedGlow)
	mp.aspectRatio = aspect;
	pe->SetMaskParams(mp);

	pe->SetEffectActive(kDist, true);
	pe->SetEffectActive(kGlow, true);
}

// メインのフレーム更新。べた書きを段階ごとの関数に委譲して見通しを良くした。
void GamePlayScene::Update(){
	UpdateHitStop();              // 踏みつけ等のヒットストップ
	SyncFromEditors();            // エディタ編集（レール／敵／カメラ要求）をシーンへ反映
	UpdateCameraAndPostEffect();  // カメラ更新＋シェイク＋踏みつけポストエフェクト

	EngineMode currentMode = EditorManager::GetInstance()->GetMode();
	HandleModeTransition(currentMode); // Edit↔Play 切替時のリセット（prevMode_ もここで更新）

	if ( currentMode == EngineMode::Play ) {
		UpdatePlayMode();         // プレイ中のゲーム進行
	}

	UpdateSceneVisuals();         // モード問わず毎フレーム行う描画用更新
}

// 踏みつけ等のヒットストップ（一瞬だけ時間を止めて手応えを出す）。リアルなフレームで数える。
void GamePlayScene::UpdateHitStop(){
	if ( hitStopTimer_ > 0.0f ) {
		hitStopTimer_ -= 1.0f / 60.0f;
		Time::GetInstance()->SetTimeScale(0.0f);
		if ( hitStopTimer_ <= 0.0f ) { Time::GetInstance()->SetTimeScale(1.0f); }
	}
}

// エディタ側の編集（レール／敵配置／Blenderカメラ要求）を検知してシーンへ反映する。
void GamePlayScene::SyncFromEditors(){
	EditorManager* em = EditorManager::GetInstance();

	// レールのライブ同期：エディタで編集されたら緑線とプレイヤー用データを即作り直す
	if ( em->GetRailEditVersion() != railField_.Version() ) {
		SyncRailsFromEditor();
	}

	// マップが読み込まれたら、保存済みの敵配置をエディタへ復元する
	if ( enemyEditor_ ) {
		int mlv = em->GetMapLoadVersion();
		if ( mlv != lastMapLoadVersion_ ) {
			lastMapLoadVersion_ = mlv;
			const auto& saved = em->GetEditorEnemyData();
			// マップに敵データがある時だけ復元。無い時は今の配置を保持（リリースでも最低1体出す）。
			if ( !saved.empty() ) {
				std::vector<EnemySpawnData> datas;
				for ( const auto& e : saved ) {
					datas.push_back({ static_cast<EnemyType>( e.type ), e.railIndex, e.distance });
				}
				enemyEditor_->SetSpawnDatas(datas); // changed_ が立つ → 下で SpawnEnemies される
			}
		}
	}

	// 敵配置エディタで追加・削除・編集があったら即リスポーン＆保存用データへ反映
	if ( enemyEditor_ && enemyEditor_->ConsumeChanged() ) {
		std::vector<LevelEnemyData> save;
		for ( const auto& s : enemyEditor_->GetSpawnDatas() ) {
			save.push_back({ static_cast<int>( s.type ), s.railIndex, s.distance });
		}
		em->SetEditorEnemyData(save);
		SpawnEnemies();
	}

	// Blenderインポータからの「カメラに適用」要求を反映
	if ( BlenderImporter* importer = em->GetBlenderImporter() ) {
		Vector3 blCamPos, blCamRot;
		if ( importer->ConsumeCameraRequest(blCamPos, blCamRot) ) {
			camera_->SetTranslation(blCamPos);
			camera_->SetRotation(blCamRot);
		}
	}
}

// カメラ更新（デバッグカメラ＋ヒット時のシェイク）と踏みつけ点中心のポストエフェクト。
void GamePlayScene::UpdateCameraAndPostEffect(){
	if ( debugCamera_ ) { debugCamera_->Update(camera_.get()); }

	// カメラシェイク：ヒット時に一瞬揺らす。前フレームに足した揺れを引いてから今フレームの
	//   揺れを足すので、基準位置を汚さない。
	{
		Vector3 shake { 0.0f, 0.0f, 0.0f };
		if ( camShakeTimer_ > 0.0f ) {
			camShakeTimer_ -= 1.0f / 60.0f;
			if ( camShakeTimer_ < 0.0f ) camShakeTimer_ = 0.0f;
			float m = camShakeMag_ * ( camShakeTimer_ / 0.22f ); // だんだん弱まる
			shake.x = std::sin(camShakeTimer_ * 95.0f) * m;
			shake.y = std::cos(camShakeTimer_ * 120.0f) * m;
		}
		camera_->SetTranslation(camera_->GetWorldPosition() - camPrevShake_ + shake);
		camPrevShake_ = shake;
	}

	camera_->Update();
	UpdateStompPostEffect(); // カメラ確定後にスクリーン投影する
}

// Edit↔Play の切り替わり瞬間のリセット処理。最後に prevMode_ を更新する。
void GamePlayScene::HandleModeTransition(EngineMode current){
	// エディット → プレイ：最新レールで確定し、プレイヤーをスタートへ、エフェクト・卵を消す
	if ( prevMode_ == EngineMode::Edit && current == EngineMode::Play ) {
		SyncRailsFromEditor();
		if ( player_ ) { player_->Initialize(); player_->SetMovementLocked(false); }
		hitEffects_.clear();
		stompEffects_.clear();
		eggSystem_.Initialize();
		throwState_ = ThrowState::Idle;
	}
	// プレイ → エディット：動くレールを基準位置に戻す（編集と表示を一致させる）
	if ( prevMode_ == EngineMode::Play && current == EngineMode::Edit ) {
		railField_.ResetMotion();
	}
	prevMode_ = current;
}

// プレイ中（時間が動いている時）のゲーム進行。
void GamePlayScene::UpdatePlayMode(){
	Input* input = Input::GetInstance();
	float dt = Time::GetInstance()->GetDeltaTime();

	// 動くレール → プレイヤー → 敵 の順で更新（位置の整合のため）
	railField_.UpdateMotion(dt);
	if ( player_ ) { player_->Update(railField_.GetRails()); }
	Vector3 ppos = player_ ? player_->GetPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
	for ( auto& e : enemies_ ) {
		if ( e->IsSwallowing() ) { e->TickSwallow(ppos, dt); } // 縮みながらプレイヤーへ吸い込まれる
		else { e->Update(railField_.GetRails(), dt); }
	}
	// 吸い込み完了 → お腹に+1して消す（口元で緑のヨッシー演出）
	for ( auto& e : enemies_ ) {
		if ( e->IsConsumed() ) {
			eggSystem_.AddToBelly();
			auto fx = std::make_unique<StompEffect>();
			fx->Initialize(e->GetPosition(), camera_.get(), textures_["circle"].srvIndex, textures_["skybox"].srvIndex, StompEffectType::Swallow);
			stompEffects_.push_back(std::move(fx));
		}
	}
	enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
		[](const std::unique_ptr<Enemy>& e){ return e->IsConsumed(); }), enemies_.end());

	// 当たり判定＋踏みつけ
	UpdateStompCollision();

	// Eキーで近くの敵を飲み込む（卵にする）
	UpdateSwallow();

	// Q長押しで構え→矢印で狙う→離して投げる（構え中はプレイヤーが止まる）
	UpdateThrowAim();

	// 飛行中の卵 → 敵の当たり判定（当たったら敵を倒して卵を割る。割れ演出は卵の Update が出す）
	eggSystem_.ResolveHits([&](const Vector3& eggPos, float eggR) -> bool {
		for ( auto& e : enemies_ ) {
			if ( !e->IsAlive() ) continue;
			float reach = eggR + e->GetRadius();
			if ( Length(eggPos - e->GetPosition()) <= reach ) {
				Vector3 ep = e->GetPosition();
				e->Defeat();
				TriggerHitFeel(0.05f, 0.2f); // 命中の手応え
				auto fx = std::make_unique<StompEffect>();
				fx->Initialize(ep, camera_.get(), textures_["circle"].srvIndex, textures_["skybox"].srvIndex, StompEffectType::EggHit);
				stompEffects_.push_back(std::move(fx));
				return true; // 命中（殻の飛び散りは卵の割れ演出が出す）
			}
		}
		return false;
	});

	// 卵の追従・飛行・割れの更新
	if ( player_ ) {
		Vector3 ppos = player_->GetPosition();
		float yaw = player_->GetRotation().y;
		Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) };
		eggSystem_.Update(ppos, facing, dt);
	}

	// スペースキー：エフェクト発生＋BGM再生
	if ( input->Triggerkey(DIK_SPACE) ) {
		AudioManager::GetInstance()->PlayWave(bgmFile_);
		auto newEffect = std::make_unique<HitEffect>();
		newEffect->Initialize({ 5.0f, 0.0f, 5.0f }, camera_.get(), textures_["circle2"].srvIndex, textures_["skybox"].srvIndex);
		hitEffects_.push_back(std::move(newEffect));
	}
	// パーティクル発生
	if ( input->Triggerkey(DIK_P) ) {
		ParticleManager::GetInstance()->Emit("Circle", { 0.0f, 0.0f, 0.0f }, 10);
	}

	// アニメーションの進行
	if ( skinnedObj_ && skinnedAnimTrack_.duration > 0.0f ) {
		skinnedAnimTime_ += 1.0f / 60.0f;
		if ( skinnedAnimTime_ > skinnedAnimTrack_.duration ) { skinnedAnimTime_ = 0.0f; }
	}

	// エフェクトの更新と死んだものの削除（stompEffect は timeScale 適用 dt → ヒットストップで一緒に止まる）
	for ( auto& effect : hitEffects_ ) { effect->Update(); }
	hitEffects_.remove_if([](const std::unique_ptr<HitEffect>& e){ return e->IsDead(); });
	for ( auto& effect : stompEffects_ ) { effect->Update(dt); }
	stompEffects_.remove_if([](const std::unique_ptr<StompEffect>& e){ return e->IsDead(); });
}

// プレイヤーと敵の球当たり判定＋踏みつけ演出（ヒットストップ・シェイク・ポスト・エフェクト生成）。
void GamePlayScene::UpdateStompCollision(){
	if ( !player_ ) return;
	Vector3 playerPos = player_->GetPosition();
	const float playerRadius = 0.5f; // プレイヤーの球体当たり判定半径

	for ( auto& enemy : enemies_ ) {
		if ( !enemy->IsAlive() ) continue;

		Vector3 enemyPos = enemy->GetPosition();
		float enemyRadius = enemy->GetRadius();
		float dist = Length(playerPos - enemyPos); // using namespace VectorMath

		if ( dist >= ( playerRadius + enemyRadius ) ) continue; // 接触してなければスキップ

		// 踏みつけ成立：1.接地していない（空中）かつ 2.プレイヤーが敵より上
		if ( !player_->IsGrounded() && playerPos.y > enemyPos.y + 0.1f ) {
			enemy->Defeat();
			player_->Bounce();
			TriggerHitFeel(0.06f, 0.28f);       // 一瞬停止＋カメラ揺れ
			fxTimer_    = 0.4f;                  // 踏んだ点中心のポストエフェクト起動
			fxWorldPos_ = enemyPos;

			auto newStompEffect = std::make_unique<StompEffect>();
			newStompEffect->Initialize(enemyPos, camera_.get(), textures_["circle"].srvIndex, textures_["skybox"].srvIndex, StompEffectType::Stomp);
			stompEffects_.push_back(std::move(newStompEffect));
		}
	}
}

// ヨッシーの「飲み込む／産む」：
//   ・E      … 一番近い敵の「飲み込み」を開始（敵が縮みながらプレイヤーへ吸い込まれ → お腹に入る）。
//   ・左Ctrl … しゃがんでお腹の敵を1匹「卵」として後ろに産む（産んだ卵は投げられる）。
//   敵を知るのはシーンなので判定はここで行う（吸い込み演出は Enemy 側、消化はUpdatePlayMode）。
void GamePlayScene::UpdateSwallow(){
	if ( !player_ ) return;
	Input* input = Input::GetInstance();
	Vector3 playerPos = player_->GetPosition();

	// --- E：飲み込み開始（縮小吸い込みアニメ。卵にはまだしない）---
	if ( input->Triggerkey(DIK_E) ) {
		const float kSwallowReach = 2.0f; // 舌の届く範囲
		Enemy* target = nullptr;
		float bestDist = kSwallowReach;
		for ( auto& e : enemies_ ) {
			if ( !e->IsAlive() ) continue;
			float d = Length(playerPos - e->GetPosition());
			if ( d < bestDist ) { bestDist = d; target = e.get(); }
		}
		if ( target ) {
			target->StartSwallow();      // 縮みながらプレイヤーへ（完了で UpdatePlayMode がお腹+1）
			TriggerHitFeel(0.03f, 0.1f); // 軽い手応え
		}
	}

	// --- 左Ctrl（しゃがみ）：お腹の敵を1匹、後ろに卵として産む ---
	if ( input->Triggerkey(DIK_LCONTROL) ) {
		float yaw = player_->GetRotation().y;
		Vector3 behind = { playerPos.x - std::sin(yaw) * 0.8f, playerPos.y + 0.3f, playerPos.z - std::cos(yaw) * 0.8f };
		if ( eggSystem_.LayEgg(behind) ) {
			auto fx = std::make_unique<StompEffect>(); // 産まれた合図（緑のヨッシー演出）
			fx->Initialize(behind, camera_.get(), textures_["circle"].srvIndex, textures_["skybox"].srvIndex, StompEffectType::Swallow);
			stompEffects_.push_back(std::move(fx));
			TriggerHitFeel(0.03f, 0.08f);
		}
	}
}

// ヨッシー風の投げ：Q長押しで構え、矢印で「画面上のカーソル」を直感的に動かし、離して投げる。
//   ・カーソルは画面座標で動く（上=上 / 右=右）。敵に近いと少し吸いつく（外せば再ロック可）。
//   ・ロック中はその敵へ。未ロックはカーソルの先(奥)へ投げる（クラフトワールド風）。
void GamePlayScene::UpdateThrowAim(){
	if ( !player_ ) return;
	Input* input = Input::GetInstance();
	float dt = Time::GetInstance()->GetDeltaTime();
	Vector3 ppos = player_->GetPosition();

	const float W = ( float ) WindowProc::GetInstance()->GetClientWidth();
	const float H = ( float ) WindowProc::GetInstance()->GetClientHeight();

	// ワールド点 → スクリーン画素（行ベクトル v*VP）。カメラ後方なら false。
	auto project = [&](const Vector3& w, float& px, float& py) -> bool {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		float cw = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
		if ( cw <= 0.0001f ) return false;
		float sx = ( w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0] ) / cw;
		float sy = ( w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1] ) / cw;
		px = ( sx * 0.5f + 0.5f ) * W;
		py = ( 1.0f - ( sy * 0.5f + 0.5f ) ) * H;
		return true;
		};

	if ( throwState_ == ThrowState::Idle ) {
		// Q を押し始めた＆地上＆卵を持っている → 構えに入る
		if ( input->Pushkey(DIK_Q) && player_->IsGrounded() && eggSystem_.HeldCount() > 0 ) {
			throwState_ = ThrowState::Aiming;
			// 構え中も移動・ジャンプは受け付ける（狙いは矢印キーで別操作なので競合しない）
			// カーソルの初期位置：プレイヤーの少し前方上をスクリーン投影（無理なら画面中央）
			float px, py;
			Vector3 facing = { std::sin(player_->GetRotation().y), 0.0f, std::cos(player_->GetRotation().y) };
			Vector3 ahead = { ppos.x + facing.x * 5.0f, ppos.y + 1.0f, ppos.z + facing.z * 5.0f };
			if ( project(ahead, px, py) ) { cursorX_ = px; cursorY_ = py; }
			else { cursorX_ = W * 0.5f; cursorY_ = H * 0.45f; }
			// ★1f点滅対策：入場フレームのうちにカーソル位置を確定（return せず下の処理へ落ちる）
			if ( cursorSprite_ ) { cursorSprite_->SetPosition({ cursorX_, cursorY_ }); cursorSprite_->Update(); }
		} else {
			return; // 構えていない時は何もしない
		}
	}

	// --- 構え中（Aiming）---
	// 矢印キーで「画面上のカーソル」を動かす（直感的：上=上 / 右=右 / 等速）。
	const float kCursorSpeed = 620.0f; // px/s
	if ( input->Pushkey(DIK_UP) )    cursorY_ -= kCursorSpeed * dt;
	if ( input->Pushkey(DIK_DOWN) )  cursorY_ += kCursorSpeed * dt;
	if ( input->Pushkey(DIK_LEFT) )  cursorX_ -= kCursorSpeed * dt;
	if ( input->Pushkey(DIK_RIGHT) ) cursorX_ += kCursorSpeed * dt;
	cursorX_ = std::clamp(cursorX_, 0.0f, W);
	cursorY_ = std::clamp(cursorY_, 0.0f, H);

	// --- ロックオン対象を探す：カーソル(自由位置)に画面上で一番近い敵（吸いつき無し）---
	Enemy* lockEnemy = nullptr;
	float  bestPix = 1e9f, bestEx = 0.0f, bestEy = 0.0f;
	for ( auto& e : enemies_ ) {
		if ( !e->IsAlive() ) continue;
		float ex, ey;
		if ( !project(e->GetPosition(), ex, ey) ) continue; // 後方は対象外
		float dpix = std::sqrt(( ex - cursorX_ ) * ( ex - cursorX_ ) + ( ey - cursorY_ ) * ( ey - cursorY_ ));
		if ( dpix < bestPix ) { bestPix = dpix; lockEnemy = e.get(); bestEx = ex; bestEy = ey; }
	}
	float lockThresh = aimLocked_ ? 120.0f : 80.0f; // 粘り（付く<外れる）
	aimLocked_ = ( lockEnemy != nullptr && bestPix < lockThresh );

	Vector3 origin = { ppos.x, ppos.y + 0.5f, ppos.z };
	Vector3 throwDir = { 0.0f, 0.0f, 1.0f };
	float   throwSpeed = 13.0f;  // 通常の投げ速度
	float   dispX = cursorX_, dispY = cursorY_;
	Vector3 cursorWorld;         // 狙い線の先端（奥に追従させる）

	if ( aimLocked_ ) {
		// ロック中：その敵へ。命中しやすいよう速度を上げる。カーソルは敵にピタッと合わせる。
		dispX = bestEx; dispY = bestEy;
		cursorWorld = lockEnemy->GetPosition();
		Vector3 t = cursorWorld - origin;
		float d = Length(t);
		if ( d > 1e-4f ) throwDir = { t.x / d, t.y / d, t.z / d };
		throwSpeed = 22.0f; // ★敵ロック時は速く
		DebugDraw::GetInstance()->Sphere(cursorWorld, lockEnemy->GetRadius() + 0.25f, { 1.0f, 0.2f, 0.2f, 1.0f }, 16);
	} else {
		// 未ロック：カーソルの画面位置を奥へアンプロジェクトした方向へ投げる（奥に投げ込める）。
		Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
		float ndcX = cursorX_ / W * 2.0f - 1.0f;
		float ndcY = 1.0f - cursorY_ / H * 2.0f;
		auto unproj = [&](float z) -> Vector3 {
			float ow = ndcX * invVP.m[0][3] + ndcY * invVP.m[1][3] + z * invVP.m[2][3] + invVP.m[3][3];
			return { ( ndcX * invVP.m[0][0] + ndcY * invVP.m[1][0] + z * invVP.m[2][0] + invVP.m[3][0] ) / ow,
			         ( ndcX * invVP.m[0][1] + ndcY * invVP.m[1][1] + z * invVP.m[2][1] + invVP.m[3][1] ) / ow,
			         ( ndcX * invVP.m[0][2] + ndcY * invVP.m[1][2] + z * invVP.m[2][2] + invVP.m[3][2] ) / ow };
			};
		Vector3 nearP = unproj(0.0f), farP = unproj(1.0f);
		Vector3 ray = { farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z };
		float rl = Length(ray);
		if ( rl > 1e-4f ) throwDir = { ray.x / rl, ray.y / rl, ray.z / rl };
		// 狙い線の先端＝カーソル方向の少し奥（奥に動かすと線もそちらへ追従する）
		cursorWorld = { origin.x + throwDir.x * 12.0f, origin.y + throwDir.y * 12.0f, origin.z + throwDir.z * 12.0f };
	}

	// 狙い線：プレイヤー → カーソルの先端（奥に合わせると線もそちらへ伸びる）
	DebugDraw::GetInstance()->Line(origin, cursorWorld, { 1.0f, 0.9f, 0.2f, 0.9f });

	// カーソル表示
	if ( cursorSprite_ ) {
		cursorSprite_->SetPosition({ dispX, dispY });
		cursorSprite_->SetSize(aimLocked_ ? Vector2{ 64.0f, 64.0f } : Vector2{ 48.0f, 48.0f });
		cursorSprite_->SetColor(aimLocked_ ? Vector4{ 1.0f, 0.25f, 0.2f, 1.0f }
		                                   : Vector4{ 1.0f, 1.0f, 1.0f, 0.85f });
		cursorSprite_->Update();
	}

	// Q を離した瞬間 → 投げる（ロック中=敵へ速く / 未ロック=カーソルの奥へ）。
	//   ※「投げずにキャンセル」したい時用のコマンドは別途キーで足せる（今は常に投げる）。
	if ( !input->Pushkey(DIK_Q) ) {
		// 投げる方向へプレイヤーの向きも合わせる（水平成分のyaw）
		if ( std::abs(throwDir.x) > 1e-4f || std::abs(throwDir.z) > 1e-4f ) {
			float yaw = std::atan2(throwDir.x, throwDir.z);
			Vector3 r = player_->GetRotation();
			player_->SetRotation({ r.x, yaw, r.z });
		}
		eggSystem_.TryThrow(ppos, throwDir, throwSpeed);
		throwState_ = ThrowState::Idle;
		aimLocked_  = false;
	}
}

// モードに関わらず毎フレーム行う描画用の更新（オーラ・各種オブジェクト・マーカー・パーティクル等）。
void GamePlayScene::UpdateSceneVisuals(){
	Input* input = Input::GetInstance();
	EngineMode currentMode = EditorManager::GetInstance()->GetMode();

	// 円柱オーラ（UVスクロール）
	if ( auraCylinderObj_ ) {
		auraCylinderScroll_ += 1.0f * ( 1.0f / 60.0f );
		if ( auraCylinderScroll_ > 1.0f ) { auraCylinderScroll_ -= 1.0f; }
		auraCylinderObj_->GetModel()->GetMaterial()->uvTransform = MakeTranslate({ 0.0f, auraCylinderScroll_, 0.0f });
		auraCylinderObj_->Update();
	}

	// タイトルへ戻る
	if ( input->Triggerkey(DIK_T) ) {
		SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
	}

	// 3Dオブジェクト・レールマーカーの行列更新
	for ( auto& obj : object3ds_ ) { obj->Update(); }
	if ( testObj_ ){ testObj_->Update(); }
	railField_.UpdateMarkers(); // レール緑線（カメラ移動に追従）

	// リングオーラ（UVスクロール）
	if ( auraObj_ ) {
		auraUvScrollOffset_ += 1.0f * ( 1.0f / 60.0f );
		if ( auraUvScrollOffset_ > 1.0f ) { auraUvScrollOffset_ -= 1.0f; }
		auraObj_->GetModel()->GetMaterial()->uvTransform = MakeTranslate({ 0.0f, auraUvScrollOffset_, 0.0f });
		auraObj_->Update();
	}

	// スキンメッシュ（プレイ中はプレイヤーの位置/向きに同期）
	if ( skinnedObj_ ) {
		Vector3 pos, rot, scale;
		skinnedAnimTrack_.UpdateTransformAtTime(skinnedAnimTime_, pos, rot, scale);
		if ( currentMode == EngineMode::Play && player_ ) {
			pos = player_->GetPosition();
			rot = player_->GetRotation();
		}
		skinnedObj_->SetTranslation(pos);
		skinnedObj_->SetRotation(rot);
		skinnedObj_->SetScale(scale);
		skinnedObj_->Update();
	}

	if ( sprite_ ) { sprite_->Update(); }
	PostEffect::GetInstance()->Update();
	for ( auto& block : blocks_ ) { block->Update(); }

	// パーティクル
	ParticleManager::GetInstance()->Update(camera_.get());
	if ( input->Triggerkey(DIK_G) ){
		GPUParticleManager::GetInstance()->Update(1.0f / 60.0f, camera_.get());
	}
	emitter_.Update(1.0f / 60.0f);

	if ( blockGroup_ ) { blockGroup_->Update(blocks_); }
}



void GamePlayScene::Draw(){
	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList();

	GPUParticleManager::GetInstance()->Dispatch(commandList);


	// 1. 【MRT開始】
	PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

	// --- 3D描画の前準備 ---
	Obj3dCommon::GetInstance()->PreDraw(commandList);

	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(9, textures_["skybox"].srvIndex);

	// 1. 先に「不透明」なものを全部描き切る！！！
	for ( auto& obj : object3ds_ ) { obj->Draw(); }
	if ( testObj_ ){ testObj_->Draw(); }
	if ( skinnedObj_ ) { skinnedObj_->Draw(); }
	for ( auto& e : enemies_ ) { e->Draw(); }   // 敵
	eggSystem_.Draw();                          // ヨッシーの卵

	// レール経路の可視化マーカー（プレイヤーが通る道筋）
	railField_.DrawMarkers();

	EditorManager::GetInstance()->Draw();

	// --- インスタンシングの3D描画 ---
	if ( blockGroup_ ) { blockGroup_->Draw(camera_.get()); }

	/*if ( skybox_ ) {
		skybox_->Draw(commandList, camera_.get());
	}*/

	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	// ⭕️ 3. 最後に「透明・加算合成」のものを描く！！！（順番超大事）
	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	if ( auraCylinderObj_ ) {
		auraCylinderObj_->Draw();
	}

	if ( auraObj_ ) {
		auraObj_->Draw();
	}

	// --- インスタンシングの3D描画 ---
	//if ( blockGroup_ ) { blockGroup_->Draw(camera_.get()); }



	for ( auto& effect : hitEffects_ ) {
		effect->Draw();
	}
	for ( auto& effect : stompEffects_ ) {
		effect->Draw();
	}

	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);


	// 2. 【MRT終了】
	// デバッグ描画：MRT（シーンRT）内で線を描く → ポストエフェクト/Bloomを通って
	//   Game View にも単体表示にも反映される（深度テストありで3D形状に隠れる）。
	if ( showDebugGrid_ ) {
		DebugDraw::GetInstance()->Grid(20.0f, 1.0f, { 0.3f, 0.3f, 0.35f, 0.5f }, 0.0f);
	}
	DebugDraw::GetInstance()->Render(camera_.get());

	// 2. 【MRT終了】3Dの描画が終わったので、2枚のキャンバスを読み込みモードに戻す
	PostEffect::GetInstance()->PostDrawSceneMRT(commandList);


	// 3. ポストエフェクト処理（バックバッファへの最終出力は FinalBlit に任せる）
	PostEffect::GetInstance()->Draw(commandList, false);

	// 4. SRV番号取得
	uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
	uint32_t maskSrv  = PostEffect::GetInstance()->GetMaskSrvIndex();

	// 5. Bloomに「色」と「マスク」を両方渡す
	Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
	uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

	// エディタに最終的なゲーム画面のSRVを渡す（Game View 表示用）
	EditorManager::GetInstance()->SetGameViewSrvIndex(finalSrv);

	// 6. 最終結果をバックバッファへ書き出す（RTV リセット込み）
	//    エディタアクティブ時はRTVのセットのみ行い描画はスキップする
	PostEffect::GetInstance()->FinalBlit(commandList, finalSrv, EditorManager::GetInstance()->IsActive());

	// --- スプライト・UI描画 ---
	SpriteCommon::GetInstance()->PreDraw(commandList);
	if ( sprite_ ) { sprite_->Draw(); }
	if ( cursorSprite_ && throwState_ == ThrowState::Aiming ) { cursorSprite_->Draw(); } // 構え中だけ狙いカーソル
	TextManager::GetInstance()->Draw();


}

void GamePlayScene::DrawDebugUI(){

#ifdef USE_IMGUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

	// --- ヨッシーHUD（仮）：画面左上に常時表示。卵の数・状態を確認できる ---
	{
		ImGui::SetNextWindowPos(ImVec2(20.0f, 60.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.55f);
		ImGui::Begin("ヨッシーHUD", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);

		ImGui::Text("おなか %d   たまご %d / %d", eggSystem_.BellyCount(), eggSystem_.HeldCount(), EggSystem::kMaxEggs);
		ImGui::SameLine();
		ImGui::TextDisabled("(飛行中:%d)", eggSystem_.FlyingCount());

		if ( throwState_ == ThrowState::Aiming ) {
			ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "▼ 構え中！ 矢印でカーソル移動 / Q を離す");
			if ( aimLocked_ ) { ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "   ★ ロックオン！ Q を離すと命中"); }
			else              { ImGui::TextDisabled("   敵にカーソルを重ねるとロック（重ねないと投げない）"); }
		} else {
			ImGui::Text("E:飲み込む   左Ctrl:しゃがんで産む   Q長押し:構える→投げる");
		}
		ImGui::End();
	}

	// レール経路の可視化トグル（共有の「詳細設定」ウィンドウに合流させる）
	ImGui::Begin("インスペクター (詳細設定)");

	if ( ImGui::CollapsingHeader("レール表示・カメラ視点 (Rail Debug)") ) {
	bool showMarkers = railField_.ShowMarkers();
	if ( ImGui::Checkbox("レール経路を表示", &showMarkers) ) { railField_.SetShowMarkers(showMarkers); }
	ImGui::Text("マーカー数: %d", railField_.MarkerCount());
	if ( ImGui::Button("マーカー再構築") ) { railField_.RebuildMarkers(); }

	// --- カメラ視点プリセット（レールを編集しやすく）---
	ImGui::Separator();
	ImGui::TextDisabled("カメラ視点プリセット:");
	if ( ImGui::Button("トップビュー（真上から）") && camera_ ) {
		// レール全体のXZ範囲を求めて、真上から全体が収まる高さに置く
		bool has = false;
		float minx = 0, maxx = 0, miny = 0, maxy = 0, minz = 0, maxz = 0;
		for ( const auto& rail : railField_.GetRails() ) {
			for ( const auto& n : rail.nodes ) {
				if ( !has ) { minx = maxx = n.x; miny = maxy = n.y; minz = maxz = n.z; has = true; } else {
					if ( n.x < minx ) minx = n.x; if ( n.x > maxx ) maxx = n.x;
					if ( n.y < miny ) miny = n.y; if ( n.y > maxy ) maxy = n.y;
					if ( n.z < minz ) minz = n.z; if ( n.z > maxz ) maxz = n.z;
				}
			}
		}
		float cx = 0, cy = 0, cz = 0, ext = 10.0f;
		if ( has ) {
			cx = ( minx + maxx ) * 0.5f; cy = ( miny + maxy ) * 0.5f; cz = ( minz + maxz ) * 0.5f;
			float ex = maxx - minx, ez = maxz - minz;
			ext = ( ex > ez ) ? ex : ez;
		}
		float dist = ext * 1.5f + 8.0f; // 全体が画面に収まる高さ（足りなければホイールでズーム）
		camera_->SetTranslation({ cx, cy + dist, cz });
		camera_->SetRotation({ 1.5707964f, 0.0f, 0.0f }); // pitch 90°=真下を向く（X=右, Z=上）
	}
	ImGui::SameLine();
	if ( ImGui::Button("斜め視点に戻す") && camera_ ) {
		camera_->SetTranslation({ 0.0f, 6.0f, -15.0f });
		camera_->SetRotation({ 0.30f, 0.0f, 0.0f });
	}
	ImGui::TextDisabled("※デバッグカメラONなら右ドラッグで自由に回せます");
	} // CollapsingHeader: レール表示・カメラ視点

	// デバッグ描画（DebugDraw）の表示設定 — 同じ「詳細設定」窓に合流
	if ( ImGui::CollapsingHeader("デバッグ描画 (DebugDraw)") ) {
		ImGui::Checkbox("グリッドを表示", &showDebugGrid_);
		ImGui::TextDisabled("Box/Sphere/Line はコードから積む。Game View にも表示されます");
	}
	ImGui::End();

	// 敵配置用エディタのUIウィンドウを描画
	if ( enemyEditor_ ) {
		enemyEditor_->DrawWindow(railField_.GetRails());
	}

#endif

}

void GamePlayScene::ReloadMap(){}

GamePlayScene::GamePlayScene(){}

GamePlayScene::~GamePlayScene(){}
// 終了
void GamePlayScene::Finalize(){
	// エディタが保持している外部ポインタをリセット（ダングリングポインタ防止）
	EditorManager::GetInstance()->ResetSceneReferences();

	object3ds_.clear();
	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}