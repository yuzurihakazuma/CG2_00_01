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
	ModelManager* modelManager = ModelManager::GetInstance();
	modelManager->LoadModel("fence", "resources", "fence.obj");
	modelManager->LoadModel("grass", "resources", "terrain.obj");
	modelManager->LoadModel("block", "resources/block", "block.obj");
	modelManager->CreateSphereModel("sphere", 16);
	modelManager->CreatePlaneModel("plane");
	modelManager->LoadModel("animatedCube", "resources/AnimatedCube", "AnimatedCube.gltf");
	modelManager->LoadModel("human", "resources/human", "walk.gltf");
	modelManager->LoadModel("egg", "resources/egg", "egg.obj"); // ヨッシーの卵（専用モデル。sphereの使い回しをやめる）
	modelManager->LoadModel("player", "resources/player", "player.gltf"); // プレイヤー（リグ付きマスコット。7色パレット焼き込み済み）
	modelManager->LoadModel("roadStraight", "resources/road", "road_straight.obj"); // 道の直線ピース（グリッド組み用）
	modelManager->LoadModel("roadEnd",      "resources/road", "road_end.obj");      // 道の終端キャップ（自由端を閉じる）
	modelManager->LoadModel("roadCorner",   "resources/road", "road_corner.obj");   // 交差点ピース：直角コーナー
	modelManager->LoadModel("roadT",        "resources/road", "road_t.obj");        // 交差点ピース：T字路
	modelManager->LoadModel("roadCross",    "resources/road", "road_cross.obj");    // 交差点ピース：十字路
	modelManager->LoadModel("roadJoint",    "resources/road", "road_joint.obj");    // 接続ノードの凸ジョイント（プラレール風）
	modelManager->CreateEggShellModel("eggShell", 0.3f);        // 卵の殻の欠片（頂点から手作り＝エンジン側で完結。割れ演出用）

	// 汎用パーティクル用の粒（"sphere" は敵と共有＋モンスターボール柄がデフォルトなので、
	//   色を付けるだけの粒には専用の白い球を使う。これも手作りモデル＋既存の白テクスチャだけで完結）。
	modelManager->CreateSphereModel("fxSphere", 8);
	if ( auto* fxModel = modelManager->FindModel("fxSphere") ) {
		fxModel->SetTexture("resources/block/white1x1.png");
	}

	// 収集物（コイン）用の金色の球。fxSphere と同じ手作りモデル＋白テクスチャ＋色だけで完結
	modelManager->CreateSphereModel("coin", 12);
	if ( auto* coinModel = modelManager->FindModel("coin") ) {
		coinModel->SetTexture("resources/block/white1x1.png");
		if ( coinModel->GetMaterial() ) {
			coinModel->GetMaterial()->color = { 1.0f, 0.85f, 0.2f, 1.0f };
		}
	}

	// クラフトブロック一式（ヨッシークラフトワールド風。全モデルが block_atlas.png 1枚を共有）。
	//   原点=底面中心・実寸1m角に変換済み（resources/block/craft/）。花は継ぎ目の自動デコ用
	modelManager->LoadModel("craftSponge",  "resources/block/craft", "block_1x1x1_sponge.obj");
	modelManager->LoadModel("craftLayer",   "resources/block/craft", "block_1x1x1_layer.obj");
	modelManager->LoadModel("craftSlope45", "resources/block/craft", "slope_1x1_rolls.obj");
	modelManager->LoadModel("craftSlope26", "resources/block/craft", "slope_2x1_rolls.obj");
	modelManager->LoadModel("flowerOrange", "resources/block/craft", "flower_orange.obj");
	modelManager->LoadModel("flowerWhite",  "resources/block/craft", "flower_white_face.obj");
	// 性質つきブロック：既存モデルを別名で読み、着色して見分ける
	auto loadTintedBlock = [&](const char* name, const char* file, const Vector4& color){
		modelManager->LoadModel(name, "resources/block/craft", file);
		if ( auto* model = modelManager->FindModel(name) ) {
			if ( model->GetMaterial() ) { model->GetMaterial()->color = color; }
		}
	};
	loadTintedBlock("craftSpring",     "block_1x1x1_sponge.obj", { 0.5f, 1.0f, 0.55f, 1.0f }); // ジャンプ台（緑）
	loadTintedBlock("craftHatena",     "block_1x1x1_layer.obj",  { 1.0f, 0.8f, 0.15f, 1.0f }); // ？ブロック（金）
	loadTintedBlock("craftHatenaUsed", "block_1x1x1_layer.obj",  { 0.45f, 0.42f, 0.4f, 1.0f }); // 使用済み（灰）
	loadTintedBlock("craftCloud",      "block_1x1x1_layer.obj",  { 0.9f, 0.97f, 1.0f, 1.0f }); // すり抜け床（白）
	// パーティクルグループ
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");
	// ※ 卵の煙／殻の飛び散りは加算パーティクルだと明るい背景で見えないため、
	//    EggSystem 側で実体(Obj3d)の小球として描画する（StompEffect と同じ確実に見える方式）。

	// テクスチャ（短縮版 Load：commandList を渡さなくてよい）
	TextureManager* textureManager = TextureManager::GetInstance();
	textures_["uvChecker"]     = textureManager->Load("resources/uvChecker.png");
	textures_["monsterBall"]   = textureManager->Load("resources/monsterBall.png");
	textures_["fence"]         = textureManager->Load("resources/fence.png");
	textures_["circle"]        = textureManager->Load("resources/circle.png");
	textures_["circle2"]       = textureManager->Load("resources/circle2.png");
	textures_["noise0"]        = textureManager->Load("Resources/noise0.png");
	textures_["noise1"]        = textureManager->Load("Resources/noise1.png");
	textures_["gradationLine"] = textureManager->Load("Resources/gradationLine.png");
	// 道アトラスの先読み：RoadMesh はレール編集のたび（=フレーム途中）に参照するので、
	// ここでキャッシュに載せておく（実行中のテクスチャ読み込みはデバッグレイヤーが嫌うため）
	textures_["roadAtlas"]     = textureManager->Load("resources/road/road_atlas.png");
	textures_["skybox"]        = textureManager->LoadCube("resources/StandardCubeMap.dds");

	// ブロックの一括描画を準備（見た目グループごとに1ドローコール）。
	//   モデルとテクスチャが揃った後に呼ぶ（ディゾルブ用SRVの束縛先が必要なため）
	blockSystem_.Initialize(textures_["uvChecker"].srvIndex);

	// Skybox
	Obj3dCommon::GetInstance()->SetEnvironmentTexture(textures_["skybox"].srvIndex);
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize("resources/StandardCubeMap.dds", commandList);

	// アニメーションデータ
	skinnedAnimTrack_.LoadFromJson("resources/human_anim.json");

	// 産卵エロージョン演出（左Ctrlで産む瞬間、SDFの卵が芯から育って実体メッシュに交代）
	eggSystem_.InitializeBirthFx(commandList);

	// 舌で捕まえた敵がSDFで溶けて消える演出（食べる瞬間もSDF消滅で統一）
	swallow_.InitializeEatFx(commandList);

	// 敵のSDF消滅演出（踏みつけ/卵命中で倒すと、その場で敵ボールが芯まで溶けて消える）
	combat_.InitializeDissolveFx(commandList);

	// SDF溶け道（近づくと道が現れ、離れると溶けて消える）のパイプライン構築
	dissolveRoad_.Initialize(commandList);
	// 消え方の調整UIを SDF パネル（SDF化の管理画面）の最下部へ差し込む
	//   ※「調整項目」ウィンドウ内から同名 Begin で追記する方式は、タブが同じドックに
	//     入ると片方しか描画されず絶対に表示されないため、フック方式で確実に出す
	SDFManager::GetInstance()->SetExtraPanelUI([this]{ dissolveRoad_.DrawImGui(); });
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

