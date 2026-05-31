#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"

#include <memory>

class RuinBeamEffect : public ICardEffect {
public:
    RuinBeamEffect(int damage) : damage_(damage) {}

    // 初期化
// 分裂ボス時に、どの個体がビームを出したかを保持できるようにする
    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;

    bool IsFinished() const override { return isFinished_; }
    const Vector3& GetDebugCenter() const { return pos_; }
    float GetDebugYaw() const { return rot_.y; }
    float GetDebugHalfWidth() const { return GetHitHalfWidth() + kPlayerHitRadius; }
    float GetDebugHalfLength() const { return GetHitHalfLength() + kPlayerHitRadius; }

private:
    bool IsPlayerInsideBeam(const Vector3& playerPos) const;
    float GetHitHalfWidth() const { return baseScale_.x * kHitHalfWidthScale; }
    float GetHitHalfLength() const { return baseScale_.z * kHitHalfLengthScale; }

private:
    static constexpr float kHitHalfWidthScale = 1.0f;
    static constexpr float kHitHalfLengthScale = 1.0f;
    static constexpr float kPlayerHitRadius = 0.6f;
    static constexpr int kLifeDuration = 95;
    static constexpr float kBeamTurnSpeed = 0.014f;

    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 originPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 1.6f, 1.2f, 14.0f };

    int damage_ = 6;
    int lifeTimer_ = kLifeDuration;
    int hitInterval_ = 12;
    int hitTimer_ = 0;
    bool isFinished_ = false;

    // このビームを発動したボス本人
// 分裂ボス時に左右どちらのビームかを失わないように保持する
    Boss* casterBoss_ = nullptr;

};
