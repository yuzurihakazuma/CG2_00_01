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
#include "engine/sdf/SDFManager.h"
#include "engine/sdf/SDFVolumeObject.h"
#include "engine/sdf/SDFText.h"
#include "engine/utils/Level/BlenderImporter.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "game/player/Player.h"
#include "engine/rail/SplineRail.h"
#include "engine/utils/Level/LevelManager.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/utils/Level/RailEditor.h" // カメラプレビュー要求の受け取りに使う


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

	// 卵アクションのSE（投げ/命中/割れ）。自前生成のプレースホルダ音源（後で差し替え可）
	AudioManager::GetInstance()->LoadWave("resources/se/eggThrow.wav");
	AudioManager::GetInstance()->LoadWave("resources/se/eggHit.wav");
	AudioManager::GetInstance()->LoadWave("resources/se/eggBreak.wav");

	// モデル
	ModelManager* model = ModelManager::GetInstance();
	model->LoadModel("fence", "resources", "fence.obj");
	model->LoadModel("grass", "resources", "terrain.obj");
	model->LoadModel("block", "resources/block", "block.obj");
	model->CreateSphereModel("sphere", 16);
	model->CreatePlaneModel("plane");
	model->LoadModel("animatedCube", "resources/AnimatedCube", "AnimatedCube.gltf");
	model->LoadModel("human", "resources/human", "walk.gltf");
	model->LoadModel("egg", "resources/egg", "egg.obj"); // ヨッシーの卵（専用モデル。sphereの使い回しをやめる）
	model->CreateEggShellModel("eggShell", 0.3f);        // 卵の殻の欠片（頂点から手作り＝エンジン側で完結。割れ演出用）

	// 汎用パーティクル用の粒（"sphere" は敵と共有＋モンスターボール柄がデフォルトなので、
	//   色を付けるだけの粒には専用の白い球を使う。これも手作りモデル＋既存の白テクスチャだけで完結）。
	model->CreateSphereModel("fxSphere", 8);
	if ( auto* fxModel = model->FindModel("fxSphere") ) {
		fxModel->SetTexture("resources/block/white1x1.png");
	}

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

	// SDFボリューム（egg.obj → SDF3DPrintf.py で焼いた距離場）。ポリゴンは箱12枚だけで
	// 形はレイマーチングが作る＝メッシュ不要の超軽量表示のデモ。看板の横に置く。
	sdfEggObj_ = std::make_unique<SDFVolumeObject>();
	if ( sdfEggObj_->Load("resources/sdf3d/egg.sdf3d", commandList) ) {
		sdfEggObj_->SetTranslation({ 4.0f, 3.0f, 3.0f });
		sdfEggObj_->SetScale(2.0f);
		sdfEggObj_->SetColor({ 0.98f, 0.96f, 0.86f, 1.0f }); // 卵のクリーム色
	}

	// 産卵エロージョン演出（左Ctrlで産む瞬間、SDFの卵が芯から育って実体メッシュに交代）
	eggSystem_.InitializeBirthFx(commandList);
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

	// 狙い用カーソル（構え中だけ表示。敵に重なると赤＝ロックオン。AimThrowController が所有）
	aimThrow_.Initialize(textures_["circle2"].srvIndex);

	// 卵保持数HUD（左上）：上段=保持卵スロット6個 / 下段=お腹にためた数（小さい丸）
	eggHudSlots_.clear();
	bellyHudIcons_.clear();
	for ( int i = 0; i < EggSystem::kMaxEggs; ++i ) {
		auto s = Sprite::Create(textures_["circle2"].srvIndex, { 36.0f + i * 42.0f, 34.0f });
		s->SetSize({ 32.0f, 32.0f });
		eggHudSlots_.push_back(std::move(s));

		auto b = Sprite::Create(textures_["circle2"].srvIndex, { 36.0f + i * 42.0f, 72.0f });
		b->SetSize({ 16.0f, 16.0f });
		bellyHudIcons_.push_back(std::move(b));
	}

	// 卵保持数の数字（SDFText。アイコン列の右に「×N（＋お腹）」。小さくてもフチ付きで潰れない）
	eggCountText_ = std::make_unique<SDFText>();
	eggCountText_->Initialize();
	eggCountText_->SetFontSize(34.0f);
	eggCountText_->SetPosition(36.0f + EggSystem::kMaxEggs * 42.0f + 10.0f, 16.0f);
	eggCountText_->SetColor({ 0.75f, 1.0f, 0.8f, 1.0f });
	eggCountText_->SetOutlineWidth(0.22f);
	eggCountText_->SetOutlineColor({ 0.05f, 0.12f, 0.05f, 1.0f });
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

	// 戦闘（踏みつけ/卵命中）＋ヒット演出（エフェクト用テクスチャを渡す）
	combat_.Initialize(textures_["circle"].srvIndex, textures_["skybox"].srvIndex);

	// --- ノードエディタの「→ ゲーム値」に、今作っているゲームの調整値を登録する ---
	//   ノードグラフから接続すると、プレイ中の挙動をリアルタイムに動かせる。
	//   ※ポインタ登録なので、シーン終了時（Finalize）に必ず解除する。
	{
		EditorManager* em = EditorManager::GetInstance();
		em->ClearNodeGameValues(); // シーン再初期化時の二重登録防止
		em->RegisterNodeGameValue("プレイヤー移動速度",   player_->MoveSpeedPtr(),  0.5f, 20.0f);
		em->RegisterNodeGameValue("ジャンプ力",           player_->JumpPowerPtr(),  1.0f, 20.0f);
		em->RegisterNodeGameValue("卵の投げ初速",         aimThrow_.ThrowSpeedNormalPtr(), 1.0f, 40.0f);
		em->RegisterNodeGameValue("ロックオン投げ初速",   aimThrow_.ThrowSpeedLockPtr(),   1.0f, 60.0f);
		em->RegisterNodeGameValue("飲み込みの届く距離",   swallow_.SwallowReachPtr(), 0.5f, 10.0f);
		em->RegisterNodeGameValue("飲み込みクールタイム", swallow_.SwallowCooldownPtr(), 0.0f, 2.0f);
	}

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

	// --- 敵のピン留め準備：編集前のレールから各敵のワールド位置を覚えておく ---
	//   敵は「レール番号＋距離(m)」で置かれているため、レールを編集すると全長が伸縮し、
	//   触っていない敵まで距離ぶんズルズル滑ってしまう（＝レール編集に追従する問題）。
	//   → 編集前の位置を記録し、編集後に「その位置へ一番近い点」に距離を張り直して固定する。
	//   ※マップ読込直後のフレームは対象外：今の spawnDatas_ は旧マップの敵なので、
	//     張り直して保存データへ書き戻すと読込した新マップの敵配置を潰してしまう。
	const bool mapLoadPending =
		( EditorManager::GetInstance()->GetMapLoadVersion() != lastMapLoadVersion_ );
	std::vector<Vector3> oldEnemyPos;
	std::vector<char>    oldEnemyValid;
	if ( enemyEditor_ && !mapLoadPending ) {
		const auto& oldRails = railField_.GetRails();
		for ( const auto& s : enemyEditor_->GetSpawnDatas() ) {
			bool ok = ( s.railIndex >= 0 && s.railIndex < ( int ) oldRails.size()
			            && oldRails[s.railIndex].nodes.size() >= 2 );
			oldEnemyValid.push_back(ok ? 1 : 0);
			oldEnemyPos.push_back(ok ? oldRails[s.railIndex].GetPositionByDistance(s.distance)
			                         : Vector3 { 0.0f, 0.0f, 0.0f });
		}
	}

	railField_.Sync(camera_.get(), whiteTex); // レール本体＋緑線を作り直す

	// --- 敵のピン留め：編集後のレール上で「元のワールド位置の最寄り点」へ距離を張り直す ---
	//   路線まるごと移動なら一緒に付いていき、形の部分編集なら他の敵は動かない。
	if ( enemyEditor_ && !mapLoadPending ) {
		auto& datas = enemyEditor_->MutableSpawnDatas();
		const auto& newRails = railField_.GetRails();
		for ( size_t i = 0; i < datas.size() && i < oldEnemyValid.size(); ++i ) {
			if ( !oldEnemyValid[i] ) continue;
			auto& s = datas[i];
			if ( s.railIndex < 0 || s.railIndex >= ( int ) newRails.size() ) continue;
			if ( newRails[s.railIndex].nodes.size() < 2 ) continue;
			s.distance = newRails[s.railIndex].GetClosestDistance(oldEnemyPos[i]);
		}
		// マップ保存用データにも張り直した距離を反映（保存した時に位置がズレないように）
		std::vector<LevelEnemyData> save;
		for ( const auto& s : datas ) {
			save.push_back({ static_cast<int>( s.type ), s.railIndex, s.distance, s.patrol ? 1 : 0 });
		}
		EditorManager::GetInstance()->SetEditorEnemyData(save);
	}

	SpawnEnemies();                           // 配置テンプレートを元に敵を生成し直す

	// マップのスタート地点をプレイヤーへ（Initialize/リスポーンで使われる）
	if ( player_ ) { player_->SetSpawn(railField_.GetStartRail(), railField_.GetStartDistance()); }
	stageFlow_.Reset(); // マップが変わったらゴール状態はリセット

	// カメラ演出ゾーンを PlayCameraController へ（ノード番号→距離の変換込み）
	camCtrl_.Sync(EditorManager::GetInstance()->GetEditorCameraZones(), railField_.GetRails());
}

