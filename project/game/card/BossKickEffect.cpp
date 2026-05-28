#include "BossKickEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/camera/Camera.h"
#include <cmath>

using namespace VectorMath;

void BossKickEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    // ボス専用攻撃なので isPlayerCaster は使わない
    (void)isPlayerCaster;

    casterBoss_ = casterBoss;
    camera_ = camera;
    casterYaw_ = casterYaw;
    timer_ = 0;
    isFinished_ = false;
    hasHit_ = false;

    // ボスの足元より少し前から蹴りを伸ばす
    startPos_ = { casterPos.x, casterPos.y + 1.0f, casterPos.z };

    Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
    pos_ = {
        startPos_.x + forward.x * 0.5f,
        startPos_.y,
        startPos_.z + forward.z * 0.5f
    };

    obj_ = Obj3d::Create("kick_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation(pos_);
        obj_->SetRotation({ 0.0f, casterYaw_, 0.0f });

        Model* model = obj_->GetModel();
        if (model) {
            model->SetTexture("resources/white1x1.png");
            Model::Material* material = model->GetMaterial();
            if (material) {
                // ボスクローに寄せた紫色にする
                material->color = { 0.7f, 0.2f, 1.0f, 1.0f };
                material->emissive = 1.5f;
            }
        }

        obj_->Update();
    }
}

void BossKickEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    // この攻撃では未使用
    (void)enemyManager;
    (void)boss;
    (void)extraBoss;
    (void)bossPos;
    (void)level;

    if (isFinished_) {
        return;
    }

    // 発動元ボスが消えたら攻撃も終了する
    if (!casterBoss_ || casterBoss_->IsDead()) {
        isFinished_ = true;
        return;
    }

    timer_++;

    Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };

    // 通常の蹴りと同じ伸び方にする
    float progress = static_cast<float>(timer_) / 15.0f;
    float distance = 0.5f + progress * 2.5f;

    pos_ = {
        startPos_.x + forward.x * distance,
        startPos_.y,
        startPos_.z + forward.z * distance
    };

    if (obj_) {
        obj_->SetTranslation(pos_);
        obj_->Update();

        // ボスクローに寄せた紫の軌跡（メインコア）
        GPUParticleManager::GetInstance()->Emit(
            pos_,
            { 0.0f, 0.0f, 0.0f },
            0.2f,
            1.1f,
            { 0.7f, 0.2f, 1.0f, 0.35f }
        );
        // 飛び散る小さな紫の粒
        for ( int i = 0; i < 5; i++ ) {
            Vector3 sparkVel = {
                ( rand() % 11 - 5 ) * 0.06f,
                ( rand() % 6 ) * 0.03f,
                ( rand() % 11 - 5 ) * 0.06f
            };
            GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.15f, 0.22f, { 0.8f, 0.3f, 1.0f, 0.8f });
        }
    }

    // 通常の蹴りと同じタイミングでヒット判定を出す
    if (timer_ >= 3 && timer_ <= 8 && !hasHit_) {
        Vector3 hitCenter = {
            startPos_.x + forward.x * 2.0f,
            startPos_.y,
            startPos_.z + forward.z * 2.0f
        };

        const float hitRadius = 2.2f; // ボスは体が大きいぶん少し広め
        int finalDamage = damage_;

        if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
            finalDamage /= 2; // ボス弱体化時はダメージ半減
        }

        if (player && !player->IsDead()) {
            Vector3 playerPos = player->GetPosition();
            Vector3 diff = {
                playerPos.x - hitCenter.x,
                0.0f,
                playerPos.z - hitCenter.z
            };

            if (Length(diff) < hitRadius) {
                player->TakeDamage(finalDamage, pos_, 2.2f);
                hasHit_ = true;

                // 命中のカメラシェイク
                if ( camera_ ) { camera_->TriggerShake(0.2f, 10); }

                // ヒット時の火花も紫系にする
                for (int i = 0; i < 20; i++) {
                    Vector3 sparkVel = {
                        (rand() % 11 - 5) * 0.12f,
                        (rand() % 11) * 0.05f,
                        (rand() % 11 - 5) * 0.12f
                    };

                    GPUParticleManager::GetInstance()->Emit(
                        pos_,
                        sparkVel,
                        0.35f,
                        0.7f,
                        { 0.7f, 0.2f, 1.0f, 1.0f }
                    );
                }
            }
        }
    }

    // 通常の蹴りと同じ終了タイミング
    if (timer_ > 15) {
        isFinished_ = true;
    }
}

void BossKickEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}