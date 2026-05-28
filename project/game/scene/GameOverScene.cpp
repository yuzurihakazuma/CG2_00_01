#include "GameOverScene.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Base/Input.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "Engine/3D/Model/ModelManager.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Camera/Camera.h"
#include "Engine/2D/Sprite.h"
#include "game/player/Player.h"
#include "externals/imgui/imgui.h"

#include "Engine/Graphics/PipelineManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "game/map/MapManager.h"
#include "Bloom.h"

#include "Engine/3D/Obj/Obj3d.h"
#include "Engine/Graphics/TextureManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace {
    constexpr std::array<const char*, 8> kBurnCardModelNames = {
    "CardSword",
    "cardFire",
    "CardShield",
    "cardPotion",
    "CardIce",
    "CardFang",
    "CardClaw",
    "CardCostBoost",
    };

    // ゲームオーバー画面用の乱数エンジン
    std::mt19937& GetBurnCardRandomEngine() {
        static std::mt19937 engine(std::random_device{}());
        return engine;
    }

    float BurnCardRandomRange(float minValue, float maxValue) {
        std::uniform_real_distribution<float> distribution(minValue, maxValue);
        return distribution(GetBurnCardRandomEngine());
    }

    int BurnCardRandomIndex(int maxExclusive) {
        std::uniform_int_distribution<int> distribution(0, maxExclusive - 1);
        return distribution(GetBurnCardRandomEngine());
    }

    bool HasControllerPromptInput(Input* input) {
        return input &&
            input->GetJoystickState() &&
            (input->PushJoystickButton(0xFFFF) ||
             input->PushLeftTrigger(0.25f) ||
             std::fabs(input->GetLeftStickX()) > 0.25f ||
             std::fabs(input->GetLeftStickY()) > 0.25f ||
             std::fabs(input->GetRightStickX()) > 0.25f ||
             std::fabs(input->GetRightStickY()) > 0.25f);
    }

    bool HasKeyboardPromptInput(Input* input) {
        if (!input) {
            return false;
        }

        const BYTE keys[] = {
            DIK_SPACE, DIK_RETURN, DIK_ESCAPE,
            DIK_W, DIK_A, DIK_S, DIK_D,
            DIK_UP, DIK_DOWN, DIK_LEFT, DIK_RIGHT
        };
        for (BYTE key : keys) {
            if (input->Pushkey(key)) {
                return true;
            }
        }

        return false;
    }
}

GameOverScene::GameOverScene() = default;

GameOverScene::~GameOverScene() = default;

