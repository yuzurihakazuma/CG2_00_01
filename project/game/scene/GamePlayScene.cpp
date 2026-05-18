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
#include "engine/3d/SplineRail.h"
#include "engine/utils/Level/LevelManager.h"
#include "engine/utils/Level/LevelEditor.h"


using namespace VectorMath;
using namespace MatrixMath;
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
	ModelManager::GetInstance()->LoadModel("block", "resources/block","block.obj");

	// 球モデル作成 (シングルトン)
	ModelManager::GetInstance()->CreateSphereModel("sphere", 16);
	// パーティクルグループ作成 (シングルトン)
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み
	textures_["uvChecker"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/uvChecker.png", commandList);
	textures_["monsterBall"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/monsterBall.png", commandList);
	textures_["fence"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/fence.png", commandList);
	textures_["circle"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle.png", commandList);
	textures_["noise0"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise0.png", commandList) };
	textures_["noise1"] = { TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise1.png", commandList) };
	
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize("resources/StandardCubeMap.dds", commandList);


	// エディタマネージャーの生成
	EditorManager::GetInstance()->Initialize();



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
	if ( testObj_ ){

		testObj_->SetCamera(camera_.get());
		testObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });

		// ノイズ画像と初期の閾値(0.0)をセット
		testObj_->SetNoiseTexture(textures_["noise0"].srvIndex);
		testObj_->SetDissolveThreshold(0.0f);

		//testObj_->PlayAnimation(&testAnimation_);

		Bloom::GetInstance()->SetTargetEmissivePower(&testObj_->GetModel()->GetMaterial()->emissive);
	}

	

	skinnedObj_ = SkinnedObj3d::Create("human", "resources/human", "walk.gltf");
	skinnedObj_->SetCamera(camera_.get());
	skinnedObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });
	skinnedObj_->SetScale({ 1.0f, 1.0f, 1.0f });
	skinnedObj_->SetRotation({ 0.0f, 3.14159f, 0.0f });


	// デプスステンシル作成 (TextureManagerシングルトン)
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(), windowProc->GetClientHeight()
	);

	

	EditorManager::GetInstance()->SetCamera(camera_.get());

	sprite_ = Sprite::Create(textures_["uvChecker"].srvIndex, spritePos_);
	

	
	blockGroup_ = std::make_unique<InstancedGroup>();
	blockGroup_->Initialize("block", 10000); // 最大1万個まで対応！
	blockGroup_->SetNoiseTexture(textures_["uvChecker"].srvIndex);

	
	  // GPUパーティクル初期化 (テクスチャを指定する)
	GPUParticleManager::GetInstance()->Initialize(
		dxCommon, SrvManager::GetInstance(), "resources/uvChecker.png");


	// エミッターの初期設定
	GPUParticleEmitterData emitterData;
	emitterData.position = { 0.0f, 0.0f, 0.0f };
	emitterData.emitRate = 20.0f;
	emitter_.SetData(emitterData);

	// エディタにエミッターを渡す（F1で開くエディタで操作できるようになる）
	EditorManager::GetInstance()->SetParticleEmitter(&emitter_);




	// --- プレイヤーの初期化 ---
	player_ = std::make_unique<Player>();
	player_->Initialize();


	// 1. JSONからマップデータを読み込む
	LevelData mapData = LevelManager::GetInstance()->Load("resources/map/map01.json");

	// 2. 読み込んだレールの座標を、SplineRailにセットする
	splineRail_.nodes.clear();
	for ( const auto& pos : mapData.railNodes ) {
		// 読み込んだ座標(pos)と、とりあえずのFOV(60.0f)をセット
		splineRail_.nodes.push_back({ pos, 60.0f });
	}

}

void GamePlayScene::Update(){
	
	// デバッグカメラ更新
	if (debugCamera_) {
		debugCamera_->Update(camera_.get());
	}
	// カメラ更新
	camera_->Update();

	Input* input = Input::GetInstance();

	// BGM再生 (シングルトン)
	if ( input->Triggerkey(DIK_SPACE) ) {
		AudioManager::GetInstance()->PlayWave(bgmFile_);
	}
	// タイトルシーンへ移動
	if ( input->Triggerkey(DIK_T) ) {
		SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
	}
	// パーティクル発生 (シングルトン)
	if ( input->Triggerkey(DIK_P) ) {
		ParticleManager::GetInstance()->Emit("Circle", { 0.0f, 0.0f, 0.0f }, 10);
	}
	// パーティクル更新
	ParticleManager::GetInstance()->Update(camera_.get());


	// 全オブジェクト更新
	for ( auto& obj : object3ds_ ) {
		obj->Update();
		
	}
	if ( testObj_ ){
		testObj_->Update();
	}

	if ( player_ && testObj_ ) {
		// 1. プレイヤーの座標・回転をレールに沿って計算する
		player_->Update(splineRail_);

		// 2. 計算結果を3Dモデル(testObj_)に渡す
		testObj_->SetTranslation(player_->GetPosition());
		testObj_->SetRotation(player_->GetRotation());
	}




	if (skinnedObj_) {
		// 再生するだけ（編集UIなし）
		if (skinnedAnimTrack_.duration > 0.0f) {
			skinnedAnimTime_ += 1.0f / 60.0f;
			if (skinnedAnimTime_ > skinnedAnimTrack_.duration) {
				skinnedAnimTime_ = 0.0f;
			}
			Vector3 pos, rot, scale;
			skinnedAnimTrack_.UpdateTransformAtTime(skinnedAnimTime_, pos, rot, scale);
			skinnedObj_->SetTranslation(pos);
			skinnedObj_->SetRotation(rot);
			skinnedObj_->SetScale(scale);
		}
		skinnedObj_->Update();
	}

	if ( sprite_ ) {
		sprite_->Update();
	}

	PostEffect::GetInstance()->Update();

	
	for (auto& block : blocks_) {
		block->Update();
	}

	if ( input->Triggerkey(DIK_G) ){
		// GPUパーティクル更新
		GPUParticleManager::GetInstance()->Update(1.0f / 60.0f, camera_.get());

	}
	

	emitter_.Update(1.0f / 60.0f);

	// InstancedGroup に「最新のデータをお願い！」と渡すだけ
	if (blockGroup_) {
		blockGroup_->Update(blocks_);
	}

}

