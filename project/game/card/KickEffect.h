#pragma once
#include "game/card/ICardEffect.h"
class KickEffect : public ICardEffect {
public:
    KickEffect(int damage) : damage_(damage) {}

    void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) override;
    void Update(Player *player, EnemyManager *enemyManager, Boss *boss, const Vector3 &bossPos, const LevelData &level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    std::unique_ptr<Obj3d> obj_ = nullptr; // ★ モデル表示用
    Vector3 scale_ = { 1.2f, 1.2f, 1.2f }; // ★ モデルのサイズ

    int damage_ = 1;
    int timer_ = 0;
    bool isFinished_ = false;
    bool hasHit_ = false; // 1回の蹴りで複数回ダメージが入らないようにする

    Vector3 pos_;
	Vector3 startPos_; // 蹴りの開始位置
    float casterYaw_ = 0.0f;
    bool isPlayerCaster_ = true;
};

