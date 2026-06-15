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

	
	ModelManager::GetInstance()->CreatePlaneModel("plane");


	// パーティクルグループ作成 (シングルトン)
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み（短縮版 Load：commandList を渡さなくてよい）
	TextureManager* tex = TextureManager::GetInstance();
	textures_["uvChecker"] = tex->Load("resources/uvChecker.png");
	textures_["monsterBall"] = tex->Load("resources/monsterBall.png");
	textures_["fence"] = tex->Load("resources/fence.png");
	textures_["circle"] = tex->Load("resources/circle.png");
	textures_["circle2"] = tex->Load("resources/circle2.png");
	textures_["noise0"] = tex->Load("Resources/noise0.png");
	textures_["noise1"] = tex->Load("Resources/noise1.png");
	textures_["gradationLine"] = tex->Load("Resources/gradationLine.png");

	textures_["skybox"] = tex->LoadCube("resources/StandardCubeMap.dds");

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
	camera_ = Camera::Create(); // ウィンドウサイズ等は内部で自動取得
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });

	// このカメラを既定（アクティブ）カメラに設定 → 以降の Obj3d::Create は自動でこのカメラを使う
	Obj3dCommon::GetInstance()->SetDefaultCamera(camera_.get());

	
	// デバッグカメラ生成
	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();
	// メニューバー「表示」からON/OFFできるよう登録
	EditorManager::GetInstance()->SetDebugCamera(debugCamera_.get());

	// プレイヤーオブジェクト生成
	testObj_ = Obj3d::Create("animatedCube");
	testObj_->SetEnvironmentMap(textures_["skybox"].srvIndex); // これだけ！

	if ( testObj_ ){

		testObj_->SetPipelineType(PipelineType::Object3D_CullNone);

		testObj_->SetTranslation({ 0.0f, 0.0f, 5.0f });

		// ノイズ画像と初期の閾値(0.0)をセット
		testObj_->SetNoiseTexture(textures_["noise0"].srvIndex);
		testObj_->SetDissolveThreshold(0.0f);

		//testObj_->PlayAnimation(&testAnimation_);

		Bloom::GetInstance()->SetTargetEmissivePower(&testObj_->GetModel()->GetMaterial()->emissive);

		// エディタのギズモ／インスペクタの操作対象として登録
		EditorManager::GetInstance()->SetGizmoTarget(testObj_.get());
	}

	// ★1. Ringモデルの生成
	ModelManager::GetInstance()->CreateRingModel("auraRing", 32, 1.0f, 0.2f);

	// ★2. 画像のリングを単体として出す
	auraObj_ = Obj3d::Create("auraRing");
	if ( auraObj_ ) {

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
	skinnedObj_->SetEnvironmentMap(textures_["skybox"].srvIndex); // スキニングも
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

	auto initialEffect = std::make_unique<HitEffect>();
	initialEffect->Initialize(
		{ 0.0f, 0.0f, 5.0f },           // 目の前に発生
		camera_.get(),
		textures_["circle2"].srvIndex,  // テクスチャ
		textures_["skybox"].srvIndex
	);
	hitEffects_.push_back(std::move(initialEffect));
	
}