void GamePlayScene::Draw(){
	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = dxCommon->GetCommandList();

	// GPUパーティクルの描画準備（DispatchでComputeシェーダーを実行して、描画に必要なデータをGPU側で更新してもらう）
	GPUParticleManager::GetInstance()->Dispatch(commandList);


	// 1. 【MRT開始】キャンバスを2枚(色用とマスク用)セットする！
	PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

	// --- 3D描画の前準備 ---
	Obj3dCommon::GetInstance()->PreDraw(commandList);

	// --- カリングありの3D描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D);
	for ( auto& obj : object3ds_ ) { obj->Draw(); }
	
	
	EditorManager::GetInstance()->Draw();

	// --- カリングなしの3D描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);
	if ( testObj_ ){ testObj_->Draw(); }

	// --- インスタンシングの3D描画 ---
	if ( blockGroup_ ) { blockGroup_->Draw(camera_.get()); }

	// 
	if (skinnedObj_) { skinnedObj_->Draw(); }

	/*if ( skybox_ ) {
		skybox_->Draw(commandList, camera_.get());
	}*/


	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);


	// 2. 【MRT終了】3Dの描画が終わったので、2枚のキャンバスを読み込みモードに戻す
	PostEffect::GetInstance()->PostDrawSceneMRT(commandList);


	// 3. いつものPostEffect（色用のキャンバスだけに処理がかかります）
	 PostEffect::GetInstance()->Draw(commandList); // ※もしこの関数を作っていれば

	// 4. エフェクト後の「色画像」と「マスク画像」の番号(SRV)をもらう
	uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
	uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();

	// 5. Bloomに「色」と「マスク」を両方渡す！
	Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
	
	// 5-2. Bloomの最終結果のSRV番号をもらう
	uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();
	// エディタマネージャーに「これが最終的なゲーム画面のSRVだよ！」と教えてあげる
	EditorManager::GetInstance()->SetGameViewSrvIndex(finalSrv);


	// 6. メイン画面（バックバッファ）への直接描画！
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// 7. 最終的な結果を画面にドーン！と描く
	PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None); // シェーダーはエフェクトなしのやつを使う
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv); // Bloomの結果をSRVとしてセット
	commandList->DrawInstanced(3, 1, 0, 0); // 巨大な三角形を描いて全画面にテクスチャを貼る方式なので、頂点数は3でOK！



	// --- スプライト・UI描画 ---
	SpriteCommon::GetInstance()->PreDraw(commandList);
	if (sprite_) { sprite_->Draw(); }
	TextManager::GetInstance()->Draw();
	

}

void GamePlayScene::DrawDebugUI(){

#ifdef USE_IMGUI
	// 3Dオブジェクト、カメラ、パーティクルのUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();


	TextManager::GetInstance()->DrawDebugUI();

	ImGui::Begin("Block Dissolve Test");

	// スライダーで 0.0(通常) 〜 1.0(消滅) を操作
	if ( ImGui::SliderFloat("ブロックの消滅度", &dissolveThreshold_, 0.0f, 1.0f) ) {
		if ( testObj_ ) {
			// スライダーを動かすと、このブロックの閾値だけが書き換わる
			testObj_->SetDissolveThreshold(dissolveThreshold_);
		}
	}

	// 便利なリセットボタン
	if ( ImGui::Button("元に戻す") ) {
		dissolveThreshold_ = 0.0f;
		if ( testObj_ ){

			testObj_->SetDissolveThreshold(0.0f);
		}
			
	}
	ImGui::SameLine();
	if ( ImGui::Button("完全に消す") ) {
		dissolveThreshold_ = 1.0f;
		if ( testObj_ ){
			testObj_->SetDissolveThreshold(1.0f);
		}
	}
	ImGui::End();



#endif

}

GamePlayScene::GamePlayScene(){}

GamePlayScene::~GamePlayScene(){}
// 終了
void GamePlayScene::Finalize(){
	

	object3ds_.clear();
	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}