#include "KickEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void KickEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
    // この効果は発動元ボスを使わない
    (void)casterBoss;
    pos_ = casterPos;
    casterYaw_ = casterYaw;
    isPlayerCaster_ = isPlayerCaster;
    timer_ = 0;
    isFinished_ = false;
    hasHit_ = false;

    // ==================================================
    // 拳モデルの初期化処理（蹴りっぽく見せるため少し下から）
    // ==================================================
    startPos_ = { casterPos.x, casterPos.y + 0.6f, casterPos.z };
    Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
    pos_ = {
        startPos_.x + forward.x * 0.5f,
        startPos_.y,
        startPos_.z + forward.z * 0.5f
    };

    obj_ = Obj3d::Create("kick_model"); // 拳モデルを代用
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation(pos_);
        obj_->SetRotation({ 0.0f, casterYaw_, 0.0f });

        Model *model = obj_->GetModel();
        if (model) {
            model->SetTexture("resources/white1x1.png");
            Model::Material *material = model->GetMaterial();
            if (material) {
                // 拳(黄)と区別するため、蹴りは青系の色にする
                material->color = { 0.2f, 0.8f, 1.0f, 1.0f };
                material->emissive = 1.2f;
            }
        }
        obj_->Update();

        Vector3 fwd = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };

        // ① 縦方向リング（蹴り軌道面を表す — 横ではなく縦に広がる）
        for (int i = 0; i < 16; i++) {
            float ringAngle = (3.14159f * 2.0f / 16.0f) * static_cast<float>(i);
            float speed = 0.55f;
            Vector3 ringVel = {
                fwd.x * std::cosf(ringAngle) * speed,
                std::sinf(ringAngle) * speed,
                fwd.z * std::cosf(ringAngle) * speed
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, ringVel, 0.18f, 0.22f, { 0.3f, 0.85f, 1.0f, 0.95f });
        }

        // ② 斜め上前方への電撃ストリーク（蹴り上げ感）
        for (int i = 0; i < 20; i++) {
            float spread = static_cast<float>(rand() % 11 - 5) * 0.06f;
            float fwdSpeed = 0.4f + static_cast<float>(rand() % 8) * 0.08f;
            float upSpeed = 0.15f + static_cast<float>(rand() % 10) * 0.06f;
            Vector3 vel = {
                fwd.x * fwdSpeed + spread,
                upSpeed,
                fwd.z * fwdSpeed + spread
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, vel, 0.18f, 0.15f, { 0.6f, 0.95f, 1.0f, 1.0f });
        }

        // ③ 真上に散る電撃（蹴り上げた衝撃）
        for (int i = 0; i < 10; i++) {
            Vector3 vel = {
                (rand() % 11 - 5) * 0.08f,
                0.5f + static_cast<float>(rand() % 8) * 0.08f,
                (rand() % 11 - 5) * 0.08f
            };
            GPUParticleManager::GetInstance()->Emit(
                pos_, vel, 0.15f, 0.15f, { 0.9f, 1.0f, 1.0f, 1.0f });
        }

        // ④ 接地衝撃波（薄い水平リング）
        for (int i = 0; i < 10; i++) {
            float angle = (3.14159f * 2.0f / 10.0f) * static_cast<float>(i);
            Vector3 ringVel = { std::sinf(angle) * 0.4f, 0.02f, std::cosf(angle) * 0.4f };
            GPUParticleManager::GetInstance()->Emit(
                pos_, ringVel, 0.12f, 0.18f, { 0.2f, 0.8f, 1.0f, 0.7f });
        }
    }
}

void KickEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) {
    if (isFinished_) return;
    timer_++;

    // プレイヤーの向いている方向を計算
    Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };

    // ==================================================
    // ★ 前に突き出すアニメーション ＆ 青いオーラ
    // ==================================================
    float progress = static_cast<float>(timer_) / 15.0f; // 15フレームで完了
    float distance = 0.5f + progress * 2.5f;

    pos_ = {
        startPos_.x + forward.x * distance,
        startPos_.y,
        startPos_.z + forward.z * distance
    };

    if (obj_) {
        obj_->SetTranslation(pos_);
        obj_->Update();
        // 電撃トレイル（小さく鋭い）
        for (int i = 0; i < 3; i++) {
            Vector3 trailPos = {
                pos_.x + (rand() % 5 - 2) * 0.06f,
                pos_.y + static_cast<float>(rand() % 3) * 0.08f,
                pos_.z + (rand() % 5 - 2) * 0.06f
            };
            GPUParticleManager::GetInstance()->Emit(
                trailPos,
                { (rand() % 7 - 3) * 0.03f, 0.04f, (rand() % 7 - 3) * 0.03f },
                0.18f, 0.2f, { 0.2f, 0.85f, 1.0f, 0.9f });
        }
        // 蹴りのコアに鋭い電撃光点
        GPUParticleManager::GetInstance()->Emit(
            pos_, { 0.0f, 0.0f, 0.0f }, 0.12f, 0.6f, { 0.4f, 0.9f, 1.0f, 0.65f });
    }


    // 蹴りの発生フレーム（例：3〜8フレームの間に判定を出す）
    if (timer_ >= 3 && timer_ <= 8 && !hasHit_) {

        // 当たり判定の中心座標（モデルの少し前に判定を出す）
        Vector3 hitCenter = {
            startPos_.x + forward.x * 2.0f,
            startPos_.y,
            startPos_.z + forward.z * 2.0f
        };

        float hitRadius = 2.0f; // 蹴りの判定サイズ
        int randomDamage = damage_ + (rand() % 2); // ★ ランダムダメージ（1〜2）

        if (isPlayerCaster_) {
            // --------------------------------------------------
            // ① ボスへの判定
            // --------------------------------------------------
            if (boss && !boss->IsDead()) {
                Vector3 bossDiff = { bossPos.x - hitCenter.x, 0.0f, bossPos.z - hitCenter.z };
                if (Length(bossDiff) < hitRadius + 2.0f) { // ボスは当たりやすいように判定を広く
                    boss->TakeDamage(randomDamage);
                    hasHit_ = true;
                    boss->ApplyKnockback(forward * 0.8f);

                    // ★ ヒット時パーティクル
                    for (int i = 0; i < 20; i++) {
                        Vector3 sparkVel = {
                            (rand() % 11 - 5) * 0.2f, (rand() % 11 - 5) * 0.2f, (rand() % 11 - 5) * 0.2f
                        };
                        GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.2f, 0.2f, { 0.2f, 0.8f, 1.0f, 1.0f });
                    }
                }
            }

            // --------------------------------------------------
            // ② 雑魚敵への判定
            // --------------------------------------------------
            if (enemyManager) {
                for (auto &enemy : enemyManager->GetEnemies()) {
                    if (enemy && !enemy->IsDead()) {
                        Vector3 ePos = enemy->GetPosition();
                        Vector3 diff = { ePos.x - hitCenter.x, 0.0f, ePos.z - hitCenter.z };

                        if (Length(diff) < hitRadius) {
                            enemy->TakeDamage(randomDamage);
                            hasHit_ = true;
                            enemy->ApplyKnockback(forward * 1.5f);

                            // ★ ヒット時パーティクル
                            for (int i = 0; i < 20; i++) {
                                Vector3 sparkVel = {
                                    (rand() % 11 - 5) * 0.2f, (rand() % 11 - 5) * 0.2f, (rand() % 11 - 5) * 0.2f
                                };
                                GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.2f, 0.2f, { 0.2f, 0.8f, 1.0f, 1.0f });
                            }
                        }
                    }
                }
            }
        }
    }

    // 蹴りのモーションが終わる時間
    if (timer_ > 15) {
        isFinished_ = true;
    }
}

void KickEffect::Draw() {
    if (isFinished_) return;
    if (obj_) obj_->Draw();
}
