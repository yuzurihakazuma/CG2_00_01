#pragma once
#include <memory>
#include "engine/math/VectorMath.h"

class Player;
class Camera;
class Input;
class DebugCamera;
class MapManager;
class BossManager;

class PlayerManager {
public:
    PlayerManager() = default;
    ~PlayerManager() = default;

    // プレイヤー生成
    void Initialize(Camera* camera);

    // プレイヤー解放
    void Finalize();

    // プレイヤー更新
    void Update(
        Input* input,
        MapManager* mapManager,
        DebugCamera* debugCamera,
        BossManager* bossManager,
        bool isMagicCasting = false
    );

    // プレイヤー描画
    void Draw();

	// プレイヤー残像描画
    void DrawAfterimage();

    // プレイヤー再配置
    void RespawnInRoom(MapManager* mapManager, Camera* camera);

    // プレイヤー本体取得
    Player* GetPlayer() const { return player_.get(); }

    // 現在位置取得
    const Vector3& GetPosition() const { return playerPos_; }

    // 現在スケール取得
    const Vector3& GetScale() const { return playerScale_; }

    // 位置設定
    void SetPosition(const Vector3& pos);

    // スケール設定
    void SetScale(const Vector3& scale);

    // 死亡状態取得
    bool IsDead() const;
    bool IsDeathAnimationFinished() const;

    // 表示状態取得
    bool IsVisible() const;

    // 状態リセット
    void Reset();

    // 無限モード適用
    void ApplyInfiniteMode(bool isInfiniteMode);

    // ステータス取得
    int GetHP() const;
    int GetMaxHP() const;
    int GetCost() const;
    int GetMaxCost() const;
    int GetLevel() const;
    int GetExp() const;
    int GetNextLevelExp() const;

    // 状態取得
    bool IsDodging() const;
    bool IsActionLocked() const;
    bool IsHit() const;
    bool IsInvincible() const;

    // コスト関連
    bool CanUseCost(int cost) const;
    void UseCost(int cost);

    // 経験値加算
    void AddExp(int value);

    // 向き取得
    float GetRotationY() const;

private:
    std::unique_ptr<Player> player_ = nullptr;

    Vector3 playerPos_ = { 5.0f, 0.0f, 5.0f };
    Vector3 playerScale_ = { 1.0f, 1.0f, 1.0f };
};
