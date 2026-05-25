#include "IceBulletEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>

using namespace VectorMath;

void IceBulletEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
    // この効果は発動元ボスを使わない
    ( void ) casterBoss;
    isPlayerCaster_ = isPlayerCaster;
    camera_ = camera;
    isFinished_ = false;

    delayTimer_ = 30; // 0.5秒間（30フレーム）の「発動前クールタイム（予兆）」を作る
    timer_ = 60;      // その後、1秒間（60フレーム）雪を降らせる
    startTimer_ = timer_;

    // 魔法陣の中心を少し前(5.0f)に設置
    Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };
    pos_ = {
        casterPos.x + forward.x * 5.0f,
        casterPos.y, // 地面
        casterPos.z + forward.z * 5.0f
    };

    // 範囲インジケーター（危険表示）を作成
    indicatorObj_ = Obj3d::Create("sphere");
    if ( indicatorObj_ ) {
        indicatorObj_->SetCamera(camera);
        indicatorObj_->SetTranslation(pos_);
        indicatorObj_->SetScale({ range_, 0.05f, range_ });

        Model* model = indicatorObj_->GetModel();
        if ( model ) {
            model->SetTexture("resources/white1x1.png");
            Model::Material* material = model->GetMaterial();
            if ( material ) {
                material->color = { 1.0f, 0.0f, 0.0f, 0.1f };
                material->emissive = 1.0f;
            }
        }
        indicatorObj_->Update();
    }

    // =========================================
    // 詠唱開始エフェクト（最初の1回）
    // =========================================
    // 着弾地点から青白い粒子が湧き上がる
    for ( int i = 0; i < 25; i++ ) {
        float angle = static_cast< float >(rand()) / RAND_MAX * 3.14159f * 2.0f;
        float r = static_cast< float >(rand()) / RAND_MAX * range_;
        Vector3 sp = {
            pos_.x + std::sinf(angle) * r,
            pos_.y + 0.05f,
            pos_.z + std::cosf(angle) * r
        };
        Vector3 sv = {
            ( rand() % 11 - 5 ) * 0.05f,
            0.5f + ( rand() % 10 ) * 0.12f,
            ( rand() % 11 - 5 ) * 0.05f
        };
        GPUParticleManager::GetInstance()->Emit(sp, sv, 0.6f, 0.2f, { 0.5f, 0.85f, 1.0f, 0.7f });
    }
    // 詠唱地点のフラッシュ
    GPUParticleManager::GetInstance()->Emit(
        { pos_.x, pos_.y + 0.05f, pos_.z },
        { 0, 0, 0 }, 0.15f, 3.5f, { 0.7f, 0.9f, 1.0f, 0.6f }
    );
}

void IceBulletEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){

    if ( isFinished_ ) return;

    // ==========================================
    // 🌟 発動前の予兆（クールタイム）処理
    // ==========================================
    if ( delayTimer_ > 0 ) {
        delayTimer_--;

        if ( indicatorObj_ ) {
            Model* model = indicatorObj_->GetModel();
            if ( model && model->GetMaterial() ) {
                Model::Material* material = model->GetMaterial();
                float progress = 1.0f - ( static_cast< float >( delayTimer_ ) / 30.0f );
                material->color.w = 0.1f + ( progress * 0.4f );
            }
            indicatorObj_->Update();
        }

        // =========================================
        // 予兆パーティクル：上空から着弾点へ降り注ぐ予兆
        // =========================================
        for ( int i = 0; i < 8; i++ ) {
            float angle = static_cast< float >(rand()) / RAND_MAX * 3.14159f * 2.0f;
            float r = static_cast< float >(rand()) / RAND_MAX * range_;
            // 上の方からランダムな位置に出現し、地面方向へ向かう
            Vector3 warnPos = {
                pos_.x + std::sinf(angle) * r,
                pos_.y + 2.5f + ( rand() % 6 ) * 0.3f,
                pos_.z + std::cosf(angle) * r
            };
            Vector3 warnVel = {
                ( rand() % 7 - 3 ) * 0.03f,
                -0.8f - ( rand() % 5 ) * 0.1f,
                ( rand() % 7 - 3 ) * 0.03f
            };
            GPUParticleManager::GetInstance()->Emit(warnPos, warnVel, 0.4f, 0.18f, { 0.45f, 0.75f, 1.0f, 0.65f });
        }
        // 予兆後半（残り10フレーム）でリングが広がる
        if ( delayTimer_ < 10 ) {
            float step = 3.14159f * 2.0f / 16.0f;
            for ( int i = 0; i < 16; i++ ) {
                float angle = step * i;
                float speed = 0.1f + 0.02f * ( 10 - delayTimer_ );
                Vector3 rv = { std::sinf(angle) * speed, 0.02f, std::cosf(angle) * speed };
                GPUParticleManager::GetInstance()->Emit(
                    { pos_.x, pos_.y + 0.05f, pos_.z },
                    rv, 0.2f, 0.5f, { 0.4f, 0.8f, 1.0f, 0.5f }
                );
            }
        }

        return;
    }

    // ==========================================
    // ❄️ ここから下が実際の発動（氷が降る）処理
    // ==========================================
    timer_--;
    if ( timer_ <= 0 ) {
        isFinished_ = true;
        return;
    }

    if ( indicatorObj_ ) {
        Model* model = indicatorObj_->GetModel();
        if ( model && model->GetMaterial() ) {
            Model::Material* material = model->GetMaterial();
            float alpha = static_cast< float >( timer_ ) / static_cast< float >( startTimer_ ) * 0.5f;
            material->color.w = alpha;
        }
        indicatorObj_->Update();
    }

    // 1. パーティクル：円の範囲に上から雪と氷を降らせる
    for ( int i = 0; i < 15; i++ ) {
        float angle = static_cast< float >(rand()) / RAND_MAX * 3.141592f * 2.0f;
        float r = ( static_cast< float >(rand()) / RAND_MAX ) * range_;

        Vector3 pPos = {
            pos_.x + std::sinf(angle) * r,
            pos_.y + 3.0f + ( rand() % 10 * 0.1f ),
            pos_.z + std::cosf(angle) * r
        };

        Vector3 pVel = {
            ( rand() % 11 - 5 ) * 0.02f,
            -3.0f - static_cast< float >( rand() % 20 ) * 0.1f,
            ( rand() % 11 - 5 ) * 0.02f
        };

        Vector4 color = { 0.5f, 0.8f, 1.0f, 0.8f };
        float scale = 0.15f + static_cast< float >( rand() % 4 ) * 0.1f;

        if ( rand() % 10 == 0 ) {
            color = { 1.0f, 1.0f, 1.0f, 1.0f };
            scale = 0.4f + static_cast< float >( rand() % 4 ) * 0.1f;
        }

        GPUParticleManager::GetInstance()->Emit(pPos, pVel, 1.0f, scale, color);
    }

    // ==========================================
    // ❄️ 2. 円形の当たり判定
    // ==========================================
    if ( timer_ == 59 ) {

        // =========================================
        // フリーズ発動バースト！（命中の瞬間）
        // =========================================

        // 外側に弾け飛ぶ氷の欠片（60個）
        for ( int i = 0; i < 60; i++ ) {
            float angle = static_cast< float >(rand()) / RAND_MAX * 3.14159f * 2.0f;
            float r = static_cast< float >(rand()) / RAND_MAX * range_;
            Vector3 bp = {
                pos_.x + std::sinf(angle) * r,
                pos_.y + 0.1f + ( rand() % 8 ) * 0.25f,
                pos_.z + std::cosf(angle) * r
            };
            Vector3 bv = {
                ( rand() % 21 - 10 ) * 0.18f,
                0.3f + ( rand() % 10 ) * 0.12f,
                ( rand() % 21 - 10 ) * 0.18f
            };
            float brightness = 0.7f + ( rand() % 4 ) * 0.08f;
            GPUParticleManager::GetInstance()->Emit(bp, bv, 0.6f, 0.22f, { brightness * 0.5f, brightness * 0.88f, 1.0f, 1.0f });
        }
        // 地面リング（大・低め）
        for ( int i = 0; i < 24; i++ ) {
            float angle = ( 3.14159f * 2.0f / 24.0f ) * i;
            float speed = 0.55f;
            Vector3 rv = { std::sinf(angle) * speed, 0.05f, std::cosf(angle) * speed };
            GPUParticleManager::GetInstance()->Emit(
                { pos_.x, pos_.y + 0.08f, pos_.z },
                rv, 0.4f, 0.9f, { 0.3f, 0.75f, 1.0f, 1.0f }
            );
        }
        // 中段リング
        for ( int i = 0; i < 18; i++ ) {
            float angle = ( 3.14159f * 2.0f / 18.0f ) * i;
            float speed = 0.4f;
            Vector3 rv = { std::sinf(angle) * speed, 0.06f, std::cosf(angle) * speed };
            GPUParticleManager::GetInstance()->Emit(
                { pos_.x, pos_.y + 0.9f, pos_.z },
                rv, 0.35f, 0.7f, { 0.5f, 0.88f, 1.0f, 0.95f }
            );
        }
        // 上段リング
        for ( int i = 0; i < 14; i++ ) {
            float angle = ( 3.14159f * 2.0f / 14.0f ) * i;
            float speed = 0.28f;
            Vector3 rv = { std::sinf(angle) * speed, 0.08f, std::cosf(angle) * speed };
            GPUParticleManager::GetInstance()->Emit(
                { pos_.x, pos_.y + 1.8f, pos_.z },
                rv, 0.3f, 0.55f, { 0.65f, 0.92f, 1.0f, 0.9f }
            );
        }
        // 氷柱（上に突き抜けるスパイク）
        for ( int i = 0; i < 15; i++ ) {
            float angle = static_cast< float >(rand()) / RAND_MAX * 3.14159f * 2.0f;
            float r = static_cast< float >(rand()) / RAND_MAX * range_ * 0.8f;
            Vector3 sp = { pos_.x + std::sinf(angle) * r, pos_.y + 0.05f, pos_.z + std::cosf(angle) * r };
            Vector3 sv = { ( rand() % 5 - 2 ) * 0.04f, 1.8f + ( rand() % 8 ) * 0.2f, ( rand() % 5 - 2 ) * 0.04f };
            GPUParticleManager::GetInstance()->Emit(sp, sv, 0.5f, 0.28f, { 0.75f, 0.95f, 1.0f, 1.0f });
        }
        // 巨大フラッシュ（白）
        GPUParticleManager::GetInstance()->Emit(
            { pos_.x, pos_.y + 0.5f, pos_.z },
            { 0, 0, 0 }, 0.18f, 6.0f, { 0.85f, 0.95f, 1.0f, 0.9f }
        );
        // 青コアフラッシュ
        GPUParticleManager::GetInstance()->Emit(
            { pos_.x, pos_.y + 0.5f, pos_.z },
            { 0, 0, 0 }, 0.12f, 3.5f, { 0.3f, 0.7f, 1.0f, 1.0f }
        );

        // 着弾点に空気歪みを一時発生（凍結で空気が揺らぐ演出）
        if ( camera_ ) {
            const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
            float icx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
            float icy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
            float icw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
            if ( icw > 0.001f ) {
                PostEffect::GetInstance()->TriggerDistortion(
                    icx/icw*0.5f+0.5f, -icy/icw*0.5f+0.5f, 0.25f, 55);
            }
        }

        // =========================================
        // ヒット判定（元のまま）
        // =========================================
        if ( isPlayerCaster_ ) {
            // --- 雑魚敵への判定 ---
            if ( enemyManager ) {
                for ( auto& enemy : enemyManager->GetEnemies() ) {
                    if ( enemy && !enemy->IsDead() ) {
                        Vector3 ePos = enemy->GetPosition();
                        Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                        if ( Length(diff) < range_ ) {
                            enemy->TakeDamage(damage_); // ダメージ1
                            enemy->Freeze(120); // 凍結
                        }
                    }
                }
            }
            // --- ボスへの判定 ---
            Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(pos_, range_, boss, extraBoss);
            if ( hitBoss ) {
                hitBoss->TakeDamage(damage_); // ダメージ1
                hitBoss->Freeze(60); // 凍結
            }
        }
        // --- 敵が使った場合（プレイヤーへの判定） ---
        else {
            if ( player && !player->IsDead() ) {
                Vector3 pPos = player->GetPosition();
                Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };

                if ( Length(diff) < range_ ) {
                    int dmg = boss && boss->IsAttackDebuffed() ? damage_ / 2 : damage_;
                    player->TakeDamage(dmg, pos_);
                }
            }
        }
    }
}

void IceBulletEffect::Draw(){
    if ( indicatorObj_ && !isFinished_ ) {
        indicatorObj_->Draw();
    }
}