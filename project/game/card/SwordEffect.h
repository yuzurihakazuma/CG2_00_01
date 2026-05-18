#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>
#include <vector>
#include <algorithm>

class SwordEffect : public ICardEffect{
public:
    SwordEffect(int damage) : damage_(damage){}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override{ return isFinished_; }

private:
    void CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj);

    struct AfterimageData{
        std::unique_ptr<Obj3d> object = nullptr;
        int lifeTimer = 0;
        bool isActive = false;
    };

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.5f, 1.5f, 1.5f };

    int damage_ = 1;
    int timer_ = 0;
    bool hasHit_ = false;
    bool isPlayerCaster_ = true;
    bool isFinished_ = false;

    float casterYaw_ = 0.0f;
    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

    // 🌟 一瞬で振り抜く鋭さを出すため、20フレームに戻します
    const int effectDuration_ = 20;

    // 🌟 濃い残像を作る設定
    static const int maxAfterimageCount_ = 8;
    static const int defaultAfterimageLife_ = 12;
    std::vector<AfterimageData> afterimages_;
};