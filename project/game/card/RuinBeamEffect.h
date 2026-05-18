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

private:
    bool IsPlayerInsideBeam(const Vector3& playerPos) const;

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 rot_ = { 0.0f, 0.0f, 0.0f };
    Vector3 baseScale_ = { 2.4f, 1.2f, 16.0f };

    int damage_ = 6;
    int lifeTimer_ = 72;
    int hitInterval_ = 12;
    int hitTimer_ = 0;
    bool isFinished_ = false;

    // このビームを発動したボス本人
// 分裂ボス時に左右どちらのビームかを失わないように保持する
    Boss* casterBoss_ = nullptr;

};