void GameOverScene::Initialize(){
    // 強いポストエフェクトは切って見やすくする
    auto* pe = PostEffect::GetInstance();
    pe->SetEffectActive(PostEffectType::Vignetting, false);
    pe->SetEffectActive(PostEffectType::Grayscale, false);
    pe->SetEffectActive(PostEffectType::RadialBlur, false);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    WindowProc* windowProc = WindowProc::GetInstance();

    // 背景用のブロックモデルを読み込む
    ModelManager::GetInstance()->LoadModel("block", "resources/block", "block.obj");
    // 落下演出に使うカードモデルを読み込む
    ModelManager::GetInstance()->LoadModel("cardFire", "resources/card", "CardFire.obj");
    ModelManager::GetInstance()->LoadModel("cardPotion", "resources/card", "CardPotion.obj");
    ModelManager::GetInstance()->LoadModel("CardShield", "resources/card", "CardShield.obj");
    ModelManager::GetInstance()->LoadModel("CardIce", "resources/card", "CardIce.obj");
    ModelManager::GetInstance()->LoadModel("CardFang", "resources/card", "CardFang.obj");
    ModelManager::GetInstance()->LoadModel("CardClaw", "resources/card", "CardClaw.obj");
    ModelManager::GetInstance()->LoadModel("CardCostBoost", "resources/card", "CardCostBoost.obj");
    ModelManager::GetInstance()->LoadModel("CardSword", "resources/card", "swordCard.obj");

    // 背景用カメラを作る
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

    // 背景マップを全面ブロックで作る
    mapManager_ = std::make_unique<MapManager>();
    mapManager_->SetCamera(camera_.get());
    mapManager_->Initialize();
    mapManager_->FillAllTiles(1);
    gameOverBgPlayerPos_ = mapManager_->GetMapCenterPosition();

    burnCardNoiseTextureIndex_ =
        TextureManager::GetInstance()->LoadTextureAndCreateSRV("Resources/noise0.png", dxCommon->GetCommandList()).srvIndex;

    const float screenW = static_cast<float>(windowProc->GetClientWidth());
    const float screenH = static_cast<float>(windowProc->GetClientHeight());

    // GAME OVER のロゴと戻り案内を作る
    gameOverSprite_ = Sprite::Create("resources/UI/Game_Over.png", { screenW * 0.5f, screenH * 0.40f });
    spaceSprite_ = Sprite::Create("resources/UI/GAME_overUI.png", { screenW * 0.5f, screenH * 0.60f });
    controllerSpaceSprite_ = Sprite::Create("resources/UI/GAME_overUIC.png", { screenW * 0.5f, screenH * 0.60f });
    isControllerUiMode_ = Input::GetInstance()->GetJoystickState();

    if (spaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->Update();
    }
    if (controllerSpaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        controllerSpaceSprite_->SetSize({ logoWidth, logoHeight });
        controllerSpaceSprite_->Update();
    }

    // 上から落ちて燃え尽きるカード演出を初期化する
    InitializeFallingBurnCards(screenW, screenH);

    // ゲームオーバー画面用のプレイヤーを作る
    gameOverPlayer_ = std::make_unique<Player>();
    gameOverPlayer_->Initialize();
    gameOverPlayer_->SetCamera(camera_.get());
    gameOverPlayer_->SetPosition(gameOverPlayerBasePos_);
    gameOverPlayer_->SetScale({ 1.0f, 1.0f, 1.0f });
    gameOverPlayer_->SetRotation(gameOverPlayerRot_);
    gameOverPlayer_->SetInputEnable(false); // ゲームオーバー画面では操作させない
    gameOverPlayerPoseTimer_ = 0;

    // ロゴ落下アニメーション初期化
    logoDropTimer_ = 0;
    logoTargetY_   = screenH * 0.50f;
    logoCurrentY_  = -400.0f; // 画面外の上からスタート
    // プレイヤーへのスポットライト（MaskedGlow）を有効化
    PostEffect::GetInstance()->SetPlayerScreenUV(0.5f, 0.6f, 0.40f); // 初期UV（カメラ更新前の仮値）
    PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedGlow, true);
}

