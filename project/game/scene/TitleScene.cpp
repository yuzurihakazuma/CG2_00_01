#include "TitleScene.h"
// --- ゲーム固有のファイル ---
#include "GamePlayScene.h"

// --- エンジン側のファイル ---
#include "Engine/Math/Matrix4x4.h"
#include "Engine/Utils/ImGuiManager.h"
#include "Engine/Utils/Color.h"
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
#include "engine/utils/EditorManager.h"
#include "engine/sdf/SDFManager.h"
#include "engine/sdf/SDFSprite.h"
#include "engine/sdf/SDFText.h"

#include <cmath>

using namespace VectorMath;
using namespace MatrixMath;

TitleScene::TitleScene(){}

TitleScene::~TitleScene(){}


// 初期化
void TitleScene::Initialize(){
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
	// パーティクルグループ作成 (シングルトン)
	ParticleManager::GetInstance()->CreateParticleGroup("Circle", "resources/uvChecker.png");

	// テクスチャ読み込み
	textures_["uvChecker"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/uvChecker.png", commandList);
	textures_["monsterBall"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/monsterBall.png", commandList);
	textures_["fence"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/fence.png", commandList);
	textures_["circle"] = TextureManager::GetInstance()->LoadTextureAndCreateSRV("resources/circle.png", commandList);


	// カメラ生成
	camera_ = std::make_unique<Camera>(windowProc->GetClientWidth(), windowProc->GetClientHeight(), dxCommon);
	camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });

	// デバッグカメラ生成
	debugCamera_ = std::make_unique<DebugCamera>();
	debugCamera_->Initialize();

	// ファイル名を指定するだけで、読み込み・生成・配置
	// 引数: (ファイルパス, 座標)
	sprite_ = Sprite::Create(textures_["uvChecker"].srvIndex, spritePos_);

	

	// デプスステンシル作成 (TextureManagerシングルトン)
	depthStencilResource_ = TextureManager::GetInstance()->CreateDepthStencilTextureResource(
		windowProc->GetClientWidth(), windowProc->GetClientHeight()
	);

	
	levelEditor_ = std::make_unique<LevelEditor>();
	levelEditor_->SetCamera(camera_.get());
	levelEditor_->Initialize();

	// SDFタイトルロゴ（kazumimi）＋スタート案内。
	//   アトラスは SDFManager が resources/sdf/ から自動ロードするので、ここではアイテムを作るだけ
	logoSprite_ = std::make_unique<SDFSprite>();
	logoSprite_->Initialize();
	logoSprite_->SetSpriteName("kazumimi");
	logoSprite_->SetOutline(0.06f, { 0.2f, 0.12f, 0.05f, 1.0f });
	logoSprite_->SetGlow(0.2f, { 1.0f, 0.9f, 0.3f, 1.0f });

	startText_ = std::make_unique<SDFText>();
	startText_->Initialize();
	startText_->SetText("T : ゲームスタート");
	startText_->SetFontSize(40.0f);
	startText_->SetOutlineWidth(0.2f);
	startText_->SetOutlineColor({ 0.05f, 0.1f, 0.05f, 1.0f });
}

void TitleScene::Update(){
	// デバッグカメラ更新
	if ( debugCamera_ ) {
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
		SceneManager::GetInstance()->ChangeScene(std::make_unique<GamePlayScene>());
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
	// スプライト更新
	sprite_->Update();



	if ( sprite_ ) {
		sprite_->SetPosition(spritePos_);
		sprite_->Update();
	}


	levelEditor_->Update();

	// SDFタイトルロゴの演出：中央配置＋ふわっと脈動するスケールとグロー。
	//   SDFなのでどれだけ拡大してもフチが滲まない（ビットマップとの一番の違い）
	titleAnimTime_ += 1.0f / 60.0f;
	float W = ( float ) WindowProc::GetInstance()->GetClientWidth();
	float H = ( float ) WindowProc::GetInstance()->GetClientHeight();
	if ( logoSprite_ ) {
		float scale = 0.85f + 0.03f * std::sin(titleAnimTime_ * 2.0f);
		const float logoSize = 528.0f * scale; // kazumimi は 528x528
		logoSprite_->SetScale(scale);
		logoSprite_->SetPosition(( W - logoSize ) * 0.5f, H * 0.06f);
		float glowPulse = 0.14f + 0.10f * ( 0.5f + 0.5f * std::sin(titleAnimTime_ * 3.0f) );
		logoSprite_->SetGlow(glowPulse, { 1.0f, 0.9f, 0.3f, 1.0f });
	}
	if ( startText_ ) {
		// ゆっくり点滅（フェード）してスタート待ちを示す
		float a = 0.55f + 0.45f * std::sin(titleAnimTime_ * 2.5f);
		startText_->SetColor({ 1.0f, 1.0f, 1.0f, a });
		startText_->SetPosition(W * 0.5f - 170.0f, H * 0.82f);
	}
}

void TitleScene::DrawDebugUI(){
#ifdef USE_IMGUI
	// 3Dオブジェクト、カメラ、パーティクルのUI
	Obj3dCommon::GetInstance()->DrawDebugUI();
	if ( camera_ ) { camera_->DrawDebugUI(); }
	if ( debugCamera_ ) { debugCamera_->DrawDebugUI(); }
	ParticleManager::GetInstance()->DrawDebugUI();

	
	levelEditor_->DrawDebugUI();
	
	// スプライト調整用UI
	ImGui::SetNextWindowSize(ImVec2(500, 100));
	ImGui::Begin("Sprite Setup");
	ImGui::DragFloat2("Position", &spritePos_.x, 0.1f, -2000.0f, 2000.0f, "% 06.1f");
	ImGui::End();
#endif

}


void TitleScene::Draw(){


	auto dxCommon = DirectXCommon::GetInstance();
	auto commandList = DirectXCommon::GetInstance()->GetCommandList();

	// 画用紙への切り替え
	PostEffect::GetInstance()->PreDrawScene(commandList);


	// 3D描画の前準備
	Obj3dCommon::GetInstance()->PreDraw(commandList);



	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);


	// 3Dオブジェクト描画
	for ( auto& obj : object3ds_ ) {
		obj->Draw();
	}

	levelEditor_->Draw();


	// パーティクル描画 (パイプライン切り替え)
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Particle);
	ParticleManager::GetInstance()->Draw(commandList);

	// スプライト描画の前準備
	SpriteCommon::GetInstance()->PreDraw(commandList);

	if ( sprite_ ) {
		sprite_->Draw();
	}

	// SDFタイトルロゴ＋スタート案内（シーンRTに直接描く＝Game View にもそのまま映る）
	if ( logoSprite_ ) { SDFManager::GetInstance()->DrawSpriteItem(commandList, *logoSprite_, "kazumimi"); }
	if ( startText_ )  { SDFManager::GetInstance()->DrawTextItem(commandList, *startText_, "jpdot"); }

	PostEffect::GetInstance()->PostDrawScene(commandList);
	PostEffect::GetInstance()->Draw(commandList);

	EditorManager::GetInstance()->SetGameViewSrvIndex(PostEffect::GetInstance()->GetSrvIndex());

}

void TitleScene::Reload(){}

void TitleScene::Finalize(){
	object3ds_.clear();

	textures_.clear();
	depthStencilResource_.Reset();
}