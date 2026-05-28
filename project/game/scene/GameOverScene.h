#pragma once

#include "Engine/Scene/IScene.h"
#include "Engine/Math/Struct.h"

#include <memory>
#include <vector>

// ゲームオーバーシーン内で使うクラスを前方宣言する
class Camera;
class MapManager;
class Sprite;
class Player;
class Obj3d;

class GameOverScene : public IScene{
public:
    GameOverScene();
    ~GameOverScene();

    void Initialize() override;
    void Update() override;
    void Draw() override;
    void Finalize() override;
    void DrawDebugUI() override;

private:
    struct FallingBurnCardParticle {
        std::unique_ptr<Obj3d> obj = nullptr;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        Vector3 velocity = { 0.0f, 0.0f, 0.0f };
        Vector3 rotation = { 0.0f, 0.0f, 0.0f };
        float rotationSpeed = 0.0f;
        Vector3 scale = { 1.0f, 1.0f, 1.0f };
        float dissolveThreshold = 0.0f;
        int age = 0;
        int lifetime = 0;
        bool isActive = false;
    };

private:
    void InitializeFallingBurnCards(float screenW, float screenH);
    void SpawnFallingBurnCard(float screenW, float screenH);
    void UpdateFallingBurnCards(float screenW, float screenH);

private:
    // ブロック背景を映すためのカメラ
    std::unique_ptr<Camera> camera_ = nullptr;

    // タイトルと同じブロック背景を描画するためのマップ
    std::unique_ptr<MapManager> mapManager_ = nullptr;

    // GAME OVERのPNGを描画するスプライト
    std::unique_ptr<Sprite> gameOverSprite_ = nullptr;

	// スペースキーでタイトルに戻ることを促すUIのスプライト
	std::unique_ptr<Sprite> spaceSprite_ = nullptr;
	std::unique_ptr<Sprite> controllerSpaceSprite_ = nullptr;
    bool isControllerUiMode_ = false;

    // タイトル画面と同じ向きで背景を見るためのカメラ設定
    Vector3 gameOverBgCameraPos_ = { 49.0f, 16.0f, 49.1f };
    Vector3 gameOverBgCameraRot_ = { 1.57f, -1.57f, 0.0f };

    // MapManagerの描画基準にする中心位置
    Vector3 gameOverBgPlayerPos_ = { 0.0f, 0.0f, 0.0f };

    // ゲームオーバー画面に表示するプレイヤー
    std::unique_ptr<Player> gameOverPlayer_ = nullptr;

    // プレイヤーの表示基準位置
    Vector3 gameOverPlayerBasePos_ = { 49.0f, 4.95f, 48.25f };

    // プレイヤーの表示用回転
    Vector3 gameOverPlayerRot_ = { -1.210f, 1.960f, 7.78f };

    // ゲームオーバー画面用のポーズ制御タイマー
    int gameOverPlayerPoseTimer_ = 0;

    // 上から落ちて燃え尽きるカード演出
    std::vector<FallingBurnCardParticle> fallingBurnCards_;
    int fallingBurnCardSpawnTimer_ = 0;
    int fallingBurnCardSpawnInterval_ = 20;

    uint32_t burnCardNoiseTextureIndex_ = 0;

    // ロゴ落下アニメーション
    int   logoDropTimer_  = 0;
    float logoCurrentY_   = -400.0f; // 現在のY座標（画面外上からスタート）
    float logoTargetY_    = 0.0f;    // 着地Y座標
};