void GameOverScene::Update(){
    const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
    const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
    Input* input = Input::GetInstance();

    if (HasControllerPromptInput(input)) {
        isControllerUiMode_ = true;
    } else if (HasKeyboardPromptInput(input)) {
        isControllerUiMode_ = false;
    }

    // 背景カメラを固定で更新する
    if (camera_) {
        camera_->SetTranslation(gameOverBgCameraPos_);
        camera_->SetRotation(gameOverBgCameraRot_);
        camera_->SetFovY(1.0f);
        camera_->SetFarClip(200.0f);
        camera_->Update();
    }

    // 背景マップを更新する
    if (mapManager_) {
        mapManager_->Update(gameOverBgPlayerPos_);
    }

    // 画面座標だけで落下カードを更新する
    UpdateFallingBurnCards(
        static_cast<float>(WindowProc::GetInstance()->GetClientWidth()),
        static_cast<float>(WindowProc::GetInstance()->GetClientHeight())
    );

    // プレイヤーは少しだけ沈み込むように揺らす
    if (gameOverPlayer_) {
        gameOverPlayerPoseTimer_++;

        Vector3 pos = gameOverPlayerBasePos_;
        Vector3 rot = gameOverPlayerRot_;
        pos.y -= std::fabs(std::sinf(static_cast<float>(gameOverPlayerPoseTimer_) * 0.06f)) * 0.12f;

        gameOverPlayer_->SetPosition(pos);
        gameOverPlayer_->SetRotation(rot);
        gameOverPlayer_->Update();
    }

    // ---- ロゴ落下アニメーション ----
    logoDropTimer_++;
    {
        const float startY  = -400.0f;
        const float range   = logoTargetY_ - startY;

        if (logoDropTimer_ <= 40) {
            // 0〜40フレーム: 加速しながら落下（easeIn）
            float t = static_cast<float>(logoDropTimer_) / 40.0f;
            logoCurrentY_ = startY + range * (t * t);
        } else if (logoDropTimer_ <= 48) {
            // 40〜48フレーム: バウンス（上に跳ねる）
            float t = static_cast<float>(logoDropTimer_ - 40) / 8.0f;
            float bounce = std::sinf(t * 3.14159f) * 28.0f;
            logoCurrentY_ = logoTargetY_ - bounce;
        } else {
            // 48フレーム以降: 着地位置で固定
            logoCurrentY_ = logoTargetY_;
        }
    }

    // ロゴの位置と大きさを更新する（落下アニメーションを適用）
    if (gameOverSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        gameOverSprite_->SetPosition({ screenW * 0.5f, logoCurrentY_ });
        gameOverSprite_->SetSize({ logoWidth, logoHeight });
        gameOverSprite_->Update();
    }

    // ---- プレイヤーへのスポットライト (MaskedGlow) ----
    if (camera_) {
        // プレイヤーの胴体中央あたりをワールド座標で指定
        Vector3 wp = {
            gameOverPlayerBasePos_.x,
            gameOverPlayerBasePos_.y + 1.0f,
            gameOverPlayerBasePos_.z
        };
        const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
        float cx = wp.x * vp.m[0][0] + wp.y * vp.m[1][0] + wp.z * vp.m[2][0] + vp.m[3][0];
        float cy = wp.x * vp.m[0][1] + wp.y * vp.m[1][1] + wp.z * vp.m[2][1] + vp.m[3][1];
        float cw = wp.x * vp.m[0][3] + wp.y * vp.m[1][3] + wp.z * vp.m[2][3] + vp.m[3][3];
        if (std::abs(cw) > 0.0001f) {
            float uvX = cx / cw * 0.5f + 0.5f;
            float uvY = -cy / cw * 0.5f + 0.5f;
            PostEffect::GetInstance()->SetPlayerScreenUV(uvX, uvY, 0.40f);
            PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedGlow, true);
        }
    }

    if (spaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        spaceSprite_->SetPosition({ screenW * 0.5f, screenH * 0.50f });
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->Update();
    }
    if (controllerSpaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        controllerSpaceSprite_->SetPosition({ screenW * 0.5f, screenH * 0.50f });
        controllerSpaceSprite_->SetSize({ logoWidth, logoHeight });
        controllerSpaceSprite_->Update();
    }

    // 決定入力でタイトルへ戻る
    if (
        input->Triggerkey(DIK_SPACE) ||
        input->TriggerJoystickButton(XINPUT_GAMEPAD_A)
        ) {
        SceneManager::GetInstance()->ChangeScene("TITLE");
        return;
    }
}

