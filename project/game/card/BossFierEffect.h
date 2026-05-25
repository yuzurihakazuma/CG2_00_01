#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/camera/Camera.h"
#include <memory>

class BossFierEffect : public ICardEffect {
public:
    // コンストラクタでCSVのダメージ量を受け取る
    BossFierEffect(int damage) : damage_(damage) {}

    // 初期化
// 分裂ボス時に、どの個体が発動した攻撃かを受け取れるようにする
    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

    // 更新
    void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) override;

    // 描画
    void Draw() override;

    // 終了判定
    bool IsFinished() const override { return isFinished_; }
private:

   
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };

    int damage_ = 10;
    int timer_ = 0;
    int sparkTimer_ = 0;      // 周期スパーク用タイマー
    float rotAngle_ = 0.0f;   // 螺旋リング回転角（static廃止→インスタンス毎に独立）
    bool isFinished_ = false;

    Camera* camera_ = nullptr;     // スクリーンUV変換 / シェイク用

    // この攻撃を発動したボス本人
// 分裂ボス時に左右どちらの弾かを失わないように保持する
    Boss* casterBoss_ = nullptr;

};