// エディタに配置された敵情報（テンプレート）に基づいて敵の実体を生成する
void GamePlayScene::SpawnEnemies(){
	if ( !enemyEditor_ ) { enemyMgr_.Clear(); return; }
	enemyMgr_.Spawn(enemyEditor_->GetSpawnDatas(), railField_.GetRails());
}



// メインのフレーム更新。べた書きを段階ごとの関数に委譲して見通しを良くした。
void GamePlayScene::Update(){
	hitFeel_.UpdateHitStop();     // 踏みつけ等のヒットストップ
	SyncFromEditors();            // エディタ編集（レール／敵／カメラ要求）をシーンへ反映
	UpdateCameraAndPostEffect();  // カメラ更新＋シェイク＋踏みつけポストエフェクト

	EngineMode currentMode = EditorManager::GetInstance()->GetMode();
	HandleModeTransition(currentMode); // Edit↔Play 切替時のリセット（prevMode_ もここで更新）

	if ( currentMode == EngineMode::Play ) {
		UpdatePlayMode();         // プレイ中のゲーム進行
	}

	UpdateSceneVisuals();         // モード問わず毎フレーム行う描画用更新
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
					datas.push_back({ static_cast<EnemyType>( e.type ), e.railIndex, e.distance, e.patrol != 0 });
				}
				enemyEditor_->SetSpawnDatas(datas); // changed_ が立つ → 下で SpawnEnemies される
			}
		}
	}

	// 敵配置エディタで追加・削除・編集があったら即リスポーン＆保存用データへ反映
	if ( enemyEditor_ && enemyEditor_->ConsumeChanged() ) {
		std::vector<LevelEnemyData> save;
		for ( const auto& s : enemyEditor_->GetSpawnDatas() ) {
			save.push_back({ static_cast<int>( s.type ), s.railIndex, s.distance, s.patrol ? 1 : 0 });
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

	// カメラエディタからの「この画角をプレビュー」要求を反映（メインカメラをその画角へ）
	if ( em->GetLevelEditor() ) {
		Vector3 pvPos, pvRot;
		if ( em->GetLevelEditor()->GetRailEditor()->ConsumeCameraPreviewRequest(pvPos, pvRot) ) {
			camera_->SetTranslation(pvPos);
			camera_->SetRotation(pvRot);
		}
	}
}

// カメラ更新（デバッグカメラ＋ヒット時のシェイク）とヒット点中心のポストエフェクト。
//   演出の実体は HitFeel クラス（ヒットストップ/シェイク/歪み+グロー）に分離した。
void GamePlayScene::UpdateCameraAndPostEffect(){
	if ( debugCamera_ ) { debugCamera_->Update(camera_.get()); }

	hitFeel_.ApplyCameraShake(camera_.get());          // ヒット時に一瞬揺らす
	camera_->Update();
	hitFeel_.UpdateImpactPostEffect(camera_.get());    // カメラ確定後にスクリーン投影する
}

// Edit↔Play の切り替わり瞬間のリセット処理。最後に prevMode_ を更新する。
void GamePlayScene::HandleModeTransition(EngineMode current){
	// エディット → プレイ：最新レールで確定し、プレイヤーをスタートへ、エフェクト・卵を消す
	if ( prevMode_ == EngineMode::Edit && current == EngineMode::Play ) {
		SyncRailsFromEditor();
		if ( player_ ) { player_->Initialize(); player_->SetMovementLocked(false); }
		camCtrl_.Reset(); // 向き切替トリガーの状態を初期化（前回プレイの向きを持ち越さない）
		hitEffects_.clear();
		combat_.ClearEffects();
		eggSystem_.Initialize();
		aimThrow_.Reset(); // 構え状態を解除
	}
	// プレイ → エディット：動くレールを基準位置に戻す（編集と表示を一致させる）
	if ( prevMode_ == EngineMode::Play && current == EngineMode::Edit ) {
		railField_.ResetMotion();
		// カメラ関連の後始末：
		//   ・回転フリーズ中に Stop しても時間が止まったままにならないよう Reset（内部で TimeScale を戻す）
		//   ・ゾーンで視野角を変えたまま戻るとエディタ画面が広角/望遠のままになるので標準に戻す
		camCtrl_.Reset();
		camera_->SetFovY(0.78f);
	}
	prevMode_ = current;
}

// プレイ中（時間が動いている時）のゲーム進行。
void GamePlayScene::UpdatePlayMode(){
	Input* input = Input::GetInstance();
	float dt = Time::GetInstance()->GetDeltaTime();

	// 動くレール → プレイヤー → 敵 の順で更新（位置の整合のため）
	railField_.UpdateMotion(dt);
	if ( player_ ) {
		// カメラの向きを渡す：向き切替（180°回り込み等）の後も「Dで画面の右へ」進めるように、
		// プレイヤー側でキー→ワールド方向の割り当てを回す
		player_->SetCameraYaw(camera_->GetRotation().y);
		player_->Update(railField_.GetRails());
	}
	Vector3 ppos = player_ ? player_->GetPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
	// 敵の移動＋吸い込みTick。吸い込み完了 → お腹に+1（口元で緑がふわっと）
	enemyMgr_.Update(railField_.GetRails(), ppos, dt, [&](const Vector3& pos){
		eggSystem_.AddToBelly();
		eggSystem_.SpawnSwallowFx(pos);
	});

	// 当たり判定＋踏みつけ
	combat_.Update(*player_, enemyMgr_, eggSystem_, hitFeel_, camera_.get());

	// E=飲み込み / 左Ctrl=産卵（SwallowAbility へ分離。dt はクールタイム用）
	swallow_.Update(*player_, enemyMgr_, eggSystem_, hitFeel_, dt);

	// Q長押しで構え→矢印で狙う→離して投げる（AimThrowController へ分離）
	aimThrow_.Update(*player_, enemyMgr_, eggSystem_, camera_.get(), dt);

	// 卵の追従・飛行・割れの更新
	if ( player_ ) {
		Vector3 ppos2 = player_->GetPosition();
		float yaw = player_->GetRotation().y;
		Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) };
		eggSystem_.Update(ppos2, facing, dt);
	}

	// ゴール判定＋ゴールマーカー（StageFlow へ分離）
	if ( player_ ) { stageFlow_.Update(player_->GetPosition(), railField_, hitFeel_); }

	// プレイ中カメラ（プレイヤー追従＋カメラ演出ゾーン）
	if ( player_ ) {
		camCtrl_.Update(camera_.get(), player_->GetPosition(), railField_.GetRails(),
			debugCamera_ && debugCamera_->IsActive(), dt);
		hitFeel_.NotifyCameraOverridden(); // カメラ位置を上書きしたのでシェイクの自己相殺をリセット
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
	combat_.UpdateEffects(dt); // 踏みつけ/命中の立体エフェクト
}