void GameOverScene::Draw() {
    auto dxCommon = DirectXCommon::GetInstance();
    auto commandList = dxCommon->GetCommandList();

    // MRT へシーン描画を開始する
    PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

    // 3D 描画パスで背景を描画する
    Obj3dCommon::GetInstance()->PreDraw(commandList);
    PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

    if (mapManager_) {
        mapManager_->Draw(gameOverBgPlayerPos_);
    }

    // MapManager描画でインスタンシング用パイプラインに切り替わるので、
    // 通常のObj3dを描く前に3D用パイプラインを入れ直す
    Obj3dCommon::GetInstance()->PreDraw(commandList);
    PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);

    // 3D 描画パスで落下カードを描画する
    for (const FallingBurnCardParticle& particle : fallingBurnCards_) {
        if (particle.isActive && particle.obj) {
            particle.obj->Draw();
        }
    }

    // 3D 描画パスでプレイヤーを描画する
    if (gameOverPlayer_) {
        gameOverPlayer_->Draw();
    }

    // MRT へのシーン描画を終了する
    PostEffect::GetInstance()->PostDrawSceneMRT(commandList);

    // ポストエフェクトを適用する
    PostEffect::GetInstance()->Draw(commandList);

    // Bloom を適用する
    uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
    uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();
    Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
    uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

    // 最終画像をバックバッファへ描画する
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
    commandList->DrawInstanced(3, 1, 0, 0);

    // 2D スプライトを最後に描画する
    if (gameOverSprite_) {
        gameOverSprite_->Draw();
    }

    Sprite* promptSprite = isControllerUiMode_ ? controllerSpaceSprite_.get() : spaceSprite_.get();
    if (promptSprite) {
        promptSprite->Draw();
    }
}

void GameOverScene::Finalize(){
    // スポットライトを切る（他シーンに影響させない）
    PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedGlow, false);

    // このシーンで作ったものを解放する
    fallingBurnCards_.clear(); // 落下カード演出を解放する
    gameOverSprite_.reset();
    spaceSprite_.reset();
    controllerSpaceSprite_.reset();
    mapManager_.reset();
    camera_.reset();
    gameOverPlayer_.reset(); // ゲームオーバー画面用プレイヤーを解放する
}

void GameOverScene::DrawDebugUI() {
#ifdef USE_IMGUI
    ImGui::Begin("GameOver Debug");

    // カメラの位置を調整する
    ImGui::DragFloat3("Camera Pos", &gameOverBgCameraPos_.x, 0.05f);

    // カメラの回転を調整する
    ImGui::DragFloat3("Camera Rot", &gameOverBgCameraRot_.x, 0.01f);

    ImGui::Text("Camera Rot X = %.3f", gameOverBgCameraRot_.x);
    ImGui::Text("Camera Rot Y = %.3f", gameOverBgCameraRot_.y);
    ImGui::Text("Camera Rot Z = %.3f", gameOverBgCameraRot_.z);

    if (ImGui::Button("Reset Camera")) {
        gameOverBgCameraPos_ = { 49.0f, 16.0f, 49.1f }; // 初期カメラ位置へ戻す
        gameOverBgCameraRot_ = { 1.57f, -1.57f, 0.0f }; // 初期カメラ回転へ戻す
    }

    ImGui::Separator();

    // プレイヤーの位置を調整する
    ImGui::DragFloat3("Player Base Pos", &gameOverPlayerBasePos_.x, 0.05f);

    // プレイヤーの回転を調整する
    ImGui::DragFloat3("Player Rot", &gameOverPlayerRot_.x, 0.01f);

    ImGui::Text("Player Rot X = %.3f", gameOverPlayerRot_.x);
    ImGui::Text("Player Rot Y = %.3f", gameOverPlayerRot_.y);
    ImGui::Text("Player Rot Z = %.3f", gameOverPlayerRot_.z);

    if (ImGui::Button("Reset Player Rot")) {
        gameOverPlayerRot_ = { 0.02f, 1.65f, 7.84f }; // 初期の向きへ戻す
    }

    ImGui::End();
#endif
}

void GameOverScene::InitializeFallingBurnCards(float screenW, float screenH) {
    fallingBurnCards_.clear();
    fallingBurnCards_.resize(40);
    fallingBurnCardSpawnTimer_ = 0;

    
}

