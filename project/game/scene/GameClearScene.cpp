#include "GameClearScene.h"

#include "Engine/Scene/SceneManager.h"
#include "Engine/Base/Input.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "Engine/3D/Model/ModelManager.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Camera/Camera.h"
#include "Engine/2D/Sprite.h"
#include "game/audio/GameBGM.h"
#include "game/audio/GameSE.h"
#include "game/player/Player.h"

#include "Engine/Graphics/PipelineManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "game/map/MapManager.h"
#include "Bloom.h"

#include <array>
#include <cmath>
#include <random>

#include "externals/imgui/imgui.h"

namespace {
    // Candidate card textures used by the clear animation.
    constexpr std::array<const char*, 8> kClearCardTexturePaths = {
        "resources/card/swordCard.png",
        "resources/card/CardFire.png",
        "resources/card/CardShield.png",
        "resources/card/CardPotion.png",
        "resources/card/CardIce.png",
        "resources/card/CardFang.png",
        "resources/card/CardClaw.png",
        "resources/card/CardCostBoost.png",
    };

    // Shared random engine for this scene.
    std::mt19937& GetClearCardRandomEngine() {
        static std::mt19937 engine(std::random_device{}());
        return engine;
    }

    float RandomRange(float minValue, float maxValue) {
        std::uniform_real_distribution<float> distribution(minValue, maxValue);
        return distribution(GetClearCardRandomEngine());
    }