// モードに関わらず毎フレーム行う描画用の更新（オーラ・各種オブジェクト・マーカー・パーティクル等）。
void GamePlayScene::UpdateSceneVisuals(){
	Input* input = Input::GetInstance();
	EngineMode currentMode = EditorManager::GetInstance()->GetMode();

	// Editモード中も敵の見た目（WVP行列）を毎フレーム更新する。
	//   Obj3d::Update は「呼ばれた時のカメラ行列」を焼き込むため、これが無いと
	//   デバッグカメラを動かした時に敵が古い行列のまま描かれ、画面に貼り付いて付いてくる。
	//   dt=0 で呼ぶのでパトロール等の移動は起きない（位置はレール上の現在距離のまま）。
	if ( currentMode != EngineMode::Play ) {
		Vector3 pp = player_ ? player_->GetPosition() : Vector3 { 0.0f, 0.0f, 0.0f };
		enemyMgr_.Update(railField_.GetRails(), pp, 0.0f, nullptr);
	}

	// カメラ演出ゾーンの可視化（球=発動範囲 / 白い箱=カメラ位置の目安。編集中も見える）
	camCtrl_.DrawZoneMarkers(railField_.GetRails());

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
		} else {
			// Editモード：スタート地点のレール上に立たせて表示（スポーンプレビュー）。
			//   起動直後から「どこから始まるか」が一目で分かる。レール編集にも毎フレーム追従する。
			const auto& rails = railField_.GetRails();
			int sr = railField_.GetStartRail();
			if ( sr >= 0 && sr < ( int ) rails.size() && rails[sr].nodes.size() >= 2 ) {
				float sd = railField_.GetStartDistance();
				pos = rails[sr].GetPositionByDistance(sd);
				Vector3 tan = rails[sr].GetTangentByDistance(sd);
				if ( std::abs(tan.x) > 1e-4f || std::abs(tan.z) > 1e-4f ) {
					rot.y = std::atan2(tan.x, tan.z); // レールの進行方向を向かせる
				}
			}
		}
		skinnedObj_->SetTranslation(pos);
		skinnedObj_->SetRotation(rot);
		skinnedObj_->SetScale(scale);
		skinnedObj_->Update();
	}

	if ( sprite_ ) { sprite_->Update(); }

	// SDFボリューム（カメラ追従のCB更新＋エロージョン自動デモ）
	if ( sdfEggObj_ ) {
		if ( sdfErodeAuto_ ) {
			// 溶けて消える(erode→1.0) ⇔ 芯から生える(→0.0) をゆっくり繰り返す
			sdfErodeTime_ += 1.0f / 60.0f;
			float w = 0.5f * ( 1.0f - std::cos(sdfErodeTime_ * 1.2f) ); // 0→1→0
			sdfEggObj_->SetErode(w);
		}
		sdfEggObj_->Update();
	}

	// SDF看板の近接表示：プレイ中はプレイヤー位置を基準に「近づいた時だけ表示」が効く。
	// エディット中は配置作業ができるよう常に全表示（Clear）。
	if ( currentMode == EngineMode::Play && player_ ) {
		SDFManager::GetInstance()->SetViewerPosition(player_->GetPosition());
	} else {
		SDFManager::GetInstance()->ClearViewerPosition();
	}

	// 卵保持数HUD：持っている卵＝緑で明るく / 空きスロット＝薄く。お腹の数はオレンジの小丸。
	{
		int held  = eggSystem_.HeldCount();
		int belly = eggSystem_.BellyCount();
		for ( int i = 0; i < ( int ) eggHudSlots_.size(); ++i ) {
			eggHudSlots_[i]->SetColor(( i < held )
				? Vector4 { 0.55f, 1.0f, 0.6f, 0.95f }    // 保持中＝ヨッシー緑
				: Vector4 { 0.25f, 0.25f, 0.28f, 0.35f }); // 空き＝薄いグレー
			eggHudSlots_[i]->Update();
		}
		for ( int i = 0; i < ( int ) bellyHudIcons_.size(); ++i ) {
			// お腹にためた数だけオレンジで表示（それ以外は完全透明）
			bellyHudIcons_[i]->SetColor({ 1.0f, 0.75f, 0.25f, ( i < belly ) ? 0.9f : 0.0f });
			bellyHudIcons_[i]->Update();
		}
		// 数字表示：「×保持数」＋お腹にいる分は「（＋n）」
		if ( eggCountText_ ) {
			std::string s = "×" + std::to_string(held);
			if ( belly > 0 ) { s += "（＋" + std::to_string(belly) + "）"; }
			eggCountText_->SetText(s);
		}
	}

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
	enemyMgr_.Draw();                           // 敵
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
	combat_.Draw(); // 踏みつけ/命中の立体エフェクト

	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);

	// --- SDFボリューム（レイマーチング）---
	//   専用PSOに切り替えるので、通常のObj3d描画が全部終わった後に描く
	if ( sdfEggObj_ ) { sdfEggObj_->Draw(commandList); }
	eggSystem_.DrawBirthFx(commandList); // 産卵エロージョン演出中のSDF卵

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

	// 5.5 SDF（文字/画像）を最終画像に焼き込む → エディタの Game View にもそのまま映る。
	//     Bloom無効時は合成RTを経由しないので、後でバックバッファへ直描きする
	bool sdfBaked = false;
	if ( Bloom::GetInstance()->IsEnabled() ) {
		SDFManager::GetInstance()->DrawIntoTexture(commandList, Bloom::GetInstance()->GetCombineTexture());
		sdfBaked = true;
	}

	// エディタに最終的なゲーム画面のSRVを渡す（Game View 表示用）
	EditorManager::GetInstance()->SetGameViewSrvIndex(finalSrv);

	// 6. 最終結果をバックバッファへ書き出す（RTV リセット込み）
	//    エディタアクティブ時はRTVのセットのみ行い描画はスキップする
	PostEffect::GetInstance()->FinalBlit(commandList, finalSrv, EditorManager::GetInstance()->IsActive());

	// --- スプライト・UI描画 ---
	SpriteCommon::GetInstance()->PreDraw(commandList);
	//if ( sprite_ ) { sprite_->Draw(); }
	aimThrow_.DrawSprite(); // 構え中だけ狙いカーソル

	// 卵保持数HUD（プレイ中のみ。エディット中は編集の邪魔になるので出さない）
	if ( EditorManager::GetInstance()->GetMode() == EngineMode::Play ) {
		for ( auto& s : eggHudSlots_ )   { s->Draw(); }
		for ( auto& s : bellyHudIcons_ ) { s->Draw(); }
		// 保持数の数字（SDFText。スプライトと同じターゲットへ描く）
		if ( eggCountText_ ) {
			SDFManager::GetInstance()->DrawTextItem(commandList, *eggCountText_, "jpdot");
		}
	}

	TextManager::GetInstance()->Draw();

	// SDF 描画（Bloom無効で焼き込めなかった場合のみバックバッファへ直描き）
	if ( !sdfBaked ) {
		SDFManager::GetInstance()->Draw(commandList);
	}
}