// 装飾/デモ用オブジェクト（展示物は DemoShowcase へ分離。ここに残るのはスキンメッシュのみ）
void GamePlayScene::SetupDemoObjects(){
	// エンジン機能の展示（回転キューブ・オーラ・SDF卵デモ等）。
	// Obj3d::Create がデフォルトカメラを掴むため、必ず SetupCameras の後に初期化する
	demo_.Initialize(DirectXCommon::GetInstance()->GetCommandList(), textures_);

	// プレイヤーの見た目（リグ付きマスコット）。カメラ確定後に生成する。
	//   7色はパレットPNGに焼き込み済みの1テクスチャ構成。クリップは Idle/TongueOut/Walk の3本
	playerObj_ = SkinnedObj3d::Create("player", "resources/player", "player.gltf");
	if ( playerObj_ ) { // モデル未登録だと nullptr が返る（Create は FindModel 前提）
		playerObj_->SetEnvironmentMap(textures_["skybox"].srvIndex);
		playerObj_->LoadClips("resources/player", "player.gltf");
		playerObj_->SetClip("Idle", true);
	}

	// 構え中に手（Itemジョイント）へ持たせる卵の見た目
	heldEggObj_ = Obj3d::Create("egg");

	// スキンメッシュ（Skinning機能の展示。定位置でその場歩き）
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

	// 狙い用カーソル（構え中だけ表示。敵に重なると赤＝ロックオン。AimThrowController が所有）
	aimThrow_.Initialize(textures_["circle2"].srvIndex);

	// 卵保持数HUD（左上）：上段=保持卵スロット6個 / 下段=お腹にためた数（小さい丸）
	eggHudSlots_.clear();
	bellyHudIcons_.clear();
	for ( int i = 0; i < EggSystem::kMaxEggs; ++i ) {
		auto slotSprite = Sprite::Create(textures_["circle2"].srvIndex, { 36.0f + i * 42.0f, 34.0f });
		slotSprite->SetSize({ 32.0f, 32.0f });
		eggHudSlots_.push_back(std::move(slotSprite));

		auto bellyIcon = Sprite::Create(textures_["circle2"].srvIndex, { 36.0f + i * 42.0f, 72.0f });
		bellyIcon->SetSize({ 16.0f, 16.0f });
		bellyHudIcons_.push_back(std::move(bellyIcon));
	}

	// 卵保持数の数字（SDFText。アイコン列の右に「×N（＋お腹）」。小さくてもフチ付きで潰れない）
	eggCountText_ = std::make_unique<SDFText>();
	eggCountText_->Initialize();
	eggCountText_->SetFontSize(34.0f);
	eggCountText_->SetPosition(36.0f + EggSystem::kMaxEggs * 42.0f + 10.0f, 16.0f);
	eggCountText_->SetColor({ 0.75f, 1.0f, 0.8f, 1.0f });
	eggCountText_->SetOutlineWidth(0.22f);
	eggCountText_->SetOutlineColor({ 0.05f, 0.12f, 0.05f, 1.0f });

	// コイン取得数（SDFText。卵HUDの下に金色で「●n/全体」）
	coinCountText_ = std::make_unique<SDFText>();
	coinCountText_->Initialize();
	coinCountText_->SetFontSize(30.0f);
	coinCountText_->SetPosition(36.0f, 104.0f);
	coinCountText_->SetColor({ 1.0f, 0.85f, 0.25f, 1.0f });
	coinCountText_->SetOutlineWidth(0.22f);
	coinCountText_->SetOutlineColor({ 0.25f, 0.15f, 0.02f, 1.0f });

	// GPUパーティクル基盤の初期化（エミッターのデモは DemoShowcase が持つ）
	GPUParticleManager::GetInstance()->Initialize(
		DirectXCommon::GetInstance(), SrvManager::GetInstance(), "resources/uvChecker.png");

	// プレイヤー・敵エディタ
	player_ = std::make_unique<Player>();
	player_->Initialize();
	enemyEditor_ = std::make_unique<EnemyEditor>();
	enemyEditor_->Initialize();

	// 戦闘（踏みつけ/卵命中）＋ヒット演出（エフェクト用テクスチャを渡す）
	combat_.Initialize(textures_["circle"].srvIndex, textures_["skybox"].srvIndex);

	// エフェクトの事前生成：初撃破の瞬間に Obj3d を数十個作るとゲームが固まるので、
	// ロード中のいま全部作ってプールに積んでおく。パフの数は最悪フレーム
	// （卵トレイル約13個借用中＋命中バースト8＋割れ演出6＋投げ煙6）を賄う量＝返却上限に合わせる
	combat_.Prewarm(camera_.get(), 4);
	eggSystem_.PrewarmPuffPool("fxSphere", 32);
	eggSystem_.PrewarmPuffPool("eggShell", 28);

	// --- ノードエディタの「→ ゲーム値」に、今作っているゲームの調整値を登録する ---
	//   ノードグラフから接続すると、プレイ中の挙動をリアルタイムに動かせる。
	//   ※ポインタ登録なので、シーン終了時（Finalize）に必ず解除する。
	{
		EditorManager* editorManager = EditorManager::GetInstance();
		editorManager->ClearNodeGameValues(); // シーン再初期化時の二重登録防止
		editorManager->RegisterNodeGameValue("プレイヤー移動速度",   player_->MoveSpeedPtr(),  0.5f, 20.0f);
		editorManager->RegisterNodeGameValue("ジャンプ力",           player_->JumpPowerPtr(),  1.0f, 20.0f);
		editorManager->RegisterNodeGameValue("卵の投げ初速",         aimThrow_.ThrowSpeedNormalPtr(), 1.0f, 40.0f);
		editorManager->RegisterNodeGameValue("ロックオン投げ初速",   aimThrow_.ThrowSpeedLockPtr(),   1.0f, 60.0f);
		editorManager->RegisterNodeGameValue("飲み込みの届く距離",   swallow_.SwallowReachPtr(), 0.5f, 10.0f);
		editorManager->RegisterNodeGameValue("飲み込みクールタイム", swallow_.SwallowCooldownPtr(), 0.0f, 2.0f);
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
//   simple=true はドラッグ中の軽量同期：道は簡易リボンのみ・敵の張り直しや再生成は行わない
//   （マウスアップ後に simple=false の本同期が1回走って最終形になる）
void GamePlayScene::SyncRailsFromEditor(bool simple){
	uint32_t whiteTex = 0;
	auto itWhite = textures_.find("white");
	if ( itWhite != textures_.end() ) { whiteTex = itWhite->second.srvIndex; }

	if ( simple ) {
		railField_.Sync(camera_.get(), whiteTex);                     // レール本体＋緑線
		roadMesh_.Build(railField_.GetRails(), camera_.get(), true);  // 道は簡易プレビュー
		return;
	}

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
		for ( const auto& spawnData : enemyEditor_->GetSpawnDatas() ) {
			bool railValid = ( spawnData.railIndex >= 0 && spawnData.railIndex < ( int ) oldRails.size()
			            && oldRails[spawnData.railIndex].nodes.size() >= 2 );
			oldEnemyValid.push_back(railValid ? 1 : 0);
			oldEnemyPos.push_back(railValid ? oldRails[spawnData.railIndex].GetPositionByDistance(spawnData.distance)
			                         : Vector3 { 0.0f, 0.0f, 0.0f });
		}
	}

	railField_.Sync(camera_.get(), whiteTex); // レール本体＋緑線を作り直す
	roadMesh_.Build(railField_.GetRails(), camera_.get()); // レール下の道メッシュも敷き直す
	dissolveRoad_.Build(railField_.GetRails());            // SDF溶け道のパネル敷設点も打ち直す

	// --- 敵のピン留め：編集後のレール上で「元のワールド位置の最寄り点」へ距離を張り直す ---
	//   路線まるごと移動なら一緒に付いていき、形の部分編集なら他の敵は動かない。
	if ( enemyEditor_ && !mapLoadPending ) {
		auto& spawnDatas = enemyEditor_->MutableSpawnDatas();
		const auto& newRails = railField_.GetRails();
		for ( size_t i = 0; i < spawnDatas.size() && i < oldEnemyValid.size(); ++i ) {
			if ( !oldEnemyValid[i] ) continue;
			auto& spawnData = spawnDatas[i];
			if ( spawnData.railIndex < 0 || spawnData.railIndex >= ( int ) newRails.size() ) continue;
			if ( newRails[spawnData.railIndex].nodes.size() < 2 ) continue;
			spawnData.distance = newRails[spawnData.railIndex].GetClosestDistance(oldEnemyPos[i]);
		}
		// マップ保存用データにも張り直した距離を反映（保存した時に位置がズレないように）
		std::vector<LevelEnemyData> save;
		for ( const auto& spawnData : spawnDatas ) {
			save.push_back({ static_cast<int>( spawnData.type ), spawnData.railIndex, spawnData.distance, spawnData.patrol ? 1 : 0,
			                 spawnData.patrolMin, spawnData.patrolMax });
		}
		EditorManager::GetInstance()->SetEditorEnemyData(save);
	}

	SpawnEnemies();                           // 配置テンプレートを元に敵を生成し直す

	// マップのスタート地点をプレイヤーへ（Initialize/リスポーンで使われる）
	if ( player_ ) { player_->SetSpawn(railField_.GetStartRail(), railField_.GetStartDistance()); }
	stageFlow_.Reset(); // マップが変わったらゴール状態はリセット

	// カメラ演出ゾーンを PlayCameraController へ（ノード番号→距離の変換込み）
	camCtrl_.Sync(EditorManager::GetInstance()->GetEditorCameraZones(), railField_.GetRails());

	// 収集物（コイン）をエディタの配置から作り直す
	coinSystem_.Sync(EditorManager::GetInstance()->GetEditorCoins(), railField_.GetRails());

	// ブロック（乗れる/ぶつかる）もエディタの配置から作り直す
	blockSystem_.Sync(EditorManager::GetInstance()->GetEditorBlocks(), &railField_.GetRails());
	lastBlockVersion_ = EditorManager::GetInstance()->GetEditorBlockVersion();
	if ( player_ ) { player_->SetBlocks(&blockSystem_); }
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

	// --- エフェクト初回使用の前倒し（ウォームアップ）---
	//   SDF溶け演出は初めて描画された瞬間にシェーダー/パイプラインの遅延構築で1秒以上固まり、
	//   ヒット時ポストエフェクト（歪み+グロー）の初回パスも約0.2秒かかる（フレーム計測で実測）。
	//   起動直後の数フレームで「画面外のSDF溶け＋無効パラメータのポストエフェクト」を一度通しておき、
	//   初めて敵を倒した瞬間のフリーズを無くす
	{
		static int fxWarmupFrame = 0;
		++fxWarmupFrame;
		PostEffect* postEffect = PostEffect::GetInstance();
		if ( fxWarmupFrame == 1 ) {
			combat_.WarmupDissolveFx(); // 画面外でSDF溶けを1回描かせる
			postEffect->SetMaskParams(PostEffectMaskParams{});   // マスク半径0＝画面に影響なし
			postEffect->SetEffectActive(PostEffectType::MaskedDistortion, true);
			postEffect->SetEffectActive(PostEffectType::MaskedGlow, true);
			postEffect->SetIrisParams(2.0f, 0.5f, 0.5f);         // 全開＝見えない
			postEffect->SetEffectActive(PostEffectType::IrisWipe, true);
			postEffect->SetEffectActive(PostEffectType::TiltShift, true);
			postEffect->SetEffectActive(PostEffectType::PaperGrain, true);
			postEffect->SetEffectActive(PostEffectType::PictureBook, true);
			postEffect->SetEffectActive(PostEffectType::Grayscale, true); // 落下ミス演出で使うので初回パスも前倒し
		} else if ( fxWarmupFrame == 4 ) {
			// 3フレーム通したら全て元に戻す（Edit中は全OFFが正規状態。Play遷移時は改めてONになる）
			postEffect->SetEffectActive(PostEffectType::MaskedDistortion, false);
			postEffect->SetEffectActive(PostEffectType::MaskedGlow, false);
			postEffect->SetEffectActive(PostEffectType::IrisWipe, false);
			postEffect->SetEffectActive(PostEffectType::TiltShift, false);
			postEffect->SetEffectActive(PostEffectType::PaperGrain, false);
			postEffect->SetEffectActive(PostEffectType::PictureBook, false);
			postEffect->SetEffectActive(PostEffectType::Grayscale, false);
			combat_.ClearEffects(); // 画面外の溶け演出を止めてスロットを返す
		}
	}
}


// エディタ側の編集（レール／敵配置／Blenderカメラ要求）を検知してシーンへ反映する。
void GamePlayScene::SyncFromEditors(){
	EditorManager* editorManager = EditorManager::GetInstance();

	// レールのライブ同期：エディタで編集されたら緑線とプレイヤー用データを作り直す。
	//   ドラッグ中は「最大10回/秒の軽量同期」に間引き、マウスアップ後に本同期を1回行う（§1）
	{
		const bool dragging = editorManager->IsRailDragging();
		const bool changed = ( editorManager->GetRailEditVersion() != railField_.Version() );
		railSyncTimer_ += 1.0f / 60.0f;
		if ( changed && dragging ) {
			if ( railSyncTimer_ >= 0.1f ) { // 10Hz
				railSyncTimer_ = 0.0f;
				SyncRailsFromEditor(true);
				railFullSyncPending_ = true; // ドラッグが終わったら本生成する
			} else {
				railFullSyncPending_ = true;
			}
		} else if ( !dragging && ( changed || railFullSyncPending_ ) ) {
			railFullSyncPending_ = false;
			SyncRailsFromEditor(false);
		}
	}

	// マップが読み込まれたら、保存済みの敵配置をエディタへ復元する
	if ( enemyEditor_ ) {
		int mapLoadVersion = editorManager->GetMapLoadVersion();
		if ( mapLoadVersion != lastMapLoadVersion_ ) {
			lastMapLoadVersion_ = mapLoadVersion;
			const auto& saved = editorManager->GetEditorEnemyData();
			// 敵ゼロのマップでも必ず反映する（以前は空だとスキップ→前マップの敵が残留し、
			// そのまま保存すると別マップの敵が紛れ込むバグがあった）
			std::vector<EnemySpawnData> spawnDatas;
			for ( const auto& enemyData : saved ) {
				spawnDatas.push_back({ static_cast<EnemyType>( enemyData.type ), enemyData.railIndex, enemyData.distance, enemyData.patrol != 0,
				                       enemyData.patrolMin, enemyData.patrolMax });
			}
			enemyEditor_->SetSpawnDatas(spawnDatas); // changed_ が立つ → 下で SpawnEnemies される
		}
	}

	// ブロックのライブ同期：ペイント配置/削除のたびにブロックだけ作り直す
	//   （レール・道・敵はそのまま＝クリック連打しても軽い）
	{
		int blockVersion = editorManager->GetEditorBlockVersion();
		if ( blockVersion != lastBlockVersion_ ) {
			lastBlockVersion_ = blockVersion;
			blockSystem_.Sync(editorManager->GetEditorBlocks(), &railField_.GetRails());
			if ( player_ ) { player_->SetBlocks(&blockSystem_); }
		}
	}

	// 敵配置エディタで追加・削除・編集があったら即リスポーン＆保存用データへ反映
	if ( enemyEditor_ && enemyEditor_->ConsumeChanged() ) {
		std::vector<LevelEnemyData> save;
		for ( const auto& spawnData : enemyEditor_->GetSpawnDatas() ) {
			save.push_back({ static_cast<int>( spawnData.type ), spawnData.railIndex, spawnData.distance, spawnData.patrol ? 1 : 0,
			                 spawnData.patrolMin, spawnData.patrolMax });
		}
		editorManager->SetEditorEnemyData(save);
		SpawnEnemies();
	}

	// Blenderインポータからの「カメラに適用」要求を反映
	if ( BlenderImporter* importer = editorManager->GetBlenderImporter() ) {
		Vector3 blenderCamPos, blenderCamRot;
		if ( importer->ConsumeCameraRequest(blenderCamPos, blenderCamRot) ) {
			camera_->SetTranslation(blenderCamPos);
			camera_->SetRotation(blenderCamRot);
		}
	}

	// カメラエディタからの「この画角をプレビュー」要求を反映（メインカメラをその画角へ）
	if ( editorManager->GetLevelEditor() ) {
		Vector3 previewPos, previewRot;
		if ( editorManager->GetLevelEditor()->GetRailEditor()->ConsumeCameraPreviewRequest(previewPos, previewRot) ) {
			camera_->SetTranslation(previewPos);
			camera_->SetRotation(previewRot);
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
		// クラフト世界観の常時ポストエフェクト（プレイ中だけ。編集の見やすさを守るためEditではOFF）。
		//   値はエディタで調整して決めた本番ルック（Playを押すたびこの状態から始まる）
		PostEffect* postEffect = PostEffect::GetInstance();
		postEffect->SetEffectActive(PostEffectType::TiltShift, true);   // ジオラマ風（遠景ぼかし）
		postEffect->SetEffectActive(PostEffectType::PaperGrain, true);  // 紙の質感
		postEffect->SetEffectActive(PostEffectType::PictureBook, true); // 絵本風（ポスタライズ＋輪郭）
		postEffect->SetTiltShiftParams(7.2f, 0.55f, 0.17f);
		postEffect->SetPaperGrainStrength(0.17f);
		postEffect->SetPictureBookParams(14.0f, 0.8f);
		SyncRailsFromEditor();
		if ( player_ ) { player_->Initialize(); player_->SetMovementLocked(false); }
		camCtrl_.Reset(); // 向き切替トリガーの状態を初期化（前回プレイの向きを持ち越さない）
		demo_.OnPlayStart(); // デモの HitEffect を消す
		combat_.ClearEffects();
		eggSystem_.Initialize();
		aimThrow_.Reset(); // 構え状態を解除
		swallow_.Reset();  // 舌アクションを解除（敵が作り直されるので target_ を確実に手放す）
		coinSystem_.ResetPlay(); // コインを全部復活させる
		blockSystem_.ResetPlay(); // ？ブロックを未使用に戻す
	}
	// プレイ → エディット：動くレールを基準位置に戻す（編集と表示を一致させる）
	if ( prevMode_ == EngineMode::Play && current == EngineMode::Edit ) {
		// プレイ用ポストエフェクトを解除（編集画面はそのまま見えるように）
		PostEffect* postEffect = PostEffect::GetInstance();
		postEffect->SetEffectActive(PostEffectType::TiltShift, false);
		postEffect->SetEffectActive(PostEffectType::PaperGrain, false);
		postEffect->SetEffectActive(PostEffectType::PictureBook, false);
		postEffect->SetEffectActive(PostEffectType::IrisWipe, false);
		postEffect->SetEffectActive(PostEffectType::Grayscale, false); // アイリス途中でEditへ戻った場合の消し忘れ防止
		irisPhase_ = 0;
		railField_.ResetMotion();
		coinSystem_.ResetPlay(); // 取得済みコインを復活（エディタで配置が見えなくならないように）
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
	float deltaTime = Time::GetInstance()->GetDeltaTime();

	// 動くレール → プレイヤー → 敵 の順で更新（位置の整合のため）
	railField_.UpdateMotion(deltaTime);
	if ( player_ ) {
		// カメラの向きを渡す：向き切替（180°回り込み等）の後も「Dで画面の右へ」進めるように、
		// プレイヤー側でキー→ワールド方向の割り当てを回す
		player_->SetCameraYaw(camera_->GetRotation().y);
		// ベロを出している間は振り向き禁止（移動は可）。出したまま反転して見た目が破綻するのを防ぐ
		player_->SetTurnLocked(swallow_.IsTongueActive());
		// 卵の構え中は狙い（カーソル）方向を向く（後ろ狙いなら振り向く）
		player_->SetFaceOverride(aimThrow_.IsAiming(), aimThrow_.GetAimYaw());
		// 構え中は後ろの列の先頭の卵を非表示（手に持っている扱い。キャンセルで列に戻る）
		eggSystem_.SetAimHolding(aimThrow_.IsAiming());
		// 構え中はベロ(E)と産卵(左Ctrl)を発動禁止（両手が卵でふさがっている）
		swallow_.SetActionBlocked(aimThrow_.IsAiming());
		player_->Update(railField_.GetRails());
	}
	Vector3 playerPos = player_ ? player_->GetPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
	// 敵の移動＋吸い込みTick。吸い込み完了 → お腹に+1（口元で緑がふわっと）
	enemyMgr_.Update(railField_.GetRails(), playerPos, deltaTime, [&](const Vector3& pos){
		eggSystem_.AddToBelly();
		eggSystem_.SpawnSwallowFx(pos);
	});

	// 当たり判定＋踏みつけ
	combat_.Update(*player_, enemyMgr_, eggSystem_, hitFeel_, camera_.get());

	// E=舌を伸ばして捕まえる / 左Ctrl=産卵（SwallowAbility へ分離。deltaTime はクールタイム・舌アニメ用）
	swallow_.Update(*player_, enemyMgr_, eggSystem_, hitFeel_, camera_.get(), deltaTime);

	// Q長押しで構え→矢印で狙う→離して投げる（AimThrowController へ分離）
	aimThrow_.Update(*player_, enemyMgr_, eggSystem_, camera_.get(), deltaTime);

	// 卵の追従・飛行・割れの更新
	if ( player_ ) {
		Vector3 currentPlayerPos = player_->GetPosition();
		float yaw = player_->GetRotation().y;
		Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) };
		eggSystem_.Update(currentPlayerPos, facing, deltaTime);
	}

	// ゴール判定＋ゴールマーカー（StageFlow へ分離）
	if ( player_ ) { stageFlow_.Update(player_->GetPosition(), railField_, hitFeel_); }

	// プレイ中カメラ（プレイヤー追従＋カメラ演出ゾーン）
	if ( player_ ) {
		camCtrl_.Update(camera_.get(), player_->GetPosition(), railField_.GetRails(),
			debugCamera_ && debugCamera_->IsActive(), deltaTime);
		hitFeel_.NotifyCameraOverridden(); // カメラ位置を上書きしたのでシェイクの自己相殺をリセット
	}

	// デモ入力（Space=BGM+HitEffect / P=パーティクル）と HitEffect の進行は DemoShowcase へ
	//   デモ表示OFF（表示メニュー）の間は入力ごと無効
	if ( EditorManager::GetInstance()->IsDemoVisible() ) {
		demo_.UpdatePlay(input, camera_.get(), textures_, bgmFile_);

		// アニメーションの進行（見本の人形）
		if ( skinnedObj_ && skinnedAnimTrack_.duration > 0.0f ) {
			skinnedAnimTime_ += 1.0f / 60.0f;
			if ( skinnedAnimTime_ > skinnedAnimTrack_.duration ) { skinnedAnimTime_ = 0.0f; }
		}
	}

	// エフェクトの更新（stompEffect は timeScale 適用 deltaTime → ヒットストップで一緒に止まる）
	combat_.UpdateEffects(deltaTime); // 踏みつけ/命中の立体エフェクト
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
		Vector3 playerPos = player_ ? player_->GetPosition() : Vector3 { 0.0f, 0.0f, 0.0f };
		enemyMgr_.Update(railField_.GetRails(), playerPos, 0.0f, nullptr);
	}

	// カメラ演出ゾーンの可視化（球=発動範囲 / 白い箱=カメラ位置の目安。編集中も見える）
	camCtrl_.DrawZoneMarkers(railField_.GetRails());

	// タイトルへ戻る
	if ( input->Triggerkey(DIK_T) ) {
		SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
	}

	// 3Dオブジェクト・レールマーカーの行列更新
	for ( auto& obj : object3ds_ ) { obj->Update(); }
	railField_.UpdateMarkers(); // レール緑線（カメラ移動に追従）
	roadMesh_.SetJointVisible(EditorManager::GetInstance()->GetEditorJointVisible()); // ジョイント表示モード（§5）
	// 動くレールのエディタプレビュー：Playを押さなくても動きを再生して組み方を確認できる。
	//   チェックを外した瞬間に基準位置へ戻す（編集と表示がずれないように）
	{
		static bool prevMotionPreview = false;
		bool motionPreview = ( currentMode == EngineMode::Edit )
		                  && EditorManager::GetInstance()->GetEditorRailMotionPreview();
		if ( motionPreview ) { railField_.UpdateMotion(1.0f / 60.0f); }
		else if ( prevMotionPreview && currentMode == EngineMode::Edit ) { railField_.ResetMotion(); }
		prevMotionPreview = motionPreview;
	}

	roadMesh_.Update(railField_.GetRails()); // 道メッシュ（動くレール追従＋カメラ追従）

	// SDF溶け道：プレイヤーとの距離でパネルの現れ/溶けを更新（エディタ中も動きが見える）
	dissolveRoad_.Update(player_ ? player_->GetPosition() : Vector3 { 0.0f, 0.0f, 0.0f }, 1.0f / 60.0f);

	// 収集物（コイン）：回転・浮遊はエディタ中も見せる。取得判定はプレイ中のみ
	coinSystem_.Update(player_ ? player_->GetPosition() : Vector3 { 0.0f, 0.0f, 0.0f },
	                   currentMode == EngineMode::Play, 1.0f / 60.0f);

	// ブロック：位置＋行列更新（動くレールに乗ったブロックの追従・カメラ追従）
	blockSystem_.Update();

	// --- アイリスワイプ演出：落下死→リスポーン時に、プレイヤー中心の円が閉じて→開く ---
	if ( currentMode == EngineMode::Play && player_ ) {
		if ( player_->ConsumeFellRespawn() ) {
			irisPhase_ = 1; // 閉じるところから開始
			irisTimer_ = 0.0f;
			PostEffect::GetInstance()->SetEffectActive(PostEffectType::IrisWipe, true);
			// ミスの瞬間は画面の色も抜く（グレースケール）。開く時に色が戻って「復活」感を出す
			PostEffect::GetInstance()->SetEffectActive(PostEffectType::Grayscale, true);
		}
		if ( irisPhase_ != 0 ) {
			irisTimer_ += 1.0f / 60.0f;
			const float kCloseTime = 0.18f, kHoldTime = 0.12f, kOpenTime = 0.5f;
			float radius = 1.4f; // 全開
			if ( irisPhase_ == 1 ) {        // 閉じる
				radius = 1.4f * ( 1.0f - irisTimer_ / kCloseTime );
				if ( irisTimer_ >= kCloseTime ) { irisPhase_ = 2; irisTimer_ = 0.0f; radius = 0.0f; }
			} else if ( irisPhase_ == 2 ) { // 閉じたまま一呼吸
				radius = 0.0f;
				if ( irisTimer_ >= kHoldTime ) {
					irisPhase_ = 3; irisTimer_ = 0.0f;
					// 開き始めた瞬間に色を戻す（円の外から色付きの世界が現れる）
					PostEffect::GetInstance()->SetEffectActive(PostEffectType::Grayscale, false);
				}
			} else {                        // 開く
				float t = irisTimer_ / kOpenTime;
				radius = 1.4f * t * t; // ゆっくり始まってすっと開く
				if ( irisTimer_ >= kOpenTime ) {
					irisPhase_ = 0;
					PostEffect::GetInstance()->SetEffectActive(PostEffectType::IrisWipe, false);
				}
			}
			// 円の中心＝プレイヤーの胸元をスクリーンUVへ投影
			float centerU = 0.5f, centerV = 0.5f;
			Vector3 playerHead = player_->GetPosition(); playerHead.y += 0.8f;
			Vector2 ndc;
			if ( WorldToNdc(playerHead, camera_->GetViewProjectionMatrix(), ndc) ) {
				centerU = ndc.x * 0.5f + 0.5f;
				centerV = 1.0f - ( ndc.y * 0.5f + 0.5f );
			}
			PostEffect::GetInstance()->SetIrisParams(( std::max )( radius, 0.0f ), centerU, centerV);
		}
	}

	// ？ブロックから出たコイン：カウント加算＋金色の粒が飛び出す演出
	{
		Vector3 bumpCoinPos {};
		while ( blockSystem_.ConsumeBumpCoin(bumpCoinPos) ) {
			coinSystem_.AddBonus(1);
			for ( int i = 0; i < 6; ++i ) {
				float angle = ( float ) i / 6.0f * 6.2831853f;
				Vector3 vel = { std::cos(angle) * 1.5f, 3.5f + ( float ) ( i % 3 ) * 0.6f, std::sin(angle) * 1.5f };
				eggSystem_.SpawnPuff(bumpCoinPos, vel, { 1.0f, 0.85f, 0.2f, 1.0f }, 0.12f, 0.5f, "fxSphere");
			}
		}
	}

	// プレイヤーの見た目（恐竜マスコット player.obj）：
	//   Play中はプレイヤーに追従、Edit中はスタート地点プレビュー。
	//   リグ無しモデルなので、歩きは実移動速度に連動した手続きウォドル（左右ロール＋小さな跳ね）で出す
	if ( playerObj_ ) {
		Vector3 pos {}, rot {};
		if ( currentMode == EngineMode::Play && player_ ) {
			pos = player_->GetPosition();
			rot = player_->GetRotation();
		} else {
			// Editモード：スタート地点のレール上に立たせて表示（スポーンプレビュー）。
			//   起動直後から「どこから始まるか」が一目で分かる。レール編集にも毎フレーム追従する。
			const auto& rails = railField_.GetRails();
			int startRail = railField_.GetStartRail();
			if ( startRail >= 0 && startRail < ( int ) rails.size() && rails[startRail].nodes.size() >= 2 ) {
				float startDistance = railField_.GetStartDistance();
				pos = rails[startRail].GetPositionByDistance(startDistance);
				Vector3 tangent = rails[startRail].GetTangentByDistance(startDistance);
				if ( std::abs(tangent.x) > 1e-4f || std::abs(tangent.z) > 1e-4f ) {
					rot.y = std::atan2(tangent.x, tangent.z); // レールの進行方向を向かせる
				}
			}
		}
		// 実際に動いた速さ（見た目の位置の差分）→ クリップと再生速度を決める
		float horizontalSpeed = 0.0f;
		if ( playerPrevPosValid_ ) {
			float dx = pos.x - playerPrevPos_.x;
			float dz = pos.z - playerPrevPos_.z;
			horizontalSpeed = std::sqrt(dx * dx + dz * dz) * 60.0f; // 固定60FPS想定で m/s へ
		}
		playerPrevPos_ = pos;
		playerPrevPosValid_ = true;
		const float kWalkBaseSpeed = 5.0f; // プレイヤーの通常移動速度(m/s)で Walk を等速再生
		float speedRatio = std::clamp(horizontalSpeed / kWalkBaseSpeed, 0.0f, 1.8f);

		// 構えをXでキャンセルした場合は投げモーションを出さない（卵は後ろへ戻るだけ）
		if ( aimThrow_.ConsumeCanceled() ) { playerThrowTimer_ = 0.0f; }

		// クリップ選択：ベロ動作中 > 歩き > 待機
		if ( currentMode == EngineMode::Play && swallow_.IsTongueActive() ) {
			bool wasRetracting = ( playerObj_->GetCurrentClip() == "TongueOut" );
			playerObj_->SetClip("TongueOut", false); // 予備動作→射出→収納（1回きり）
			playerObj_->SetPlaybackSpeed(2.8f);      // 1.33秒のクリップを高速化したベロ動作(約0.5秒)に合わせる
			// ベロの長さを実距離に同期（Tongueボーンの伸びを上書き）＝目の前の敵でも貫通しない
			float stretch = std::clamp(swallow_.GetTongueLength() / 1.2f, 0.10f, 1.0f);
			playerObj_->SetBoneScaleOverride("Tongue", { 1.0f, stretch, 1.0f });
			// 捕獲成立で戻しに入った瞬間、クリップを収納パートへ飛ばす（伸ばす絵をスキップ）
			if ( wasRetracting && swallow_.IsRetracting() && !playerTongueSeeked_ ) {
				playerObj_->SetAnimationTime(0.85f);
				playerTongueSeeked_ = true;
			}
		} else if ( currentMode == EngineMode::Play && aimThrow_.IsAiming() ) {
			// 卵投げの構え：Throw クリップの「溜め」ポーズで静止（両手で頭上に構える）
			playerObj_->ClearBoneScaleOverride();
			playerTongueSeeked_ = false;
			playerObj_->SetClip("Throw", false);
			playerObj_->SetPlaybackSpeed(0.0f);
			playerObj_->SetAnimationTime(0.67f); // 溜め(f16)のポーズ
			playerThrowTimer_ = 0.65f;           // 離した後にリリース→復帰を再生する時間
			// 構え中は卵を持ち物ソケット（Itemジョイント）に持たせる
			Vector3 itemPos;
			heldEggVisible_ = ( eggSystem_.HeldCount() > 0 ) && playerObj_->GetJointWorldPosition("Item", itemPos);
			if ( heldEggVisible_ && heldEggObj_ ) {
				heldEggObj_->SetTranslation(itemPos);
				heldEggObj_->SetScale({ 0.29f, 0.29f, 0.29f }); // READMEの推奨（手のサイズに合う）
				heldEggObj_->Update();
			}
		} else if ( currentMode == EngineMode::Play && playerThrowTimer_ > 0.0f ) {
			// 離した直後：リリース(f20)→復帰を最後まで再生
			heldEggVisible_ = false;
			if ( playerObj_->GetCurrentClip() == "Throw" && playerThrowTimer_ >= 0.649f ) {
				playerObj_->SetAnimationTime(0.75f); // リリース直前(f18)から
			}
			playerObj_->SetClip("Throw", false);
			playerObj_->SetPlaybackSpeed(1.6f);
			playerThrowTimer_ -= 1.0f / 60.0f;
		} else if ( currentMode == EngineMode::Play && eggSystem_.IsBirthActive() ) {
			// 産卵：しゃがみ踏ん張り→ポンッ（演出時間に合わせて早回し）
			heldEggVisible_ = false;
			playerObj_->SetClip("EggLay", false);
			playerObj_->SetPlaybackSpeed(2.5f);
		} else if ( currentMode == EngineMode::Play && player_ && !player_->IsGrounded() ) {
			heldEggVisible_ = false;
			playerObj_->ClearBoneScaleOverride();
			playerTongueSeeked_ = false;
			if ( player_->IsFluttering() ) {
				playerObj_->SetClip("Walk", true);   // ふんばり：足を高速バタバタ（ソニック風）
				playerObj_->SetPlaybackSpeed(3.2f);
			} else {
				// ジャンプ中：Walkの足を伸ばした瞬間のポーズで静止＝つま先立ちで跳んでいる感じ
				playerObj_->SetClip("Walk", true);
				playerObj_->SetPlaybackSpeed(0.0f);
				playerObj_->SetAnimationTime(0.25f); // クリップ(1秒)内のポーズ位置。好みで0〜1秒
			}
		} else if ( speedRatio > 0.05f ) {
			playerObj_->ClearBoneScaleOverride();
			playerTongueSeeked_ = false;
			heldEggVisible_ = false;
			playerObj_->SetClip("Walk", true);       // 足パタパタ＋バウンド
			playerObj_->SetPlaybackSpeed(( std::max )( speedRatio, 0.4f ));
		} else {
			playerObj_->ClearBoneScaleOverride();
			playerTongueSeeked_ = false;
			heldEggVisible_ = false;
			playerObj_->SetClip("Idle", true);       // 呼吸＋頭の微揺れ
			playerObj_->SetPlaybackSpeed(1.0f);
		}

		playerObj_->SetTranslation({ pos.x, pos.y + playerModelYOffset_, pos.z });
		playerObj_->SetRotation({ 0.0f, rot.y, 0.0f });
		playerObj_->Update();
	}

	// スキンメッシュ（Skinning機能の展示）：定位置でその場歩き。
	//   デモ表示OFF（表示メニュー）の間は更新も描画もしない
	if ( skinnedObj_ && EditorManager::GetInstance()->IsDemoVisible() ) {
		skinnedObj_->SetPlaybackSpeed(1.0f);
		skinnedObj_->Update();
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
		// 構え中は1個を手に持っている扱い＝列から1個減らして見せる。
		// キャンセルすると構えが解けて自動的に列へ戻る（実際の消費は投げた瞬間だけ）
		if ( aimThrow_.IsAiming() && held > 0 ) { --held; }
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
			std::string countText = "×" + std::to_string(held);
			if ( belly > 0 ) { countText += "（＋" + std::to_string(belly) + "）"; }
			eggCountText_->SetText(countText);
		}
		// コイン取得数（コインが1枚も置かれていないマップでは出さない）
		if ( coinCountText_ ) {
			coinCountText_->SetText(coinSystem_.TotalCount() > 0
				? "コイン " + std::to_string(coinSystem_.CollectedCount()) + "/" + std::to_string(coinSystem_.TotalCount())
				: "");
		}
	}

	PostEffect::GetInstance()->Update();

	// パーティクル
	ParticleManager::GetInstance()->Update(camera_.get());

	// 展示物（オーラ・回転キューブ・SDF卵・ブロック群・GPUパーティクル）の見た目更新
	//   デモ表示OFF（表示メニュー）の間はスキップ
	if ( EditorManager::GetInstance()->IsDemoVisible() ) {
		demo_.UpdateVisuals(input, camera_.get());
	}
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
	const bool demoVisible = EditorManager::GetInstance()->IsDemoVisible(); // デモ展示のON/OFF（表示メニュー）
	for ( auto& obj : object3ds_ ) { obj->Draw(); }
	if ( demoVisible ) { demo_.DrawOpaque(); }  // 展示物（回転キューブ）
	if ( playerObj_ ) { playerObj_->Draw(); }   // プレイヤー（恐竜マスコット）
	if ( heldEggVisible_ && heldEggObj_ ) { heldEggObj_->Draw(); } // 構え中の手持ち卵
	if ( demoVisible && skinnedObj_ ) { skinnedObj_->Draw(); } // 見本の人形（Skinning展示）
	enemyMgr_.Draw();                           // 敵
	eggSystem_.Draw();                          // ヨッシーの卵
	coinSystem_.Draw();                         // 収集物（コイン）
	swallow_.Draw();                            // 舌（伸ばす/引き込む動作中だけ）

	// レール下の道メッシュ（クラフト風の地面）。Edit/Play どちらでも見える「本番の見た目」
	roadMesh_.Draw();

	// レール経路の緑線マーカーは「エディット中だけ」表示する。
	//   Play開始やリリース版（初期モードがPlay）ではレールの線が全て消え、道メッシュだけが残る。
	if ( EditorManager::GetInstance()->GetMode() == EngineMode::Edit ) {
		railField_.DrawMarkers();
	}

	EditorManager::GetInstance()->Draw();

	// --- インスタンシングの3D描画（展示物：ブロック一括）---
	if ( demoVisible ) { demo_.DrawInstanced(camera_.get()); }
	blockSystem_.Draw(camera_.get()); // 配置ブロック（見た目グループごとに1ドローコール）

	/*if ( skybox_ ) {
		skybox_->Draw(commandList, camera_.get());
	}*/

	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	// ⭕️ 3. 最後に「透明・加算合成」のものを描く！！！（順番超大事）
	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	if ( demoVisible ) { demo_.DrawAdditive(); } // 展示物（オーラ2種＋SpaceデモのHitEffect）
	combat_.Draw(); // 踏みつけ/命中の立体エフェクト

	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);

	// --- SDFボリューム（レイマーチング）---
	//   専用PSOに切り替えるので、通常のObj3d描画が全部終わった後に描く
	if ( demoVisible ) { demo_.DrawSdf(commandList); } // 展示物（SDF卵のエロージョン/モーフデモ）
	eggSystem_.DrawBirthFx(commandList);   // 産卵エロージョン演出中のSDF卵
	combat_.DrawDissolveFx(commandList);   // 倒された敵がSDFで溶けて消える演出
	dissolveRoad_.Draw(commandList);       // SDF溶け道（近づくと現れる道パネル）
	swallow_.DrawEatFx(commandList);       // 舌で捕まえた敵がSDFで溶けて消える演出
	SDFManager::GetInstance()->DrawVolumes(commandList); // エディタで配置した3Dボリューム

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
	//     Bloom有効時は合成RT、無効時は PostEffect の最終RTが finalSrv の実体なので、
	//     どちらの場合も「FinalBlit が読むテクスチャ」へ焼き込めばフルスクリーンにも映る
	RenderTexture* sdfTarget = Bloom::GetInstance()->IsEnabled()
		? Bloom::GetInstance()->GetCombineTexture()
		: PostEffect::GetInstance()->GetFinalTexture();
	SDFManager::GetInstance()->DrawIntoTexture(commandList, sdfTarget);

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
		for ( auto& slotSprite : eggHudSlots_ )   { slotSprite->Draw(); }
		for ( auto& bellyIcon : bellyHudIcons_ ) { bellyIcon->Draw(); }
		// 保持数の数字（SDFText。スプライトと同じターゲットへ描く）
		if ( eggCountText_ ) {
			SDFManager::GetInstance()->DrawTextItem(commandList, *eggCountText_, "jpdot");
		}
		// コイン取得数（コインがあるマップだけ文字が入っている）
		if ( coinCountText_ && coinSystem_.TotalCount() > 0 ) {
			SDFManager::GetInstance()->DrawTextItem(commandList, *coinCountText_, "jpdot");
		}
	}

	TextManager::GetInstance()->Draw();
}