void GamePlayScene::Update(){
	
	// Blenderインポータからの「カメラに適用」要求を反映
	if ( BlenderImporter* importer = EditorManager::GetInstance()->GetBlenderImporter() ) {
		Vector3 blCamPos, blCamRot;
		if ( importer->ConsumeCameraRequest(blCamPos, blCamRot) ) {
			camera_->SetTranslation(blCamPos);
			camera_->SetRotation(blCamRot);
		}
	}

	// デバッグカメラ更新
	if (debugCamera_) {
		debugCamera_->Update(camera_.get());
	}
	// カメラ更新
	camera_->Update();

	Input* input = Input::GetInstance();

	// 1. スペースキーでエフェクト発生＋BGM再生（1か所に統合）
	if (input->Triggerkey(DIK_SPACE)) {
		// 新しいエフェクトを生成
		auto newEffect = std::make_unique<HitEffect>();

		// 発生場所（例：目の前）、カメラ、テクスチャ、環境マップを渡して初期化
		newEffect->Initialize(
			{ 5.0f, 0.0f, 5.0f },            // 発生位置
			camera_.get(),                 // カメラ
			textures_["gradationLine"].srvIndex,  // 鋭い光用の丸いテクスチャ
			textures_["skybox"].srvIndex   // 反射用の環境マップ
		);

		// リストに追加して、あとは任せる！
		hitEffects_.push_back(std::move(newEffect));

		// BGM再生 (シングルトン)
		AudioManager::GetInstance()->PlayWave(bgmFile_);
	}

	// 2. リストに載っている全エフェクトを更新
	for (auto& effect : hitEffects_) {
		effect->Update();
	}

	// 3. 寿命が尽きて「死んだ」エフェクトをリストから自動削除
	hitEffects_.remove_if([](const std::unique_ptr<HitEffect>& e) {
		return e->IsDead();
		});

	// ★ 新しい円柱オーラの更新処理
	if ( auraCylinderObj_ ) {
		// スクロール速度の計算（1秒間で1周するペース。デルタタイム基準）
		auraCylinderScroll_ += Time::GetInstance()->GetDeltaTime();
		if ( auraCylinderScroll_ > 1.0f ) {
			auraCylinderScroll_ -= 1.0f;
		}

		// V方向（縦方向）にUVをずらす行列を作成
		Matrix4x4 uvTransform = MakeTranslate({ 0.0f, auraCylinderScroll_, 0.0f });

		// マテリアルに行列を適用
		auraCylinderObj_->GetModel()->GetMaterial()->uvTransform = uvTransform;

		// オブジェクトの行列更新
		auraCylinderObj_->Update();
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


	if ( auraObj_ ) {
		// 1. スクロール値を毎フレーム少しずつ増やす（デルタタイム基準）
		float scrollSpeed = Time::GetInstance()->GetDeltaTime(); // 1秒間に1.0スクロールする速さ
		auraUvScrollOffset_ += scrollSpeed;

		// 1.0を超えたら0に戻す（オーバーフロー防止）
		if ( auraUvScrollOffset_ > 1.0f ) {
			auraUvScrollOffset_ -= 1.0f;
		}

		// 2. UVをV方向(Y軸)に移動させる行列を作成
		Matrix4x4 uvTransform = MakeTranslate({ 0.0f, auraUvScrollOffset_, 0.0f });

		// （応用：スライドのようにU方向にScaleをかけて解像度を細かく見せたい場合は以下のように合成します）
		// Matrix4x4 uvScale = MakeScaleMatrix({ 3.0f, 1.0f, 1.0f }); 
		// uvTransform = Multiply(uvScale, uvTransform);

		//auraObj_->SetRotation({ 1.5708f, 0.0f, 0.0f });

		// 3. マテリアルにUVTransform行列をセット
		auraObj_->GetModel()->GetMaterial()->uvTransform = uvTransform;

		// 4. オブジェクトの更新
		auraObj_->Update();
	}

	// テストオブジェクト更新
	if ( testObj_ ){
		testObj_->Update();
	}

	if (skinnedObj_) {
		// 再生するだけ（編集UIなし）
		if (skinnedAnimTrack_.duration > 0.0f) {
			skinnedAnimTime_ += Time::GetInstance()->GetDeltaTime();
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

	if ( input->Triggerkey(DIK_G) ){
		// GPUパーティクル更新（デルタタイム基準）
		GPUParticleManager::GetInstance()->Update(Time::GetInstance()->GetDeltaTime(), camera_.get());
	}

	emitter_.Update(Time::GetInstance()->GetDeltaTime());
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

	// 1. 先に「不透明」なものを全部描き切る！！！
	if ( testObj_ ){ testObj_->Draw(); }
	if ( skinnedObj_ ) { skinnedObj_->Draw(); }


	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
		// ⭕️ 2. ここで背景（スカイボックス）を描く！！！
		// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	if ( skybox_ ) {
		skybox_->Draw(commandList, camera_.get());
	}

	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	// ⭕️ 3. 最後に「透明・加算合成」のものを描く！！！（順番超大事）
	// ＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝＝
	if ( auraCylinderObj_ ) {
		auraCylinderObj_->Draw();
	}

	if ( auraObj_ ) {
		auraObj_->Draw();
	}

	for ( auto& effect : hitEffects_ ) {
		effect->Draw();
	}

	// --- パーティクル描画 ---
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// --- GPUパーティクル描画 ---
	GPUParticleManager::GetInstance()->Draw(commandList);


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

	// 4. エフェクト後の「色画像」と「マスク画像」の番号(SRV)をもらう
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

	// デバッグ描画（DebugDraw）の表示設定 — 共有「詳細設定」窓に合流
	if ( ImGui::Begin("インスペクター (詳細設定)") ) {
		if ( ImGui::CollapsingHeader("デバッグ描画 (DebugDraw)") ) {
			ImGui::Checkbox("グリッドを表示", &showDebugGrid_);
			ImGui::TextDisabled("Box/Sphere/Line はコードから積む。Game View にも表示されます");
		}
	}
	ImGui::End();

	ImGui::Begin("Environment Map Control");

	// 例：テスト用のキューブ (testObj_) だけ反射をいじれるようにする
	if ( testObj_ ) {
		// GetModel() でモデルを取得し、そのマテリアルの数値をスライダーで直接操作する
		ImGui::SliderFloat("Cube Reflect", &testObj_->GetModel()->GetMaterial()->environmentCoefficient, 0.0f, 1.0f);
	}	
	ImGui::End();


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

void GamePlayScene::ReloadMap(){}

GamePlayScene::GamePlayScene(){}

GamePlayScene::~GamePlayScene(){}
// 終了
void GamePlayScene::Finalize(){
	// エディタが保持している外部ポインタをリセット（ダングリングポインタ防止）
	EditorManager::GetInstance()->ResetSceneReferences();

	GPUParticleManager::GetInstance()->Finalize();

	textures_.clear();
	depthStencilResource_.Reset();
}