void GamePlayScene::DrawDebugUI(){

#ifdef USE_IMGUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

	// SDFボリューム（卵）のエロージョン操作パネル
	ImGui::Begin("SDF卵 (エロージョン)");
	if ( sdfEggObj_ && sdfEggObj_->IsLoaded() ) {
		ImGui::TextDisabled("距離場に足す量で形が痩せる/太る（SDFだけの演出）");
		ImGui::Checkbox("自動アニメ（溶ける⇔生える）", &sdfErodeAuto_);
		if ( sdfErodeAuto_ ) {
			ImGui::TextDisabled("erode: %.2f m", sdfEggObj_->RefErode());
		} else {
			ImGui::SliderFloat("erode (m)", &sdfEggObj_->RefErode(), -0.3f, 1.2f);
		}
		ImGui::DragFloat3("位置", &sdfEggObj_->RefTranslation().x, 0.1f);
		ImGui::DragFloat("スケール", &sdfEggObj_->RefScale(), 0.05f, 0.2f, 10.0f);
	} else {
		ImGui::TextDisabled("resources/sdf3d/egg.sdf3d が読み込まれていません");
	}
	ImGui::End();

	// ※ヨッシーHUD（おなか/たまご数・操作説明の仮表示）は一旦削除した。
	//   本実装のUI（スプライト等）を作る時に復活させる。

	// ゴール到達表示（StageFlow へ分離）
	stageFlow_.DrawDebugUI();

	// レール経路の可視化トグル（共有の「詳細設定」ウィンドウに合流させる）
	ImGui::Begin("インスペクター (詳細設定)");

	if ( ImGui::CollapsingHeader("レール表示・カメラ視点 (Rail Debug)") ) {
	bool showMarkers = railField_.ShowMarkers();
	if ( ImGui::Checkbox("レール経路を表示", &showMarkers) ) { railField_.SetShowMarkers(showMarkers); }

	// プレイ中カメラ（プレイヤー追従＋カメラ演出ゾーン）
	camCtrl_.DrawDebugUI();
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
	// ノードエディタに登録したゲーム値（player_ 等のポインタ）も解除する
	EditorManager::GetInstance()->ClearNodeGameValues();

	object3ds_.clear();
	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}