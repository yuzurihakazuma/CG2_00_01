#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class BossKickEffect : public ICardEffect {
public:
    // ボス専用の蹴り攻撃ダメージを受け取る
    BossKickEffect(int damage) : damage_(damage) {}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    std::unique_ptr<Obj3d> obj_ = nullptr; // ボス蹴り演出用オブジェクト
    Vector3 scale_ = { 1.8f, 1.8f, 1.8f }; // ボス用なので少し大きめにする

    int damage_ = 1;
    int timer_ = 0;
    bool isFinished_ = false;
    bool hasHit_ = false; // 1回の蹴りで多重ヒットしないようにする

    Vector3 pos_{};
    Vector3 startPos_{}; // 蹴り開始位置
    float casterYaw_ = 0.0f;

    Boss* casterBoss_ = nullptr; // 発動したボス本人を保持する
};