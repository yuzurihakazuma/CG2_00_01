#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include <memory>

class DecoyEffect : public ICardEffect{
public:
    DecoyEffect(int duration) : duration_(duration){}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override{ return isFinished_; }

    // デコイの座標（敵が狙う用）
    Vector3 GetPosition() const{ return pos_; }

    // 敵やボスがデコイを攻撃したときに呼ぶ
    void NotifyHit();

public:
    std::unique_ptr<SkinnedObj3d> model_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };
    int duration_ = 300;
    int timer_ = 0;
    bool isFinished_ = false;

    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

private:
    bool isBeingHit_ = false;  // このフレームにヒット通知があったか
};

