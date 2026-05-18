#include "BossFierEffect.h"
#include "game/player/Player.h"
#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void BossFierEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    isFinished_ = false;
    timer_ = 120; // 2秒

    // この攻撃を発動したボス本人を保持する
// 今は主に分裂ボス時の発動元追跡のために使う
    casterBoss_ = casterBoss;

    pos_ = casterPos;
    pos_.y += 1.0f; // 少し浮かせる

    Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };

    // 弾のスピード（避けられるように少し遅めに設定。速くしたい場合は数字を大きく！）
    float speed = 0.5f;
    velocity_ = {
        forward.x * speed,
        0.0f,
        forward.z * speed
    };

   
}

void BossFierEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {

    if (isFinished_) {
        return;
    }

    // 弾を前へ進める
    pos_ = {
         pos_.x + velocity_.x,
         pos_.y + velocity_.y,
         pos_.z + velocity_.z
    };
   
    // ==========================================
    // パーティクル放出処理
    // ==========================================
    Vector4 mainColor = { 0.7f, 0.0f, 1.0f, 1.0f }; // 明るめの紫
    Vector4 subColor = { 0.4f, 0.0f, 0.8f, 1.0f };  // 濃い紫


    // ファイアーボールの軌跡
    for (int i = 0; i < 3; i++) {
        Vector3 particlePos = pos_;
        particlePos.x += (rand() % 11 - 5) * 0.2f;
        particlePos.y += (rand() % 11 - 5) * 0.2f;
        particlePos.z += (rand() % 11 - 5) * 0.2f;

        Vector3 particleVel = {
            (rand() % 11 - 5) * 0.02f,
            (rand() % 11 - 5) * 0.02f,
            (rand() % 11 - 5) * 0.02f
        };

        float scale = 0.5f + (rand() % 11) * 0.15f;
        float lifeTime = 0.5f + (rand() % 11) * 0.05f;
        Vector4 color = (rand() % 2 == 0) ? mainColor : subColor;

        GPUParticleManager::GetInstance()->Emit(particlePos, particleVel, scale, lifeTime, color);
    }

    // --------------------------------------------------
    // プレイヤーへの当たり判定
    // --------------------------------------------------
    if (player && !player->IsDead()) {
        Vector3 playerPos = player->GetPosition();
        Vector3 diff = {
            playerPos.x - pos_.x,
            playerPos.y - pos_.y,
            playerPos.z - pos_.z
        };

        // スケールが大きいので当たり判定も広くする（2.0fくらい）
        if (Length(diff) < 2.0f) {
            int finalDamage = damage_;

            // 発動元のボスが攻撃デバフ中ならダメージを半減する
            // 分裂ボス時に boss 引数へ依存すると、別個体を参照する可能性がある
            if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
                finalDamage = finalDamage / 2;
            }

            // PlayerのTakeDamageは「ダメージ量」と「攻撃が飛んできた位置」を渡す
            player->TakeDamage(finalDamage, pos_);
            isFinished_ = true; // 当たったら消える
            return;
        }
    }

    // 寿命が尽きたら消える（壁抜け防止）
    timer_--;
    if (timer_ <= 0) {
        isFinished_ = true;
    }
}

void BossFierEffect::Draw() {
   
}


