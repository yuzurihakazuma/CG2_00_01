#include "BossFierEffect.h"
#include "game/player/Player.h"
#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>

using namespace VectorMath;

void BossFierEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    isFinished_ = false;
    timer_ = 120; // 2秒

    camera_ = camera;
    casterBoss_ = casterBoss;

    pos_ = casterPos;
    pos_.y += 1.0f;

    Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };

    float speed = 0.5f;
    velocity_ = {
        forward.x * speed,
        0.0f,
        forward.z * speed
    };

    // =============================================
    // 発射瞬間の閃光（魔力が凝縮されるエフェクト）
    // =============================================
    // 白い閃光
    GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.10f, 2.5f, { 1.0f, 0.8f, 1.0f, 1.0f });
    // 紫コア
    GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.15f, 1.8f, { 0.7f, 0.0f, 1.0f, 1.0f });
    // 発射方向に粒子が弾ける（8個）
    for ( int i = 0; i < 8; i++ ) {
        float angle = static_cast<float>(rand() % 628) * 0.01f;
        float sp = 1.2f + static_cast<float>(rand() % 5) * 0.2f;
        Vector3 ev = {
            std::cosf(angle) * sp,
            0.3f + static_cast<float>(rand() % 4) * 0.15f,
            std::sinf(angle) * sp
        };
        GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.25f, 0.25f, { 0.8f, 0.2f, 1.0f, 0.9f });
    }
}

void BossFierEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {

    if ( isFinished_ ) return;

    // 弾を前へ進める
    pos_ += velocity_;

    // =============================================
    // 飛行中のトレイルパーティクル
    // =============================================

    rotAngle_ += 0.20f;

    // --- 内側リング（速く回転・明るい紫/ピンク、6個）---
    const int kInnerCount = 6;
    const float kInnerRadius = 0.38f;
    for ( int i = 0; i < kInnerCount; i++ ) {
        float angle = rotAngle_ + (3.14159f * 2.0f / kInnerCount) * i;
        Vector3 emitPos = {
            pos_.x + std::cosf(angle) * kInnerRadius,
            pos_.y + std::sinf(angle * 0.8f) * 0.18f,
            pos_.z + std::sinf(angle) * kInnerRadius
        };
        Vector3 vel = {
            -std::sinf(angle) * 1.0f + (rand() % 3 - 1) * 0.05f,
             0.15f + static_cast<float>(rand() % 4) * 0.06f,
             std::cosf(angle) * 1.0f + (rand() % 3 - 1) * 0.05f
        };
        float r = 0.6f + static_cast<float>(rand() % 4) * 0.1f;
        GPUParticleManager::GetInstance()->Emit(emitPos, vel, 0.25f, 0.24f, { r, 0.0f, 1.0f, 1.0f });
    }

    // --- 外側リング（逆回転・暗い深紫、5個）---
    const int kOuterCount = 5;
    const float kOuterRadius = 0.72f;
    for ( int i = 0; i < kOuterCount; i++ ) {
        float angle = -rotAngle_ * 0.55f + (3.14159f * 2.0f / kOuterCount) * i;
        Vector3 emitPos = {
            pos_.x + std::cosf(angle) * kOuterRadius,
            pos_.y + std::sinf(angle * 0.5f) * 0.15f,
            pos_.z + std::sinf(angle) * kOuterRadius
        };
        Vector3 vel = {
            std::sinf(angle) * 0.6f + (rand() % 3 - 1) * 0.04f,
            0.08f + static_cast<float>(rand() % 3) * 0.05f,
           -std::cosf(angle) * 0.6f + (rand() % 3 - 1) * 0.04f
        };
        float b = 0.55f + static_cast<float>(rand() % 3) * 0.1f;
        GPUParticleManager::GetInstance()->Emit(emitPos, vel, 0.38f, 0.34f, { 0.25f, 0.0f, b, 0.9f });
    }

    // --- コア（中心の白熱した核・4個）---
    for ( int i = 0; i < 4; i++ ) {
        Vector3 corePos = {
            pos_.x + (rand() % 5 - 2) * 0.06f,
            pos_.y + (rand() % 5 - 2) * 0.06f,
            pos_.z + (rand() % 5 - 2) * 0.06f
        };
        Vector3 coreVel = {
            (rand() % 3 - 1) * 0.12f,
            0.5f + static_cast<float>(rand() % 5) * 0.08f,
            (rand() % 3 - 1) * 0.12f
        };
        float bright = (i < 2) ? 1.0f : 0.85f;
        GPUParticleManager::GetInstance()->Emit(corePos, coreVel, 0.16f, 0.28f, { bright, bright * 0.85f, 1.0f, 1.0f });
    }

    // --- 周期スパーク（3フレームに1回、8個飛び散る）---
    sparkTimer_++;
    if ( sparkTimer_ % 3 == 0 ) {
        for ( int i = 0; i < 8; i++ ) {
            float sAngle = static_cast<float>(rand() % 628) * 0.01f;
            float sp = 1.8f + static_cast<float>(rand() % 8) * 0.3f;
            Vector3 sv = {
                std::cosf(sAngle) * sp,
                0.6f + static_cast<float>(rand() % 10) * 0.15f,
                std::sinf(sAngle) * sp
            };
            float r = 0.7f + static_cast<float>(rand() % 3) * 0.1f;
            GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.22f, 0.16f, { r, 0.1f, 1.0f, 1.0f });
        }
    }

    // --- 軌跡（進行逆方向・暗い魔力のしぶき、5個・大きめ）---
    Vector3 trailBase = {
        pos_.x - velocity_.x * 2.8f,
        pos_.y,
        pos_.z - velocity_.z * 2.8f
    };
    for ( int i = 0; i < 5; i++ ) {
        Vector3 trailPos = {
            trailBase.x + (rand() % 7 - 3) * 0.14f,
            trailBase.y + (rand() % 5 - 2) * 0.14f,
            trailBase.z + (rand() % 7 - 3) * 0.14f
        };
        Vector3 trailVel = {
            (rand() % 3 - 1) * 0.12f,
            0.15f + static_cast<float>(rand() % 5) * 0.06f,
            (rand() % 3 - 1) * 0.12f
        };
        float b = 0.5f + static_cast<float>(rand() % 4) * 0.1f;
        GPUParticleManager::GetInstance()->Emit(trailPos, trailVel, 0.55f, 0.42f, { 0.25f, 0.0f, b, 0.80f });
    }

    // =============================================
    // 飛行中の熱波歪み（弾の位置を毎フレーム更新し続ける）
    // =============================================
    if ( camera_ ) {
        const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
        float hcx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
        float hcy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
        float hcw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
        if ( hcw > 0.001f ) {
            // タイマー3フレームで毎フレーム呼ぶ → 常に弾を追いかけて揺らぎ続ける
            PostEffect::GetInstance()->TriggerDistortion(
                hcx/hcw*0.5f+0.5f, -hcy/hcw*0.5f+0.5f, 0.13f, 3);
        }
    }

    // =============================================
    // プレイヤーへの当たり判定
    // =============================================
    if ( player && !player->IsDead() ) {
        Vector3 playerPos = player->GetPosition();
        Vector3 diff = {
            playerPos.x - pos_.x,
            playerPos.y - pos_.y,
            playerPos.z - pos_.z
        };

        if ( Length(diff) < 2.6f ) {
            int finalDamage = damage_;
            if ( casterBoss_ && casterBoss_->IsAttackDebuffed() ) {
                finalDamage = finalDamage / 2;
            }
            player->TakeDamage(finalDamage, pos_);

            // =============================================
            // 命中爆発エフェクト
            // =============================================

            // 白閃光 → 紫コアの2段フラッシュ
            GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.10f, 4.0f, { 1.0f, 0.85f, 1.0f, 1.0f });
            GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.18f, 2.8f, { 0.8f, 0.0f, 1.0f, 1.0f });

            // 外へ弾け飛ぶ魔力の破片（35個）
            for ( int j = 0; j < 35; j++ ) {
                float angle = static_cast<float>(rand() % 628) * 0.01f;
                float sp = 2.0f + static_cast<float>(rand() % 12) * 0.25f;
                Vector3 ev = {
                    std::cosf(angle) * sp,
                    0.4f + static_cast<float>(rand() % 10) * 0.18f,
                    std::sinf(angle) * sp
                };
                float r = 0.4f + static_cast<float>(rand() % 4) * 0.12f;
                float b = 0.7f + static_cast<float>(rand() % 3) * 0.1f;
                GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.40f, 0.30f, { r, 0.0f, b, 1.0f });
            }

            // 上昇する魔力の光柱（15個）
            for ( int j = 0; j < 15; j++ ) {
                Vector3 sv = {
                    (rand() % 11 - 5) * 0.35f,
                    1.8f + static_cast<float>(rand() % 10) * 0.25f,
                    (rand() % 11 - 5) * 0.35f
                };
                GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.35f, 0.22f, { 0.95f, 0.6f, 1.0f, 1.0f });
            }

            // 地面リングバースト（20個）
            Vector3 groundPos = { pos_.x, pos_.y - 0.8f, pos_.z };
            for ( int j = 0; j < 20; j++ ) {
                float angle = (3.14159f * 2.0f / 20.0f) * j;
                float sp = 0.9f + static_cast<float>(rand() % 4) * 0.15f;
                Vector3 rv = { std::sinf(angle) * sp, 0.04f, std::cosf(angle) * sp };
                GPUParticleManager::GetInstance()->Emit(groundPos, rv, 0.35f, 0.65f, { 0.6f, 0.0f, 1.0f, 1.0f });
            }

            // 中段リング（14個）
            for ( int j = 0; j < 14; j++ ) {
                float angle = (3.14159f * 2.0f / 14.0f) * j;
                Vector3 rv = { std::sinf(angle) * 0.6f, 0.06f, std::cosf(angle) * 0.6f };
                GPUParticleManager::GetInstance()->Emit(
                    { pos_.x, pos_.y + 0.5f, pos_.z }, rv, 0.30f, 0.5f, { 0.9f, 0.3f, 1.0f, 0.95f });
            }

            // カメラシェイク
            if ( camera_ ) {
                camera_->TriggerShake(0.22f, 14);
            }

            // 着弾点に熱波歪みを一時発生（ボス炎弾は大きめ・長め）
            if ( camera_ ) {
                const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
                float hcx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
                float hcy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
                float hcw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
                if ( hcw > 0.001f ) {
                    PostEffect::GetInstance()->TriggerDistortion(
                        hcx/hcw*0.5f+0.5f, -hcy/hcw*0.5f+0.5f, 0.28f, 55);
                }
            }

            isFinished_ = true;
            return;
        }
    }

    // 寿命切れ（壁抜け防止）
    timer_--;
    if ( timer_ <= 0 ) {
        isFinished_ = true;
    }
}

void BossFierEffect::Draw() {
}
