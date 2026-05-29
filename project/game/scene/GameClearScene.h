#pragma once

#include "Engine/Scene/IScene.h"
#include "Engine/Math/Struct.h"

#include <memory>
#include <vector>

// Forward declarations used by the clear scene.
class Camera;
class MapManager;
class Sprite;
class Player;

class GameClearScene : public IScene {
public:
    GameClearScene();
    ~GameClearScene();

    void Initialize() override;
    void Update() override;
    void Draw() override;
    void Finalize() override;
    void DrawDebugUI() override;

private:
    struct ClearCardParticle {
        std::unique_ptr<Sprite> sprite = nullptr;
        Vector2 position = { 0.0f, 0.0f };
        Vector2 velocity = { 0.0f, 0.0f };
        Vector2 gravity = { 0.0f, 0.0f };
        float rotation = 0.0f;
        float rotationSpeed = 0.0f;
        float scale = 1.0f;
        float alpha = 1.0f;
        int age = 0;
        int lifetime = 0;
        bool isActive = false;
    };

private:
    void InitializeClearCardParticles(float screenW, float screenH);
    void SpawnClearCardBurst(float screenW, float screenH);
    void SpawnClearCardParticle(float screenW, float screenH, const Vector2& burstOrigin);
    void UpdateClearCardParticles(float screenW, float screenH);

private:
    // Camera used for the 3D clear background.
    std::unique_ptr<Camera> camera_ = nullptr;

    // Map used to draw the block background behind the UI.
    std::unique_ptr<MapManager> mapManager_ = nullptr;

    // Foreground UI sprites.
    std::unique_ptr<Sprite> gameClearSprite_ = nullptr;
    std::unique_ptr<Sprite> spaceSprite_ = nullptr;
    std::unique_ptr<Sprite> controllerSpaceSprite_ = nullptr;
    bool isControllerUiMode_ = false;
    bool hasConfirmedReturn_ = false;

    // Animated card particles for the clear effect.
    std::vector<ClearCardParticle> clearCardParticles_;

    // Shared timer for clear-scene animation.
    int clearAnimationTimer_ = 0;

    // Spawn interval for rising cards.
    int clearCardSpawnInterval_ = 60;

    // Background camera settings.
    Vector3 gameClearBgCameraPos_ = { 49.0f, 16.0f, 49.1f };
    Vector3 gameClearBgCameraRot_ = { 1.57f, -1.57f, 0.0f };

    // Center position passed to the map manager.
    Vector3 gameClearBgPlayerPos_ = { 0.0f, 0.0f, 0.0f };

    // クリア画面に表示するプレイヤー
    std::unique_ptr<Player> clearPlayer_ = nullptr;

    // プレイヤーの表示基準位置
    Vector3 clearPlayerBasePos_ = { 49.0f, 4.95f, 48.25f };

    // プレイヤーの表示用回転
    Vector3 clearPlayerRot_ = { 0.02f, 1.65f, 7.84f };

    // 喜びアニメーション管理タイマー
    int clearPlayerPoseTimer_ = 0;
};
