#include "GamePlayScene.h"
// --- ゲーム固有のファイル ---
#include "TitleScene.h"

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
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "engine/graphics/RenderTexture.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/utils/TextManager.h"
#include "Bloom.h"
#include "engine/3d/model/Model.h"
#include "engine/utils/EditorManager.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "game/player/Player.h"
#include "engine/rail/SplineRail.h"
#include "engine/utils/Level/LevelManager.h"
#include "engine/utils/Level/LevelEditor.h"


using namespace VectorMath;
using namespace MatrixMath;

// レール間の接続情報をロード時に1回だけ計算する
static void BuildRailConnections(std::vector<SplineRail>& rails){
	// --- 終端接続のリセット ---
	for ( auto& r : rails ){
		r.frontConnIndex = -1;
		r.backConnIndex  = -1;
		r.branchPoints.clear();
	}

	auto endDist = [](const Vector3& a, const Vector3& b) -> float{
		float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
		};

	// --- 終端接続（既存） ---
	for ( int i = 0; i < ( int ) rails.size(); ++i ){
		for ( int j = 0; j < ( int ) rails.size(); ++j ){
			if ( i == j ) continue;
			const Vector3& iFront = rails[i].nodes.front();
			const Vector3& iBack  = rails[i].nodes.back();
			const Vector3& jFront = rails[j].nodes.front();
			const Vector3& jBack  = rails[j].nodes.back();

			if ( rails[i].frontConnIndex < 0 ){
				if ( endDist(iFront, jFront) < 0.1f ){ rails[i].frontConnIndex = j; rails[i].frontConnToFront = true; } else if ( endDist(iFront, jBack) < 0.1f ){ rails[i].frontConnIndex = j; rails[i].frontConnToFront = false; }
			}
			if ( rails[i].backConnIndex < 0 ){
				if ( endDist(iBack, jFront) < 0.1f ){ rails[i].backConnIndex = j; rails[i].backConnToFront = true; } else if ( endDist(iBack, jBack) < 0.1f ){ rails[i].backConnIndex = j; rails[i].backConnToFront = false; }
			}
		}
	}

	// --- 途中分岐点の計算 ---
	// XY平面で branchXYThreshold 以内かつ Z方向に branchZMinDiff 以上離れたノード同士を分岐点として登録
	const float branchXYThreshold = 1.5f;
	const float branchZMinDiff    = 0.5f;

	for ( int i = 0; i < ( int ) rails.size(); ++i ){
		for ( size_t ni = 0; ni < rails[i].nodes.size(); ++ni ){
			// 乗り継ぎ元の末尾ノードはスキップ（terminalFiredでカバー済み）
			if ( ni == rails[i].nodes.size() - 1 ) continue;

			const Vector3& nodeI = rails[i].nodes[ni];

			for ( int j = 0; j < ( int ) rails.size(); ++j ){
				if ( j == i ) continue;

				for ( size_t nj = 0; nj < rails[j].nodes.size(); ++nj ){
					// 乗り換え先の末尾ノードはtotalLength_に飛ぶのでスキップ
					if ( nj == rails[j].nodes.size() - 1 ) continue;

					const Vector3& nodeJ = rails[j].nodes[nj];

					float dx = nodeI.x - nodeJ.x;
					float dy = nodeI.y - nodeJ.y;
					float xyDist = std::sqrt(dx * dx + dy * dy);
					if ( xyDist >= branchXYThreshold ) continue;

					float zDiff = nodeJ.z - nodeI.z;
					if ( std::abs(zDiff) < branchZMinDiff ) continue;

					SplineRail::BranchPoint bp;
					bp.distance   = rails[i].GetDistanceFromT(static_cast< float >( ni ));
					bp.targetRail = j;
					bp.targetDist = rails[j].GetDistanceFromT(static_cast< float >( nj ));
					bp.zSign      = ( zDiff > 0.0f ) ? 1 : -1;
					rails[i].branchPoints.push_back(bp);
				}
			}
		}
	}
}