void GamePlayScene::DrawDebugUI(){

#ifdef USE_IMGUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

	// 展示物のパネル（SDF卵のエロージョン操作）。デモ表示OFFの間はウィンドウごと出さない
	if ( EditorManager::GetInstance()->IsDemoVisible() ) { demo_.DrawImGui(); }

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

	// --- 道の設定（危険帯の長さ／両面描画／再生成）---
	ImGui::Separator();
	ImGui::TextDisabled("道の設定:");
	{
		bool roadVisible = roadMesh_.IsVisible();
		if ( ImGui::Checkbox("道を表示", &roadVisible) ) { roadMesh_.SetVisible(roadVisible); }

		bool cullNone = roadMesh_.IsCullNone();
		if ( ImGui::Checkbox("両面描画（OFF=背面カリングで軽量化）", &cullNone) ) {
			roadMesh_.SetCullNone(cullNone); // 即時反映（再生成不要）
		}

		int cornerStyle = roadMesh_.GetCornerStyle();
		const char* cornerStyleLabels[] = { "自動（角度で判定）", "いつも丸広場（ヨッシー風）", "丸なし（角ばり）" };
		ImGui::SetNextItemWidth(200.0f);
		if ( ImGui::Combo("曲がり角の形", &cornerStyle, cornerStyleLabels, 3) ) {
			roadMesh_.SetCornerStyle(cornerStyle);
			roadMesh_.Build(railField_.GetRails(), camera_.get()); // 選んだ瞬間に道を作り直して反映
		}
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("レールが曲がって繋がる角の見た目：\n 自動＝鋭い角はマイター、大きく回る角は丸広場\n いつも丸広場＝全部の角に丸い広場を出す\n 丸なし＝丸広場を出さず角ばった接続にする");

		float warnLength = roadMesh_.GetWarnLength();
		ImGui::SetNextItemWidth(160.0f);
		if ( ImGui::SliderFloat("危険帯の長さ(m)", &warnLength, 0.5f, 5.0f, "%.1f") ) {
			roadMesh_.SetWarnLength(warnLength);
		}
		// スライダーを離した時に道を作り直して反映（ドラッグ中の連続再生成はしない）
		if ( ImGui::IsItemDeactivatedAfterEdit() ) {
			roadMesh_.Build(railField_.GetRails(), camera_.get());
		}
		ImGui::SameLine();
		if ( ImGui::Button("道を再生成") ) {
			roadMesh_.Build(railField_.GetRails(), camera_.get());
			dissolveRoad_.Build(railField_.GetRails());
			coinSystem_.Sync(EditorManager::GetInstance()->GetEditorCoins(), railField_.GetRails());
			blockSystem_.Sync(EditorManager::GetInstance()->GetEditorBlocks(), &railField_.GetRails());
		}
		ImGui::Text("SDF溶け道: チェーン点 %d / 描画チャンク %d", dissolveRoad_.PieceCount(), dissolveRoad_.ActiveCount());
		ImGui::SetNextItemWidth(160.0f);
		ImGui::SliderFloat("プレイヤーモデル高さ補正(m)", &playerModelYOffset_, -0.6f, 0.6f, "%.2f");
		if ( ImGui::IsItemHovered() ) ImGui::SetTooltip("プレイヤーの足元と道の上面が合うように調整（マイナスで下がる）");
		ImGui::Text("道メッシュ/ピース数: %d", roadMesh_.TileCount());
		ImGui::Text("道の頂点数: %d / 三角形: %d", roadMesh_.VertexCount(), roadMesh_.TriangleCount());
	}


	// --- カメラ視点プリセット（レールを編集しやすく）---
	ImGui::Separator();
	ImGui::TextDisabled("カメラ視点プリセット:");
	if ( ImGui::Button("トップビュー（真上から）") && camera_ ) {
		// レール全体のXZ範囲を求めて、真上から全体が収まる高さに置く
		bool hasBounds = false;
		float minx = 0, maxx = 0, miny = 0, maxy = 0, minz = 0, maxz = 0;
		for ( const auto& rail : railField_.GetRails() ) {
			for ( const auto& node : rail.nodes ) {
				if ( !hasBounds ) { minx = maxx = node.x; miny = maxy = node.y; minz = maxz = node.z; hasBounds = true; } else {
					if ( node.x < minx ) minx = node.x; if ( node.x > maxx ) maxx = node.x;
					if ( node.y < miny ) miny = node.y; if ( node.y > maxy ) maxy = node.y;
					if ( node.z < minz ) minz = node.z; if ( node.z > maxz ) maxz = node.z;
				}
			}
		}
		float centerX = 0, centerY = 0, centerZ = 0, extent = 10.0f;
		if ( hasBounds ) {
			centerX = ( minx + maxx ) * 0.5f; centerY = ( miny + maxy ) * 0.5f; centerZ = ( minz + maxz ) * 0.5f;
			float extentX = maxx - minx, extentZ = maxz - minz;
			extent = ( extentX > extentZ ) ? extentX : extentZ;
		}
		float cameraHeight = extent * 1.5f + 8.0f; // 全体が画面に収まる高さ（足りなければホイールでズーム）
		camera_->SetTranslation({ centerX, centerY + cameraHeight, centerZ });
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
		// レールエディタで選択中のノード位置（レール番号＋距離）を「選択ノードに配置」用に渡す
		int pickRail = -1; float pickDist = 0.0f; bool hasPick = false;
		Vector3 pickNodePos {};
		if ( EditorManager::GetInstance()->GetEditorSelectedNode(pickRail, pickNodePos) ) {
			const auto& rails = railField_.GetRails();
			if ( pickRail >= 0 && pickRail < ( int ) rails.size() && rails[pickRail].nodes.size() >= 2 ) {
				pickDist = rails[pickRail].GetClosestDistance(pickNodePos);
				hasPick = true;
			}
		}
		enemyEditor_->DrawWindow(railField_.GetRails(), pickRail, pickDist, hasPick);

		// --- Game View 連携（エディット中のみ）：色分けピン・巡回範囲・直接ドラッグ ---
		if ( EditorManager::GetInstance()->GetMode() == EngineMode::Edit ) {
			const auto& gv = EditorManager::GetInstance()->GetGameViewMouse();
			const auto& rails = railField_.GetRails();
			auto& spawnDatas = enemyEditor_->MutableSpawnDatas();

			// 敵のワールド位置（レール上＋少し浮かせた高さ）
			auto enemyWorldPos = [&](const EnemySpawnData& spawnData, Vector3& out) -> bool {
				if ( spawnData.railIndex < 0 || spawnData.railIndex >= ( int ) rails.size() ) return false;
				if ( rails[spawnData.railIndex].nodes.size() < 2 ) return false;
				Vector3 p = rails[spawnData.railIndex].GetPositionByDistance(spawnData.distance);
				out = { p.x, p.y + 0.5f, p.z };
				return true;
			};
			// world→Game View スクリーン座標
			auto projectToScreen = [&](const Vector3& worldPos, Vector2& out) -> bool {
				Vector2 ndc;
				if ( !WorldToNdc(worldPos, gv.viewProj, ndc) ) return false;
				out.x = gv.imgMin.x + ( ndc.x * 0.5f + 0.5f ) * gv.imgSize.x;
				out.y = gv.imgMin.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * gv.imgSize.y;
				return true;
			};

			// 1) 種類別の色分けピン（Zako=赤 / Strong=紫）＋パトロールの巡回範囲（橙線）
			for ( const auto& spawnData : spawnDatas ) {
				Vector3 wp;
				if ( !enemyWorldPos(spawnData, wp) ) continue;
				Vector4 pinColor = ( spawnData.type == EnemyType::Zako )
					? Vector4 { 1.0f, 0.35f, 0.25f, 1.0f }   // 赤（雑魚）
					: Vector4 { 0.75f, 0.4f, 1.0f, 1.0f };   // 紫（強敵）
				DebugDraw::GetInstance()->Line({ wp.x, wp.y + 0.4f, wp.z }, { wp.x, wp.y + 1.1f, wp.z }, pinColor);
				DebugDraw::GetInstance()->Sphere({ wp.x, wp.y + 1.2f, wp.z }, 0.16f, pinColor, 10);

				// 巡回範囲：レールに沿った橙線（範囲指定なしのパトロールはレール全体）
				if ( spawnData.patrol ) {
					const SplineRail& rail = rails[spawnData.railIndex];
					float len = rail.GetLength();
					float lo = ( spawnData.patrolMin >= 0.0f ) ? ( std::min )( spawnData.patrolMin, len ) : 0.0f;
					float hi = ( spawnData.patrolMax >= 0.0f ) ? std::clamp(spawnData.patrolMax, lo, len) : len;
					int steps = std::clamp(( int ) ( ( hi - lo ) / 0.5f ), 1, 200);
					Vector3 prev = rail.GetPositionByDistance(lo);
					for ( int s = 1; s <= steps; ++s ) {
						Vector3 cur = rail.GetPositionByDistance(lo + ( hi - lo ) * ( float ) s / ( float ) steps);
						DebugDraw::GetInstance()->Line({ prev.x, prev.y + 0.15f, prev.z },
						                               { cur.x,  cur.y + 0.15f,  cur.z }, { 1.0f, 0.6f, 0.15f, 1.0f });
						prev = cur;
					}
				}
			}

			// 2) マウス直下の敵を探す（スクリーン距離16px以内。ドラッグのつかみ判定）
			//    ブロック配置モード中はクリックをブロック側に譲る（敵をつかまない）
			int mouseOverEnemy = -1;
			if ( gv.hovered && !gv.gizmoActive && enemyDragIdx_ < 0
				&& !EditorManager::GetInstance()->IsEditorBlockPaintMode() ) {
				float bestPick = 16.0f;
				for ( int i = 0; i < ( int ) spawnDatas.size(); ++i ) {
					Vector3 wp; if ( !enemyWorldPos(spawnDatas[i], wp) ) continue;
					Vector2 sp; if ( !projectToScreen(wp, sp) ) continue;
					float dx = sp.x - gv.mousePos.x, dy = sp.y - gv.mousePos.y;
					float d = std::sqrt(dx * dx + dy * dy);
					if ( d < bestPick ) { bestPick = d; mouseOverEnemy = i; }
				}
			}
			if ( mouseOverEnemy >= 0 ) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }

			// 3) つかむ → ドラッグでレール上を移動（別レールへの乗せ替えも可）→ 離して確定
			if ( mouseOverEnemy >= 0 && ImGui::IsMouseClicked(0) ) {
				enemyDragIdx_ = mouseOverEnemy;
				enemyEditor_->SetSelectedEntry(mouseOverEnemy);
			}
			if ( enemyDragIdx_ >= 0 ) {
				if ( enemyDragIdx_ >= ( int ) spawnDatas.size() ) {
					enemyDragIdx_ = -1; // ドラッグ中に削除された等
				} else if ( ImGui::IsMouseDown(0) ) {
					// マウスにいちばん近いレール上の点を探す（0.5m刻みのサンプリング）
					int bestRail = -1; float bestDist = 0.0f; float bestPx = 40.0f;
					for ( int rr = 0; rr < ( int ) rails.size(); ++rr ) {
						const SplineRail& rail = rails[rr];
						if ( rail.nodes.size() < 2 ) continue;
						float len = rail.GetLength();
						int steps = std::clamp(( int ) ( len / 0.5f ), 2, 400);
						for ( int s = 0; s <= steps; ++s ) {
							float dist = len * ( float ) s / ( float ) steps;
							Vector3 wp = rail.GetPositionByDistance(dist);
							Vector2 sp; if ( !projectToScreen({ wp.x, wp.y + 0.5f, wp.z }, sp) ) continue;
							float dx = sp.x - gv.mousePos.x, dy = sp.y - gv.mousePos.y;
							float d = std::sqrt(dx * dx + dy * dy);
							if ( d < bestPx ) { bestPx = d; bestRail = rr; bestDist = dist; }
						}
					}
					if ( bestRail >= 0 ) {
						spawnDatas[enemyDragIdx_].railIndex = bestRail;
						spawnDatas[enemyDragIdx_].distance  = bestDist;
					}
					// ゴースト表示（確定はマウスを離した時。ドラッグ中の毎フレームリスポーンを避ける）
					Vector3 wp;
					if ( enemyWorldPos(spawnDatas[enemyDragIdx_], wp) ) {
						DebugDraw::GetInstance()->Sphere(wp, 0.9f, { 0.3f, 1.0f, 0.6f, 1.0f });
					}
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				} else {
					// マウスを離した：確定（リスポーン＋マップ保存データへ反映）
					enemyEditor_->MarkChanged();
					enemyDragIdx_ = -1;
				}
			}

			// 4) 一覧でホバー/選択中の敵のハイライト（黄=ホバー / 水色=選択）
			auto drawEnemyMarker = [&](int idx, const Vector4& color){
				if ( idx < 0 || idx >= ( int ) spawnDatas.size() ) return;
				Vector3 wp;
				if ( enemyWorldPos(spawnDatas[idx], wp) ) {
					DebugDraw::GetInstance()->Sphere(wp, 0.8f, color);
				}
			};
			drawEnemyMarker(enemyEditor_->GetHoveredEntry(),  { 1.0f, 1.0f, 0.2f, 1.0f });
			drawEnemyMarker(enemyEditor_->GetSelectedEntry(), { 0.3f, 0.8f, 1.0f, 1.0f });

			// 敵の上にいる間/ドラッグ中はレール編集のマウス操作を止める（両方掴む事故防止）
			EditorManager::GetInstance()->SetExternalDragActive(enemyDragIdx_ >= 0 || mouseOverEnemy >= 0);
		} else {
			enemyDragIdx_ = -1;
			EditorManager::GetInstance()->SetExternalDragActive(false);
		}
	}

	// --- ブロック配置モード（マリオメーカー風：クリックで置く/消す。配置物タブでON）---
	if ( EditorManager::GetInstance()->GetMode() == EngineMode::Edit
		&& EditorManager::GetInstance()->IsEditorBlockPaintMode() ) {
		const auto& gv = EditorManager::GetInstance()->GetGameViewMouse();
		const auto& rails = railField_.GetRails();
		auto projectToScreen = [&](const Vector3& worldPos, Vector2& out) -> bool {
			Vector2 ndc;
			if ( !WorldToNdc(worldPos, gv.viewProj, ndc) ) return false;
			out.x = gv.imgMin.x + ( ndc.x * 0.5f + 0.5f ) * gv.imgSize.x;
			out.y = gv.imgMin.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * gv.imgSize.y;
			return true;
		};

		// 1) マウスに一番近いレール（0.25m刻みサンプル → 1mセルへ吸着）。
		//    高い段(level 7=+7m)や横(side±2)も選べるよう、レール線1点ではなく
		//    「そのサンプル位置に立つ断面の縦線segment（+0.5〜+7.5m）」との画面距離で判定する。
		//    ※縦線だけで選ぶと、画面上で柱が重なる「1本奥の並走レール」に吸着して
		//      見ている道からずれた場所に置かれる事故が起きる。そこで
		//      足元（レール線）の近さも加点し、指している道のレールが勝つようにする
		int bestRail = -1; float bestDist = 0.0f; float bestPx = 220.0f;
		if ( gv.hovered && !gv.gizmoActive ) {
			for ( int rr = 0; rr < ( int ) rails.size(); ++rr ) {
				const SplineRail& rail = rails[rr];
				if ( rail.nodes.size() < 2 ) continue;
				if ( !rail.visible ) continue; // 見えない骨組み（リフトのガイド等）には置かない
				float len = rail.GetLength();
				int steps = std::clamp(( int ) ( len / 0.25f ), 2, 800);
				for ( int s = 0; s <= steps; ++s ) {
					float dist = len * ( float ) s / ( float ) steps;
					Vector3 wp = rail.GetPositionByDistance(dist);
					Vector2 spLow, spHigh;
					if ( !projectToScreen({ wp.x, wp.y + 0.5f, wp.z }, spLow ) ) continue;
					if ( !projectToScreen({ wp.x, wp.y + 7.5f, wp.z }, spHigh) ) continue;
					// マウス→2D線分（断面の縦線）の距離
					float vx = spHigh.x - spLow.x, vy = spHigh.y - spLow.y;
					float len2 = vx * vx + vy * vy;
					float t = ( len2 > 1e-6f )
						? std::clamp((( gv.mousePos.x - spLow.x ) * vx + ( gv.mousePos.y - spLow.y ) * vy) / len2, 0.0f, 1.0f)
						: 0.0f;
					float cx = spLow.x + vx * t, cy = spLow.y + vy * t;
					float dx = cx - gv.mousePos.x, dy = cy - gv.mousePos.y;
					float segDistPx = std::sqrt(dx * dx + dy * dy);
					// 足元の近さ（上限つき＝高い段を選ぶ操作は妨げない）
					float bx = spLow.x - gv.mousePos.x, by = spLow.y - gv.mousePos.y;
					float baseDistPx = std::sqrt(bx * bx + by * by);
					float score = segDistPx + ( std::min )( baseDistPx, 160.0f ) * 0.5f;
					// 塗り始めたレールに吸い付く（ドラッグ中に隣のレールへ飛び移らない）
					if ( rr == lastPaintRail_ || ( rectFillActive_ && rr == rectFillRail_ ) ) { score -= 25.0f; }
					if ( score < bestPx ) { bestPx = score; bestRail = rr; bestDist = dist; }
				}
			}
		}

		if ( bestRail >= 0 ) {
			const SplineRail& rail = rails[bestRail];
			// 1mグリッドの「整数セル」へ吸着する。レール長で丸めずクランプすると
			// 端だけ半端な距離のセルが生まれ、隣と重なった二重ブロックが置けてしまう
			float railLen  = rail.GetLength();

			// どのレールに置かれるかが一目で分かるように、対象レールを黄色でなぞる
			{
				int highlightSteps = std::clamp(( int ) railLen, 1, 200);
				Vector3 prevPoint = rail.GetPositionByDistance(0.0f);
				for ( int s = 1; s <= highlightSteps; ++s ) {
					Vector3 curPoint = rail.GetPositionByDistance(railLen * ( float ) s / ( float ) highlightSteps);
					DebugDraw::GetInstance()->Line({ prevPoint.x, prevPoint.y + 0.05f, prevPoint.z },
					                               { curPoint.x, curPoint.y + 0.05f, curPoint.z },
					                               { 1.0f, 0.9f, 0.2f, 1.0f });
					prevPoint = curPoint;
				}
			}
			float cellDist = std::round(bestDist);
			if ( cellDist < 0.0f )    { cellDist = 0.0f; }
			if ( cellDist > railLen ) { cellDist = std::floor(railLen); }

			Vector3 base    = rail.GetPositionByDistance(cellDist);
			Vector3 tangent = rail.GetTangentByDistance(cellDist);
			// 道幅方向（水平の右）。BlockSystem::BlockWorldPos と同じ求め方に揃える
			float horizLen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
			Vector3 right { 0.0f, 0.0f, 0.0f };
			if ( horizLen > 1e-4f ) { right = { tangent.z / horizLen, 0.0f, -tangent.x / horizLen }; }

			// 2) 断面のセル（段 × 横ずれ）から、マウスに一番近いものを選ぶ。
			//    マウスを上へ動かせば高い段、横へ動かせば道の脇（飾り/壁）に置ける。
			//    横ずれセルにはペナルティを足して、迷ったら「乗れる中心線(side=0)」を優先。
			//    既にブロックがあるセルはボーナスで選ばれやすく＝「消したい/積みたいブロック」を狙いやすくする
			int   bestLevel = 0;
			float bestSide  = 0.0f;
			float bestCellPx = 1e9f;
			for ( int level = 0; level < 8; ++level ) {
				for ( int sideStep = -2; sideStep <= 2; ++sideStep ) {
					float side = ( float ) sideStep;
					Vector3 center = { base.x + right.x * side,
					                   base.y + ( float ) level * BlockSystem::kSize + 0.5f + BlockSystem::kSurfaceY,
					                   base.z + right.z * side };
					Vector2 sp; if ( !projectToScreen(center, sp) ) continue;
					float dx = sp.x - gv.mousePos.x, dy = sp.y - gv.mousePos.y;
					float d = std::sqrt(dx * dx + dy * dy) + std::abs(side) * 14.0f; // 横はやや選ばれにくく
					if ( EditorManager::GetInstance()->HasEditorBlock(bestRail, cellDist, level, side) ) {
						d -= 12.0f; // 既存ブロックのセルを優先（削除・種類替えの狙い撃ち用）
					}
					if ( d < bestCellPx ) { bestCellPx = d; bestLevel = level; bestSide = side; }
				}
			}

			bool cellOccupied = EditorManager::GetInstance()->HasEditorBlock(bestRail, cellDist, bestLevel, bestSide);
			bool eraseMode    = EditorManager::GetInstance()->IsEditorBlockEraseMode();
			int  paintType    = EditorManager::GetInstance()->GetEditorBlockPaintType();
			int  paintShape   = EditorManager::GetInstance()->GetEditorBlockPaintShape(); // 0=1個/1=柱/2=階段

			// 3) ゴースト表示（緑=ここに置く / 赤=クリックで消す / 水色=道の脇＝当たらない飾り）
			Vector3 ghostCenter = { base.x + right.x * bestSide,
			                        base.y + ( float ) bestLevel * BlockSystem::kSize + 0.5f + BlockSystem::kSurfaceY,
			                        base.z + right.z * bestSide };
			Vector4 ghostColor;
			if ( cellOccupied || eraseMode ) { ghostColor = { 1.0f, 0.35f, 0.3f, 1.0f }; }  // 消す
			else if ( bestSide != 0.0f )     { ghostColor = { 0.35f, 0.8f, 1.0f, 1.0f }; }  // 飾り（乗れない）
			else                             { ghostColor = { 0.3f, 1.0f, 0.5f, 1.0f }; }   // 置く（乗れる）
			DebugDraw::GetInstance()->Box(ghostCenter, { 1.0f, 1.0f, 1.0f }, ghostColor);
			// 横ずれセルの時は中心線との対応が分かるように足元へ線を引く
			if ( bestSide != 0.0f ) {
				DebugDraw::GetInstance()->Line(ghostCenter,
					{ base.x, base.y + ( float ) bestLevel * BlockSystem::kSize + 0.5f + BlockSystem::kSurfaceY, base.z }, ghostColor);
			}
			// 柱モード：下まで埋まる範囲を薄い枠で予告する
			if ( paintShape == 1 ) {
				Vector4 dim = { ghostColor.x, ghostColor.y, ghostColor.z, 0.35f };
				for ( int lv = 0; lv < bestLevel; ++lv ) {
					DebugDraw::GetInstance()->Box(
						{ ghostCenter.x, base.y + ( float ) lv * BlockSystem::kSize + 0.5f + BlockSystem::kSurfaceY, ghostCenter.z },
						{ 0.9f, 0.9f, 0.9f }, dim);
				}
			}

			// 4) クリックで適用。押した瞬間に「置く/消す」を決めて、押しっぱなしで連続適用（ペイント）。
			//    右クリック＝常に消す / 消しゴムモード＝左クリックでも消す / ブロックを左クリック＝消す
			auto applyPaint = [&](int rail_, float dist_, int level_, float side_){
				// 配置ズレ調査用ログ（1配置=1行。デバッガ出力＋ファイルにも追記）
				char paintLog[256];
				snprintf(paintLog, sizeof(paintLog),
					"BlockPaint: rail=%d dist=%.1f lvl=%d side=%.1f erase=%d | mouse=(%.0f,%.0f) img=(%.0f,%.0f)+(%.0fx%.0f) pickPx=%.1f\n",
					rail_, dist_, level_, side_, blockPaintErasing_ ? 1 : 0,
					gv.mousePos.x, gv.mousePos.y, gv.imgMin.x, gv.imgMin.y, gv.imgSize.x, gv.imgSize.y, bestPx);
				OutputDebugStringA(paintLog);
				EditorManager* editorManager = EditorManager::GetInstance();
				if ( blockPaintErasing_ ) {
					if ( paintShape == 1 ) {
						// 柱消し：そのセルの縦一列をまとめて消す
						for ( int lv = 0; lv < 8; ++lv ) { editorManager->RemoveEditorBlock(rail_, dist_, lv, side_); }
					} else {
						editorManager->RemoveEditorBlock(rail_, dist_, level_, side_);
					}
				} else {
					if ( paintShape == 1 ) {
						// 柱：クリックした段から地面まで縦に埋める（塔・壁の土台が1クリック）
						for ( int lv = 0; lv <= level_; ++lv ) { editorManager->AddEditorBlock(rail_, dist_, lv, side_, paintType); }
					} else {
						editorManager->AddEditorBlock(rail_, dist_, level_, side_, paintType);
					}
				}
				lastPaintRail_ = rail_; lastPaintDist_ = dist_; lastPaintLevel_ = level_; lastPaintSide_ = side_;
			};
			// 範囲フィル：ドラッグ中は終点を更新して、塗られる矩形を薄枠で予告する
			if ( paintShape == 3 && rectFillActive_ && rectFillRail_ == bestRail ) {
				rectFillEndDist_  = cellDist;
				rectFillEndLevel_ = bestLevel;
				float rd0 = ( std::min )( rectFillDist_, rectFillEndDist_ );
				float rd1 = ( std::min )( ( std::max )( rectFillDist_, rectFillEndDist_ ), rd0 + 32.0f );
				int   rl0 = ( std::min )( rectFillLevel_, rectFillEndLevel_ );
				int   rl1 = ( std::max )( rectFillLevel_, rectFillEndLevel_ );
				Vector4 dim = rectFillErase_ ? Vector4 { 1.0f, 0.4f, 0.35f, 0.5f }
				                             : Vector4 { 0.35f, 1.0f, 0.55f, 0.5f };
				for ( float d = rd0; d <= rd1 + 0.5f; d += 1.0f ) {
					Vector3 cellBase = rail.GetPositionByDistance(std::clamp(d, 0.0f, railLen));
					Vector3 cellTan  = rail.GetTangentByDistance(std::clamp(d, 0.0f, railLen));
					float hl = std::sqrt(cellTan.x * cellTan.x + cellTan.z * cellTan.z);
					Vector3 cellRight { 0.0f, 0.0f, 0.0f };
					if ( hl > 1e-4f ) { cellRight = { cellTan.z / hl, 0.0f, -cellTan.x / hl }; }
					for ( int lv = rl0; lv <= rl1; ++lv ) {
						DebugDraw::GetInstance()->Box(
							{ cellBase.x + cellRight.x * rectFillSide_,
							  cellBase.y + ( float ) lv * BlockSystem::kSize + 0.5f + BlockSystem::kSurfaceY,
							  cellBase.z + cellRight.z * rectFillSide_ },
							{ 0.92f, 0.92f, 0.92f }, dim);
					}
				}
			}

			if ( gv.hovered && !gv.gizmoActive ) {
				if ( ImGui::IsMouseClicked(1) ) {
					// 右クリック＝そのセルを1個だけ消す（ドラッグはカメラ回転と衝突するので単発のみ）
					EditorManager::GetInstance()->RemoveEditorBlock(bestRail, cellDist, bestLevel, bestSide);
				} else if ( ImGui::IsMouseClicked(0) ) {
					if ( paintShape == 3 ) {
						// 範囲フィル：始点を記録（適用はボタンを離した時にまとめて）
						rectFillActive_   = true;
						rectFillErase_    = eraseMode || cellOccupied;
						rectFillRail_     = bestRail;
						rectFillDist_     = cellDist;
						rectFillLevel_    = bestLevel;
						rectFillSide_     = bestSide;
						rectFillEndDist_  = cellDist;
						rectFillEndLevel_ = bestLevel;
					} else {
						blockPaintErasing_ = eraseMode || cellOccupied;
						applyPaint(bestRail, cellDist, bestLevel, bestSide);
					}
				} else if ( ImGui::IsMouseDown(0) && lastPaintLevel_ >= 0
					&& ImGui::IsMouseDragging(0, 6.0f)      // クリックの手ぶれ（数px）では連続配置しない
					&& bestRail == lastPaintRail_ ) {        // ドラッグ中に別レールへ飛び移って撒き散らさない
					// 階段モード中は段の違いを無視して「同じマスか」だけを見る（段はこちらで決めるため）
					bool sameCell = ( lastPaintRail_ == bestRail
						&& std::abs(lastPaintSide_ - bestSide) < 0.5f
						&& std::abs(lastPaintDist_ - cellDist) < 0.5f
						&& ( paintShape == 2 || lastPaintLevel_ == bestLevel ) );
					if ( !sameCell ) {
						if ( paintShape == 2 && !blockPaintErasing_ ) {
							// 階段：1マス進むごとに1段ずつ高くする（ドラッグするだけで階段が生える）
							int stairLevel = std::clamp(lastPaintLevel_ + 1, 0, 7);
							applyPaint(bestRail, cellDist, stairLevel, bestSide);
						} else {
							// 速くドラッグするとフレーム間でセルが飛ぶ。同じ段・同じ横位置なら
							// 間のセルも埋めて、線を引くように途切れず塗れるようにする。
							// ただし1フレームの補間は8マスまで（画面外→遠くへ復帰した時などに
							// レール全長ぶん一気に塗ってしまう事故を防ぐ）
							if ( lastPaintRail_ == bestRail && lastPaintLevel_ == bestLevel
								&& std::abs(lastPaintSide_ - bestSide) < 0.5f
								&& std::abs(cellDist - lastPaintDist_) <= 8.5f ) {
								float from = lastPaintDist_, to = cellDist;
								float step = ( to >= from ) ? 1.0f : -1.0f;
								for ( float d = from + step; std::abs(d - to) > 0.5f; d += step ) {
									applyPaint(bestRail, d, bestLevel, bestSide);
								}
							}
							applyPaint(bestRail, cellDist, bestLevel, bestSide);
						}
					}
				}
			}
		}
		// 範囲フィルの確定：ボタンを離した瞬間に矩形をまとめて塗る/消す
		//   （マウスがレールから外れていても、最後に指していたセルまでを適用）
		if ( rectFillActive_ && !ImGui::IsMouseDown(0) ) {
			int   fillType = EditorManager::GetInstance()->GetEditorBlockPaintType();
			float d0 = ( std::min )( rectFillDist_, rectFillEndDist_ );
			float d1 = ( std::min )( ( std::max )( rectFillDist_, rectFillEndDist_ ), d0 + 32.0f ); // 事故防止の上限
			int   l0 = ( std::min )( rectFillLevel_, rectFillEndLevel_ );
			int   l1 = ( std::max )( rectFillLevel_, rectFillEndLevel_ );
			for ( float d = d0; d <= d1 + 0.5f; d += 1.0f ) {
				for ( int lv = l0; lv <= l1; ++lv ) {
					if ( rectFillErase_ ) { EditorManager::GetInstance()->RemoveEditorBlock(rectFillRail_, d, lv, rectFillSide_); }
					else                  { EditorManager::GetInstance()->AddEditorBlock(rectFillRail_, d, lv, rectFillSide_, fillType); }
				}
			}
			rectFillActive_ = false;
		}

		// ボタンを離した時と、マウスが Game View から出た時はペイントの続きをリセット
		// （画面外で凍結した位置から遠くへ復帰した瞬間に大量補間しないように）
		if ( !ImGui::IsMouseDown(0) || !gv.hovered ) { lastPaintLevel_ = -1; }

		// 配置モード中はレール編集のクリックを止める（ノード選択やスタンプと競合しない）
		if ( gv.hovered ) { EditorManager::GetInstance()->SetExternalDragActive(true); }
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
	// SDFパネルへ差し込んだ溶け道設定UI（this をキャプチャ）も解除する
	SDFManager::GetInstance()->SetExtraPanelUI(nullptr);

	object3ds_.clear();
	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}