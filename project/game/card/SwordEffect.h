#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>
#include <vector>
#include <algorithm>

class SwordEffect : public ICardEffect{
public:
    // 🌟 復活：あなたの元のコンストラクタ
    SwordEffect(int damage) : damage_(damage){}

    void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera) override;
    void Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level) override;
    void Draw() override;
    bool IsFinished() const override{ return isFinished_; }

private:
    // PRS (Position, Rotation, Scale) をコピーするヘルパー関数
    void CopyPRS(const std::unique_ptr<Obj3d>& source, const std::unique_ptr<Obj3d>& dest);

    // 残像のデータを覚える構造体
    struct Afterimage{
        std::unique_ptr<Obj3d> obj = nullptr;
        int lifeTimer = 0;
        bool isActive = false;
    };

private:
    std::unique_ptr<Obj3d> obj_ = nullptr;
    Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ = { 1.5f, 1.5f, 1.5f };

    // 🌟 復活：消えてしまっていた変数たち
    int damage_ = 1;
    int timer_ = 0;
    bool hasHit_ = false;
    bool isPlayerCaster_ = true;
    bool isFinished_ = false;

    float casterYaw_ = 0.0f;
    Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };
    Vector3 playerPos_ = { 0.0f, 0.0f, 0.0f };

    int effectDuration_ = 20; // エフェクト全体の時間

    // ==========================================
    // 🌟 追加：剣の残像システム
    // ==========================================
    static const int kAfterimageCount = 6;
    static const int kAfterimageLife = 10;
    std::vector<Afterimage> afterimages_;
};