// 初期化
void GamePlayScene::Initialize(){
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

	// 球モデル作成 (シングルトン)
	ModelManager::GetInstance()->CreateSphereModel("sphere", 16);


	ModelManager::GetInstance()->CreatePlaneModel("plane");


	// パーティクルグループ作成 (シングルトン)
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み
	textures_["uvChecker"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/uvChecker.png", commandList);
	textures_["monsterBall"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/monsterBall.png", commandList);
	textures_["fence"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/fence.png", commandList);
	textures_["circle"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle.png", commandList);
	textures_["circle2"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle2.png", commandList);
	textures_["noise0"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise0.png", commandList) };
	textures_["noise1"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise1.png", commandList) };
	textures_["gradationLine"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/gradationLine.png", commandList) };


	textures_["skybox"] = TextureManager::GetInstance()->LoadTextureAndCreateSRVCube("resources/StandardCubeMap.dds", commandList);

	Obj3dCommon::GetInstance()->SetEnvironmentTexture(textures_["skybox"].srvIndex);

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize("resources/StandardCubeMap.dds", commandList);


	// ※EditorManager::Initialize() は Framework::Initialize() で呼ばれるので、ここでは不要



	// モデル読み込み (シングルトン)
	// アニメーション
	ModelManager::GetInstance()->LoadModel("animatedCube", "resources/AnimatedCube", "AnimatedCube.gltf");
	testAnimation_ = LoadAnimationFromFile("resources/AnimatedCube", "AnimatedCube.gltf");
	ModelManager::GetInstance()->LoadModel("human", "resources/human", "walk.gltf");

	// 保存済みアニメーションを読み込んで再生するだけ
	skinnedAnimTrack_.LoadFromJson("resources/human_anim.json");

	// カメラ生成
	camera_ = std::make_unique<Camera>(windowProc->GetClientWidth(), windowProc->GetClientHeight(), dxCommon);
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });


	// デバッグカメラ生成
	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();

	// プレイヤーオブジェクト生成
	testObj_ = Obj3d::Create("animatedCube");
	testObj_->SetEnvironmentMap(textures_["skybox"].srvIndex);

	if ( testObj_ ){

		testObj_->SetPipelineType(PipelineType::Object3D_CullNone);

		testObj_->SetCamera(camera_.get());
		testObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });

		testObj_->SetNoiseTexture(textures_["noise0"].srvIndex);
		testObj_->SetDissolveThreshold(0.0f);

		Bloom::GetInstance()->SetTargetEmissivePower(&testObj_->GetModel()->GetMaterial()->emissive);

		// エディタのギズモ／インスペクタの操作対象として登録（master_engineの改善を取り込み）
		EditorManager::GetInstance()->SetGizmoTarget(testObj_.get());
	}

	// ★1. Ringモデルの生成
	ModelManager::GetInstance()->CreateRingModel("auraRing", 32, 1.0f, 0.2f);

	// ★2. 画像のリングを単体として出す
	auraObj_ = Obj3d::Create("auraRing");
	if ( auraObj_ ) {
		auraObj_->SetCamera(camera_.get());

		// テクスチャをセット
		auraObj_->GetModel()->SetTexture(textures_["gradationLine"].srvIndex);

		// ★ザラザラの原因を潰す！ノイズ枠にダミーとして同じ画像をセットしてディゾルブを無効化
		auraObj_->SetNoiseTexture(textures_["gradationLine"].srvIndex);

		// ライティングで真っ黒になるのを防ぐ
		auraObj_->GetModel()->GetMaterial()->enableLighting = 0;

		// ★地面に魔法陣のように広がるように、X軸で90度(1.57ラジアン)倒す
		auraObj_->SetRotation({ 1.5708f, 0.0f, 0.0f });
		// 床に埋まらないように少しだけ上に浮かす
		auraObj_->SetTranslation({ 0.0f, 0.1f, 5.0f });

		// 加算合成で綺麗に光らせる
		auraObj_->SetPipelineType(PipelineType::Object3D_Additive);
	}

	// 1. 円柱（Cylinder）モデルを生成（名前: "auraCylinderModel"、分割数32、半径1.5、高さ4.0）
	ModelManager::GetInstance()->CreateCylinderModel("auraCylinderModel", 32, 1.5f, 4.0f);

	auraCylinderObj_ = Obj3d::Create("auraCylinderModel");
	if ( auraCylinderObj_ ) {
		auraCylinderObj_->SetCamera(camera_.get());

		// ⭕️ 画像は .png のままでOK！
		auraCylinderObj_->GetModel()->SetTexture(textures_["gradationLine"].srvIndex);

	
		// ⭕️ 絶対にディゾルブ（透明化）させないように、閾値をマイナスにしておく
		auraCylinderObj_->SetDissolveThreshold(-1.0f);

		// 影で真っ黒になるのを防ぐ
		auraCylinderObj_->GetModel()->GetMaterial()->enableLighting = 0;

		// テストObjと被らないように横にズラす
		auraCylinderObj_->SetTranslation({ -5.0f, 2.0f, 5.0f });

		// 加算合成
		auraCylinderObj_->SetPipelineType(PipelineType::Object3D_Additive);
	}
	
	
	skinnedObj_ = SkinnedObj3d::Create("human", "resources/human", "walk.gltf");
	skinnedObj_->SetEnvironmentMap(textures_["skybox"].srvIndex);
	skinnedObj_->SetCamera(camera_.get());
	skinnedObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });
	skinnedObj_->SetScale({ 1.0f, 1.0f, 1.0f });
	skinnedObj_->SetRotation({ 0.0f, 3.14159f, 0.0f });

	EditorManager::GetInstance()->SetTargetSkinnedObj(skinnedObj_.get());

	// デプスステンシル作成 (TextureManagerシングルトン)
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(), windowProc->GetClientHeight()
	);



	EditorManager::GetInstance()->SetCamera(camera_.get());

	sprite_ = Sprite::Create(textures_["uvChecker"].srvIndex, spritePos_);



	blockGroup_ = std::make_unique<InstancedGroup>();
	blockGroup_->Initialize("block", 10000);
	blockGroup_->SetNoiseTexture(textures_["uvChecker"].srvIndex);


	// GPUパーティクル初期化 (テクスチャを指定する)
	GPUParticleManager::GetInstance()->Initialize(
		dxCommon, SrvManager::GetInstance(), "resources/uvChecker.png");


	// エミッターの初期設定
	GPUParticleEmitterData emitterData;
	emitterData.position = { 0.0f, 0.0f, 0.0f };
	emitterData.emitRate = 20.0f;
	emitter_.SetData(emitterData);

	EditorManager::GetInstance()->SetParticleEmitter(&emitter_);




	// --- プレイヤーの初期化 ---
	player_ = std::make_unique<Player>();
	player_->Initialize();


	// 1. JSONからマップデータを読み込む
	LevelData mapData = LevelManager::GetInstance()->Load("resources/map/map01.json");

	// 2. 読み込んだ複数のレールの座標を、SplineRailにセットする
	splineRails_.clear();
	for ( const auto& line : mapData.railLines ) {
		SplineRail newRail;
		newRail.nodes = line;
		newRail.BuildDistanceTable();
		splineRails_.push_back(newRail);
	}
	BuildRailConnections(splineRails_); // ★接続情報を計算

}

void GamePlayScene::Update(){

	// ========================================================
	// ▼ 0. 常に実行する処理（カメラの更新）
	// ========================================================
	if ( debugCamera_ ) { debugCamera_->Update(camera_.get()); }
	camera_->Update();

	Input* input = Input::GetInstance();

	// ========================================================
	// ▼ 1. モードの切り替わりを監視する（ここでリロード！）
	// ========================================================
	EngineMode currentMode = EditorManager::GetInstance()->GetMode();

	// もし「エディット(停止)」から「プレイ(再生)」に切り替わった瞬間なら、リセット処理を行う
	if ( prevMode_ == EngineMode::Edit && currentMode == EngineMode::Play ) {

		// ★追加：最新のマップデータをJSONから読み込み直す（保存したてのデータを反映）
		LevelData mapData = LevelManager::GetInstance()->Load("resources/map/map01.json");
		splineRails_.clear();
		for ( const auto& line : mapData.railLines ) {
			SplineRail newRail;
			newRail.nodes = line;
			newRail.BuildDistanceTable();
			splineRails_.push_back(newRail);
		}
		BuildRailConnections(splineRails_); // ★接続情報を計算

		// ② プレイヤーをスタート地点に戻す
		if ( player_ ) { player_->Initialize(); }

		// ③ エフェクトなども綺麗に消す
		hitEffects_.clear();
	}
	prevMode_ = currentMode; // 現在のモードを記憶して次に備える


	// ========================================================
	// ▼ 2. プレイモード中（時間が動いている時）だけ実行する処理
	// ========================================================
	if ( currentMode == EngineMode::Play ) {

		// プレイヤーのレール移動計算
		if ( player_ ) {
			player_->Update(splineRails_);
		}

		// スペースキー入力（エフェクト発生とBGM再生）
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

		// アニメーションの進行（時間を進める）
		if ( skinnedObj_ && skinnedAnimTrack_.duration > 0.0f ) {
			skinnedAnimTime_ += 1.0f / 60.0f;
			if ( skinnedAnimTime_ > skinnedAnimTrack_.duration ) { skinnedAnimTime_ = 0.0f; }
		}

		// エフェクトの更新と死んだエフェクトの削除
		for ( auto& effect : hitEffects_ ) { effect->Update(); }
		hitEffects_.remove_if([](const std::unique_ptr<HitEffect>& e){ return e->IsDead(); });
	}

	// ★ 円柱オーラの更新処理
	if ( auraCylinderObj_ ) {
		auraCylinderScroll_ += 1.0f * ( 1.0f / 60.0f );
		if ( auraCylinderScroll_ > 1.0f ) { auraCylinderScroll_ -= 1.0f; }
		Matrix4x4 uvTransform = MakeTranslate({ 0.0f, auraCylinderScroll_, 0.0f });
		auraCylinderObj_->GetModel()->GetMaterial()->uvTransform = uvTransform;
		auraCylinderObj_->Update();
	}

	// ========================================================
	// ▼ 3. モードに関わらず常に実行する処理（描画のための更新）
	// ========================================================
	if ( input->Triggerkey(DIK_T) ) {
		SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
	}

	// 3Dオブジェクトの行列更新
	for ( auto& obj : object3ds_ ) { obj->Update(); }
	if ( testObj_ ){ testObj_->Update(); }

	if ( auraObj_ ) {
		auraUvScrollOffset_ += 1.0f * ( 1.0f / 60.0f );
		if ( auraUvScrollOffset_ > 1.0f ) { auraUvScrollOffset_ -= 1.0f; }
		Matrix4x4 uvTransform = MakeTranslate({ 0.0f, auraUvScrollOffset_, 0.0f });
		auraObj_->GetModel()->GetMaterial()->uvTransform = uvTransform;
		auraObj_->Update();
	}

	// スキニングアニメーションの行列更新
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

	// パーティクルの更新
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

	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);


	// 2. 【MRT終了】
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