    int RandomIndex(int maxExclusive) {
        std::uniform_int_distribution<int> distribution(0, maxExclusive - 1);
        return distribution(GetClearCardRandomEngine());
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

    // Scatter origins spread across the screen so cards do not all come from one point.
    Vector2 GetRandomScatterOrigin(float screenW, float screenH) {
        constexpr std::array<Vector2, 5> kNormalizedOrigins = {
            Vector2{ 0.22f, 0.74f },
            Vector2{ 0.38f, 0.66f },
            Vector2{ 0.50f, 0.70f },
            Vector2{ 0.64f, 0.64f },
            Vector2{ 0.80f, 0.72f },
        };

        const Vector2& normalizedOrigin = kNormalizedOrigins[RandomIndex(static_cast<int>(kNormalizedOrigins.size()))];
        return {
            screenW * normalizedOrigin.x + RandomRange(-70.0f, 70.0f),
            screenH * normalizedOrigin.y + RandomRange(-45.0f, 45.0f)
        };
    }
}

GameClearScene::GameClearScene() = default;

GameClearScene::~GameClearScene() = default;

void GameClearScene::Initialize() {
    hasConfirmedReturn_ = false;

    // クリアシーンに入ったらクリア用BGMへ切り替える
    GameBGM::GameClear(0.8f);

    // Reset strong post effects so the clear scene stays readable.
    auto* pe = PostEffect::GetInstance();
    pe->SetEffectActive(PostEffectType::Vignetting, false);
    pe->SetEffectActive(PostEffectType::Grayscale, false);
    pe->SetEffectActive(PostEffectType::RadialBlur, false);

    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    WindowProc* windowProc = WindowProc::GetInstance();

    // Load the block model used by the background map.
    ModelManager::GetInstance()->LoadModel("block", "resources/block", "block.obj");

    // Create the camera for the background.
    camera_ = std::make_unique<Camera>(
        windowProc->GetClientWidth(),
        windowProc->GetClientHeight(),
        dxCommon
    );
    camera_->SetTranslation(gameClearBgCameraPos_);
    camera_->SetRotation(gameClearBgCameraRot_);
    camera_->SetFovY(1.0f);
    camera_->SetFarClip(200.0f);
    camera_->Update();

    // Build a simple full-block map as the 3D backdrop.
    mapManager_ = std::make_unique<MapManager>();
    mapManager_->SetCamera(camera_.get());
    mapManager_->Initialize();
    mapManager_->FillAllTiles(1);
    // クリア画面の基準座標は床の高さを使う
    gameClearBgPlayerPos_ = mapManager_->GetMapCenterFloorPosition(0.0f);

    const float screenW = static_cast<float>(windowProc->GetClientWidth());
    const float screenH = static_cast<float>(windowProc->GetClientHeight());

    // Create the main logo and the return prompt.
    gameClearSprite_ = Sprite::Create("resources/UI/GAME_CLEAR.png", { screenW * 0.5f, screenH * 0.40f });
    spaceSprite_ = Sprite::Create("resources/UI/CrearUI.png", { screenW * 0.5f, screenH * 0.60f });
    controllerSpaceSprite_ = Sprite::Create("resources/UI/CrearUIC.png", { screenW * 0.5f, screenH * 0.60f });
    isControllerUiMode_ = Input::GetInstance()->GetJoystickState();

    if (gameClearSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        gameClearSprite_->SetSize({ logoWidth, logoHeight });
        gameClearSprite_->Update();
    }

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

    // Reset animation state and prepare initial card particles.
    clearAnimationTimer_ = 0;
    InitializeClearCardParticles(screenW, screenH);

    // クリア画面用プレイヤーを生成
    clearPlayer_ = std::make_unique<Player>();
    clearPlayer_->Initialize();
    clearPlayer_->SetCamera(camera_.get());

    clearPlayer_->SetPosition(clearPlayerBasePos_);
    clearPlayer_->SetScale({ 1.0f, 1.0f, 1.0f });


    clearPlayer_->SetRotation(clearPlayerRot_);
    clearPlayer_->SetInputEnable(false); // クリア画面ではプレイヤー操作を無効にする

    // 最初は待機ポーズ
    clearPlayer_->PlayIdlePose(1);
    clearPlayerPoseTimer_ = 0;

}

void GameClearScene::InitializeClearCardParticles(float screenW, float screenH) {
    clearCardParticles_.clear();
    clearCardParticles_.resize(24);

    // Start with one opening burst so the effect begins as a clear throw.
    SpawnClearCardBurst(screenW, screenH);
}

void GameClearScene::SpawnClearCardBurst(float screenW, float screenH) {
    // Choose one origin for this burst, then throw multiple cards from it.
    const Vector2 burstOrigin = GetRandomScatterOrigin(screenW, screenH);
    const int burstCount = 6 + RandomIndex(5); // 6 to 10 cards

    for (int i = 0; i < burstCount; ++i) {
        SpawnClearCardParticle(screenW, screenH, burstOrigin);
    }
}

void GameClearScene::SpawnClearCardParticle(float screenW, float screenH, const Vector2& burstOrigin) {
    for (ClearCardParticle& particle : clearCardParticles_) {
        if (particle.isActive) {
            continue;
        }

        particle.sprite = Sprite::Create(
            kClearCardTexturePaths[RandomIndex(static_cast<int>(kClearCardTexturePaths.size()))],
            { screenW * 0.5f, screenH * 0.78f }
        );
        if (!particle.sprite) {
            return;
        }

        // Keep one burst grouped by spawning all cards around the same origin.
        particle.position = {
            burstOrigin.x + RandomRange(-36.0f, 36.0f),
            burstOrigin.y + RandomRange(-28.0f, 28.0f)
        };

        // Throw the cards outward with a strong initial burst.
        particle.velocity = {
            RandomRange(-16.0f, 16.0f),
            RandomRange(-22.0f, -13.0f)
        };

        // Gravity pulls the cards back down so they travel in arcs instead of rising forever.
        particle.gravity = {
            0.0f,
            RandomRange(0.38f, 0.62f)
        };

        particle.rotation = RandomRange(-0.45f, 0.45f);
        particle.rotationSpeed = RandomRange(-0.13f, 0.13f);
        particle.scale = RandomRange(0.68f, 1.18f);
        particle.alpha = 1.0f;
        particle.age = 0;
        particle.lifetime = 90 + RandomIndex(45);
        particle.isActive = true;

        // Apply the initial sprite state.
        particle.sprite->SetAnchorPoint({ 0.5f, 0.5f });
        particle.sprite->SetSize({ screenW * 0.085f * particle.scale, screenH * 0.15f * particle.scale });
        particle.sprite->SetPosition(particle.position);
        particle.sprite->SetRotation(particle.rotation);
        particle.sprite->SetColor({ 1.0f, 1.0f, 1.0f, particle.alpha });
        particle.sprite->Update();
        return;
    }
}

void GameClearScene::UpdateClearCardParticles(float screenW, float screenH) {
    // Trigger a new grouped burst once per second.
    if (clearAnimationTimer_ % clearCardSpawnInterval_ == 0) {
        SpawnClearCardBurst(screenW, screenH);
    }

    for (ClearCardParticle& particle : clearCardParticles_) {
        if (!particle.isActive || !particle.sprite) {
            continue;
        }

        particle.age++;
        particle.velocity.x *= 0.992f;
        particle.velocity.y += particle.gravity.y;
        particle.position.x += particle.velocity.x;
        particle.position.y += particle.velocity.y;
        particle.rotation += particle.rotationSpeed;

        // Let rotation slow a touch so late-life cards feel less mechanical.
        particle.rotationSpeed *= 0.997f;

        const float lifeT = static_cast<float>(particle.age) / static_cast<float>(particle.lifetime);

        // Slightly enlarge early on so the burst reads better around the logo area.
        const float popScale = 1.0f + std::sinf(std::min(lifeT, 0.35f) / 0.35f * 3.1415926f) * 0.10f;
        const float drawScale = particle.scale * popScale;

        particle.sprite->SetPosition(particle.position);
        particle.sprite->SetRotation(particle.rotation);
        particle.sprite->SetSize({ screenW * 0.085f * drawScale, screenH * 0.15f * drawScale });
        particle.sprite->SetColor({ 1.0f, 1.0f, 1.0f, particle.alpha });
        particle.sprite->Update();

        // Recycle the slot after the card leaves the screen or expires.
        if (
            particle.age >= particle.lifetime ||
            particle.position.y < -260.0f ||
            particle.position.y > screenH + 260.0f ||
            particle.position.x < -320.0f ||
            particle.position.x > screenW + 320.0f
            ) {
            particle.sprite.reset();
            particle.isActive = false;
        }
    }
}

void GameClearScene::Update() {
    const float screenW = static_cast<float>(WindowProc::GetInstance()->GetClientWidth());
    const float screenH = static_cast<float>(WindowProc::GetInstance()->GetClientHeight());
    Input* input = Input::GetInstance();

    if (HasControllerPromptInput(input)) {
        isControllerUiMode_ = true;
    } else if (HasKeyboardPromptInput(input)) {
        isControllerUiMode_ = false;
    }

    // Advance the shared clear-scene timer.
    clearAnimationTimer_++;

    // Keep the background camera fixed.
    if (camera_) {
        camera_->SetTranslation(gameClearBgCameraPos_);
        camera_->SetRotation(gameClearBgCameraRot_);
        camera_->SetFovY(1.0f);
        camera_->SetFarClip(200.0f);
        camera_->Update();
    }

    // Update the background map.
    if (mapManager_) {
        mapManager_->Update(gameClearBgPlayerPos_);
    }

    // クリア画面用プレイヤーを喜んでいる感じで動かす
    if (clearPlayer_) {
        clearPlayerPoseTimer_++;

        Vector3 pos = clearPlayerBasePos_;
        Vector3 rot = clearPlayerRot_;

        // 跳ねる方向はワールドY固定にする
        pos.x -= std::fabs(std::sinf(static_cast<float>(clearPlayerPoseTimer_) * 0.10f)) * 0.55f;

        // 向きの揺れはY回転だけにする
        rot.y += std::sinf(static_cast<float>(clearPlayerPoseTimer_) * 0.05f) * 0.18f;

        clearPlayer_->SetPosition(pos);
        clearPlayer_->SetRotation(rot);

        // 一定間隔でポーズを入れて喜んでる感じを出す
        if (clearPlayerPoseTimer_ % 50 == 1) {
            clearPlayer_->PlayClearPose(12);
        } else if (clearPlayerPoseTimer_ % 50 == 26) {
            clearPlayer_->PlayIdlePose(10);
        }

        clearPlayer_->Update();
    }

    // Update the rising random-card effect.
    UpdateClearCardParticles(screenW, screenH);

    // Add a small vertical float to the main logo.
    if (gameClearSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        const float floatOffsetY = std::sinf(static_cast<float>(clearAnimationTimer_) * 0.05f) * 10.0f;

        gameClearSprite_->SetPosition({ screenW * 0.5f, screenH * 0.42f + floatOffsetY });
        gameClearSprite_->SetSize({ logoWidth, logoHeight });
        gameClearSprite_->Update();
    }

    // Pulse the return prompt to make the input hint clearer.
    if (spaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        const float blink = 0.55f + 0.45f * ((std::sinf(static_cast<float>(clearAnimationTimer_) * 0.08f) + 1.0f) * 0.5f);

        spaceSprite_->SetPosition({ screenW * 0.5f, screenH * 0.60f });
        spaceSprite_->SetSize({ logoWidth, logoHeight });
        spaceSprite_->SetColor({ 1.0f, 1.0f, 1.0f, blink });
        spaceSprite_->Update();
    }
    if (controllerSpaceSprite_) {
        const float logoWidth = screenW * 0.90f;
        const float logoHeight = logoWidth * (1080.0f / 1920.0f);
        const float blink = 0.55f + 0.45f * ((std::sinf(static_cast<float>(clearAnimationTimer_) * 0.08f) + 1.0f) * 0.5f);

        controllerSpaceSprite_->SetPosition({ screenW * 0.5f, screenH * 0.60f });
        controllerSpaceSprite_->SetSize({ logoWidth, logoHeight });
        controllerSpaceSprite_->SetColor({ 1.0f, 1.0f, 1.0f, blink });
        controllerSpaceSprite_->Update();
    }

    // Return to the title scene on confirm.
    if (!hasConfirmedReturn_ &&
        (input->Triggerkey(DIK_SPACE) ||
         input->TriggerJoystickButton(XINPUT_GAMEPAD_A))) {
        hasConfirmedReturn_ = true;
        GameSE::Confirm();
        SceneManager::GetInstance()->ChangeScene("TITLE");
        return;
    }
}

void GameClearScene::Draw() {
    auto dxCommon = DirectXCommon::GetInstance();
    auto commandList = dxCommon->GetCommandList();

    // 1. Begin MRT scene rendering.
    PostEffect::GetInstance()->PreDrawSceneMRT(commandList);

    // Draw the background map into the scene target.
    Obj3dCommon::GetInstance()->PreDraw(commandList);
    PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::Object3D_CullNone);
    if (mapManager_) {
        mapManager_->Draw(gameClearBgPlayerPos_);
    }

