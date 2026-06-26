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
	// エディット → プレイ：最新レールで確定し、プレイヤーをスタートへ、エフェクトを消す
	if ( prevMode_ == EngineMode::Edit && current == EngineMode::Play ) {
		SyncRailsFromEditor();
		if ( player_ ) { player_->Initialize(); }
		hitEffects_.clear();
		stompEffects_.clear();
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
	for ( auto& e : enemies_ ) { e->Update(railField_.GetRails(), dt); }

	// 当たり判定＋踏みつけ
	UpdateStompCollision();

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
			newStompEffect->Initialize(enemyPos, camera_.get(), textures_["circle"].srvIndex, textures_["skybox"].srvIndex);
			stompEffects_.push_back(std::move(newStompEffect));
		}
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
	TextManager::GetInstance()->Draw();


}

void GamePlayScene::DrawDebugUI(){

#ifdef USE_IMGUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

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