void GameOverScene::SpawnFallingBurnCard(float screenW, float screenH) {
    (void)screenW;
    (void)screenH;

    for (FallingBurnCardParticle& particle : fallingBurnCards_) {
        if (particle.isActive) {
            continue;
        }

        particle.obj = Obj3d::Create(kBurnCardModelNames[BurnCardRandomIndex(static_cast<int>(kBurnCardModelNames.size()))]);
        if (!particle.obj) {
            return;
        }

        particle.obj->SetCamera(camera_.get());

        // z の範囲を広げて横方向の降ってくる幅を広げる
        particle.position = {
    BurnCardRandomRange(36.0f, 42.0f),
    3.0f,
    BurnCardRandomRange(34.0f, 64.0f)
        };
        // タイトル画面と同じように X 方向へ流す
        particle.velocity = {
            0.04f,
            0.0f,
            0.0f
        };

        // ランダム回転でカードらしく見せる
        particle.rotation = {
            BurnCardRandomRange(0.0f, 6.28f),
            BurnCardRandomRange(0.0f, 6.28f),
            BurnCardRandomRange(0.0f, 6.28f)
        };

        particle.rotationSpeed = BurnCardRandomRange(-0.03f, 0.03f);

        // タイトル画面に近いサイズにする
        particle.scale = { 1.1f, 1.1f, 1.1f };
        particle.dissolveThreshold = 0.0f;
        particle.age = 0;
        // 生存時間をほぼ2倍にして長く残るようにする
        particle.lifetime = 280 + BurnCardRandomIndex(120);
        particle.isActive = true;

        particle.obj->SetTranslation(particle.position);
        particle.obj->SetRotation(particle.rotation);
        particle.obj->SetScale(particle.scale);
        particle.obj->SetNoiseTexture(burnCardNoiseTextureIndex_);
        particle.obj->SetDissolveColor({ 1.0f, 0.2f, 0.05f }); // 攻撃カード系と同じ燃える色
        particle.obj->SetDissolveThreshold(0.0f);
        particle.obj->Update();
        return;
    }
}
void GameOverScene::UpdateFallingBurnCards(float screenW, float screenH) {
    (void)screenW;
    (void)screenH;

    fallingBurnCardSpawnTimer_++;
    if (fallingBurnCardSpawnTimer_ >= fallingBurnCardSpawnInterval_) {
        fallingBurnCardSpawnTimer_ = 0;
        SpawnFallingBurnCard(screenW, screenH);
    }

    for (FallingBurnCardParticle& particle : fallingBurnCards_) {
        if (!particle.isActive || !particle.obj) {
            continue;
        }

        particle.age++;
        particle.position.x += particle.velocity.x;
        particle.position.z += particle.velocity.z;
        particle.rotation.z += particle.rotationSpeed;

        const float lifeT = static_cast<float>(particle.age) / static_cast<float>(particle.lifetime);
        const float clampedLifeT = std::clamp(lifeT, 0.0f, 1.0f);

        // カード使用時と同じ系統のディゾルブ進行
                // 後半まで形を保って、遅めに燃え尽きるようにする
        particle.dissolveThreshold = std::clamp((clampedLifeT - 0.70f) / 0.30f, 0.0f, 1.0f);

        particle.obj->SetTranslation(particle.position);
        particle.obj->SetRotation(particle.rotation);
        particle.obj->SetScale(particle.scale);
        particle.obj->SetDissolveColor({ 1.0f, 0.2f, 0.05f }); // 燃える縁色
        particle.obj->SetDissolveThreshold(particle.dissolveThreshold);
        particle.obj->Update();

        // 画面の下側まで流れたか、ディゾルブが終わったら消す
        if (
            particle.age >= particle.lifetime ||
            particle.position.x > 78.0f ||
            particle.dissolveThreshold >= 1.0f
            ) {
            particle.obj.reset();
            particle.isActive = false;
        }
    }
}
