#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class BossSummonEffect : public ICardEffect {
public:
    BossSummonEffect(int spawnCount) : spawnCount_(spawnCount) {}

    // 発射元ボスも受け取って保持する
    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

    void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, -20.0f, 0.0f };
    Vector3 scale_ = { 0.1f, 0.1f, 0.1f }; // 最初は小さく

    int spawnCount_ = 5;
    int timer_ = 60; // 魔法陣が表示される時間（1秒）
    bool isFinished_ = false;

    // この召喚を実行したボス本人
// 分裂ボス時に左右どちらの召喚かを失わないようにする
    Boss* casterBoss_ = nullptr;

};

