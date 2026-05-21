#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>
#include <vector>
#include <algorithm>

class BossSpearEffect : public ICardEffect {
public:
    BossSpearEffect(int damage) : damage_(damage) {}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    void CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj);

    struct AfterimageData {
        std::unique_ptr<Obj3d> object = nullptr;
        int lifeTimer = 0;
        bool isActive = false;
    };

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 2.0f, 2.0f, 2.0f }; // ボス用に少し大きめ

    int damage_ = 1;
    int timer_ = 0;
    bool isFinished_ = false;

    float casterYaw_ = 0.0f;
    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

    Boss* casterBoss_ = nullptr; // 発動したボス本人を保持
    std::vector<void*> hitTargets_;
    std::vector<AfterimageData> afterimages_;

    static const int maxAfterimageCount_ = 3;   // 残像数は既存槍と同じ
    static const int defaultAfterimageLife_ = 5; // 残像寿命も既存槍と同じ
};