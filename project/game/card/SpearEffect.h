#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>
#include <vector>

class Enemy;

class SpearEffect : public ICardEffect {
public:
    SpearEffect(int damage) : damage_(damage) {}

    void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera) override;
    void Update(Player *player, EnemyManager *enemyManager, Boss *boss, const Vector3 &bossPos, const LevelData &level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.5f, 1.5f, 1.5f }; // 槍のモデルに合わせて調整

    int damage_ = 1;
    int timer_ = 0;
    bool isPlayerCaster_ = true;
    bool isFinished_ = false;

    float casterYaw_ = 0.0f;
    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

    // ==========================================
    // ★ 槍特有：貫通ヒットを管理するリスト
    // （一度ダメージを与えた敵を記録して、多段ヒットを防ぐ）
    // ==========================================
    std::vector<void *> hitTargets_;
};

