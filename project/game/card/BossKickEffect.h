#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class BossKickEffect : public ICardEffect {
public:
    BossKickEffect(int damage) : damage_(damage) {}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 scale_ = { 1.8f, 1.8f, 1.8f };

    int damage_     = 1;
    int timer_      = 0;
    bool isFinished_ = false;
    bool hasHit_    = false;

    Vector3 pos_{};
    Vector3 startPos_{};   // ため開始位置（地面）
    float   baseY_ = 0.0f; // 地面のY座標
    float   casterYaw_  = 0.0f;
    float   spinYaw_    = 0.0f; // 回し蹴り中の現在ヨー角

    // フェーズ定数
    static constexpr int kWindupEnd = 18; // ため終了
    static constexpr int kJumpEnd   = 25; // ジャンプ終了
    static constexpr int kSpinEnd   = 38; // 回し蹴り終了
    static constexpr int kLandEnd   = 45; // 着地終了（＝全体終了）

    // 回し蹴りの半径と高さ
    static constexpr float kKickRadius = 3.5f;
    static constexpr float kJumpHeight = 3.0f;

    Boss*   casterBoss_ = nullptr;
    Camera* camera_     = nullptr;
};
