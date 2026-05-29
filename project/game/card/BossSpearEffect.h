#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>
#include <vector>
#include <array>
#include <algorithm>

class BossSpearEffect : public ICardEffect {
public:
    BossSpearEffect(int damage) : damage_(damage) {}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override { return isFinished_; }

private:
    // フェーズ定義
    // 0: 展開（3本が扇形に浮かぶ）
    // 1: 一斉突き
    // 2: 引き戻し
    // 3: フィニッシュ（中央1本だけ深く追撃）
    // 4: 終了戻し
    static constexpr int kPhaseDeploy   = 20; // 展開フレーム数
    static constexpr int kPhaseThrust   = 8;  // 突きフレーム数
    static constexpr int kPhaseRetract  = 12; // 戻しフレーム数
    static constexpr int kPhaseFinish   = 8;  // フィニッシュフレーム数
    static constexpr int kPhaseFinishR  = 10; // フィニッシュ戻しフレーム数

    static constexpr float kSpreadAngle = 0.5236f; // 扇の広がり角（30°）

    struct SpearData {
        std::unique_ptr<Obj3d> obj = nullptr;
        Vector3 pos = { 0.0f, 0.0f, 0.0f };
        float yaw = 0.0f; // 各槍の向き
        bool hitDone = false;
    };

    Vector3 scale_ = { 2.0f, 2.0f, 2.0f };

    int damage_ = 1;
    int timer_ = 0;
    bool isFinished_ = false;

    float casterYaw_ = 0.0f;
    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

    Boss* casterBoss_ = nullptr;

    // 3本の槍（左・中央・右）
    std::array<SpearData, 3> spears_;
};