    // 2. Finish MRT scene rendering.
    PostEffect::GetInstance()->PostDrawSceneMRT(commandList);

    // 3. Apply the normal post-effect path.
    PostEffect::GetInstance()->Draw(commandList);

    // 4. Run bloom on the scene result.
    uint32_t colorSrv = PostEffect::GetInstance()->GetSrvIndex();
    uint32_t maskSrv = PostEffect::GetInstance()->GetMaskSrvIndex();
    Bloom::GetInstance()->Render(commandList, colorSrv, maskSrv);
    uint32_t finalSrv = Bloom::GetInstance()->GetResultSrvIndex();

    // 5. Present the final image to the back buffer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = dxCommon->GetBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dxCommon->GetDsvHandle();
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    PipelineManager::GetInstance()->SetPostEffectPipeline(commandList, PostEffectType::None);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(0, finalSrv);
    commandList->DrawInstanced(3, 1, 0, 0);

    // 6. Draw the rising cards behind the logo.
    for (const ClearCardParticle& particle : clearCardParticles_) {
        if (particle.isActive && particle.sprite) {
            particle.sprite->Draw();
        }
    }

    // 7. Draw the clear logo on top.
    if (gameClearSprite_) {
        gameClearSprite_->Draw();
    }

    // 8. Draw the return prompt.
    Sprite* promptSprite = isControllerUiMode_ ? controllerSpaceSprite_.get() : spaceSprite_.get();
    if (promptSprite) {
        promptSprite->Draw();
    }

