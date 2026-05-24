#include "GameOverScene.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Base/Input.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "Engine/3D/Model/ModelManager.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Camera/Camera.h"
#include "Engine/2D/Sprite.h"

#include "Engine/Graphics/PipelineManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "game/map/MapManager.h"
#include "Bloom.h"

GameOverScene::GameOverScene() = default;

GameOverScene::~GameOverScene() = default;

void GameOverScene::Initialize(){
    // ゲームシーンで付いたままのポストエフェクトをリセット
    auto* pe = PostEffect::GetInstance();
    pe->SetEffectActive(PostEffectType::Vignetting, false);
    pe->SetEffectActive(PostEffectType::Grayscale, false);
    pe->SetEffectActive(PostEffectType::RadialBlur, false);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    WindowProc* windowProc = WindowProc::GetInstance();

    // タイトルと同じブロックモデルを背景用に読み込む
    ModelManager::GetInstance()->LoadModel("block", "resources/block", "block.obj");

    // ブロック背景を上から映すカメラを作成する
    camera_ = std::make_unique<Camera>(
        windowProc->GetClientWidth(),
        windowProc->GetClientHeight(),
        dxCommon
    );
    camera_->SetTranslation(gameOverBgCameraPos_);
    camera_->SetRotation(gameOverBgCameraRot_);
    camera_->SetFovY(1.0f);
    camera_->SetFarClip(200.0f);
    camera_->Update();

    // マップを全てブロックで埋めて背景にする
    mapManager_ = std::make_unique<MapManager>();
    mapManager_->SetCamera(camera_.get());
    mapManager_->Initialize();
    mapManager_->FillAllTiles(1);
    gameOverBgPlayerPos_ = mapManager_->GetMapCenterPosition();

    const float screenW = static_cast< float >( windowProc->GetClientWidth() );
    const float screenH = static_cast< float >( windowProc->GetClientHeight() );

    // GAME OVER画像を画面中央より少し上に表示する
    gameOverSprite_ = Sprite::Create("resources/UI/Game_Over.png", { screenW * 0.5f, screenH * 0.40f });

	spaceSprite_ = Sprite::Create("resources/UI/GAME_overUI.png", { screenW * 0.5f, screenH * 0.60f });

    if ( spaceSprite_ ) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * ( 1080.0f / 1920.0f );
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->Update();
    }
	// 画面サイズに合わせてGAME OVER画像の大きさを更新する
    if ( spaceSprite_ ) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * ( 1080.0f / 1920.0f );
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->Update();
    }

}

void GameOverScene::Update(){
    // 背景用カメラを毎フレーム更新する
    if ( camera_ ) {
        camera_->SetTranslation(gameOverBgCameraPos_);
        camera_->SetRotation(gameOverBgCameraRot_);
        camera_->SetFovY(1.0f);
        camera_->SetFarClip(200.0f);
        camera_->Update();
    }

    // ブロック背景を更新する
    if ( mapManager_ ) {
        mapManager_->Update(gameOverBgPlayerPos_);
    }

    // 画面サイズに合わせてGAME OVER画像の位置と大きさを更新する
    if ( gameOverSprite_ ) {
        const float screenW = static_cast< float >( WindowProc::GetInstance()->GetClientWidth() );
        const float screenH = static_cast< float >( WindowProc::GetInstance()->GetClientHeight() );
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * ( 1080.0f / 1920.0f );

        gameOverSprite_->SetPosition({ screenW * 0.5f, screenH * 0.50f });
        gameOverSprite_->SetSize({ logoWidth, logoHeight });
        gameOverSprite_->Update();
    }

    if ( spaceSprite_ ) {
        const float screenW = static_cast< float >( WindowProc::GetInstance()->GetClientWidth() );
        const float screenH = static_cast< float >( WindowProc::GetInstance()->GetClientHeight() );
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * ( 1080.0f / 1920.0f );

        spaceSprite_->SetPosition({ screenW * 0.5f, screenH * 0.50f });
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->Update();
    }

    // スペースキーでタイトルへ戻る
    if ( Input::GetInstance()->Triggerkey(DIK_SPACE) ) {
        SceneManager::GetInstance()->ChangeScene("TITLE");
        return;
    }
}

void GameOverScene::Draw(){
    auto dxCommon = DirectXCommon::GetInstance();
    auto commandList = dxCommon->GetCommandList();

    // 1. MRT描画を開始する
    PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

    // ブロック背景をMRTへ描画する
    Obj3dCommon::GetInstance()->PreDraw(commandList);
    PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);
    if ( mapManager_ ) {
        mapManager_->Draw(gameOverBgPlayerPos_);
    }

    // 2. MRT描画を終了する
    PostEffect::GetInstance()->PostDrawSceneMRT(commandList);

    // 3. ポストエフェクトを適用する
    PostEffect::GetInstance()->Draw(commandList);

    // 4. Bloomを適用する
    uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
    uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();
    Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
    uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

    // 5. バックバッファへ最終結果を描画する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
    commandList->DrawInstanced(3, 1, 0, 0);

    // 6. 最終画面の上にGAME OVER画像を描画する
    if ( gameOverSprite_ ) {
        gameOverSprite_->Draw();
    }
	// 7. スペースキーでタイトルに戻ることを促すUIのスプライトを描画する
    if ( spaceSprite_ ) {
        spaceSprite_->Draw();
    }


}

void GameOverScene::Finalize(){
    // GameOverSceneで作ったものだけ解放する
    gameOverSprite_.reset();
	spaceSprite_.reset();
    mapManager_.reset();
    camera_.reset();
}