    // 背景の上にプレイヤーを描画
    if (clearPlayer_) {
        clearPlayer_->Draw();
    }
}

void GameClearScene::Finalize() {
    // Release the animated card particles first.
    clearCardParticles_.clear();

    // Release scene resources.
    gameClearSprite_.reset();
    spaceSprite_.reset();
    controllerSpaceSprite_.reset();
    mapManager_.reset();
    camera_.reset();

    clearPlayer_.reset();
}

void GameClearScene::DrawDebugUI() {
#ifdef USE_IMGUI
    ImGui::Begin("GameClear Player Debug");

    ImGui::DragFloat3("Player Base Pos", &clearPlayerBasePos_.x, 0.05f);
    ImGui::DragFloat3("Player Rot", &clearPlayerRot_.x, 0.01f);
    ImGui::Text("Y Rot = %.3f", clearPlayerRot_.y);

    if (ImGui::Button("Face Camera")) {
        Vector3 toCamera = {
            gameClearBgCameraPos_.x - clearPlayerBasePos_.x,
            0.0f,
            gameClearBgCameraPos_.z - clearPlayerBasePos_.z
        };

        // モデルの正面が逆ならここで180度補正
        clearPlayerRot_.y = std::atan2f(toCamera.x, toCamera.z) + 3.1415926f;
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset Rot")) {
        clearPlayerRot_ = { 0.0f, 0.0f, 0.0f };
    }

    ImGui::End();
#endif
}
