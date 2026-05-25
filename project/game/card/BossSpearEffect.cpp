#include "BossSpearEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

void BossSpearEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    // ボス専用なので isPlayerCaster は使わない
    (void)isPlayerCaster;

    isFinished_ = false;
    timer_ = 0;
    hitTargets_.clear();
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;
    casterBoss_ = casterBoss; // 攻撃デバフ判定用に発動元を保持

    obj_ = Obj3d::Create("spear_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model* model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material* mat = model->GetMaterial();
            mat->color = { 0.7f, 0.2f, 1.0f, 1.0f }; // ボス用の紫
            mat->emissive = 2.8f; // 少し強めに発光
        }
    }

    afterimages_.clear();
    for (int index = 0; index < maxAfterimageCount_; index++) {
        AfterimageData afterimage;
        afterimage.object = Obj3d::Create("spear_model");
        if (afterimage.object) {
            afterimage.object->SetCamera(camera);
            afterimage.object->SetScale(scale_);

            Model* model = afterimage.object->GetModel();
            if (model && model->GetMaterial()) {
                Model::Material* mat = model->GetMaterial();
                mat->color = { 0.7f, 0.2f, 1.0f, 1.0f }; // 残像も同じ紫にする
                mat->emissive = 2.0f;
            }
        }
        afterimage.isActive = false;
        afterimages_.push_back(std::move(afterimage));
    }
}

void BossSpearEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    // このボス槍では未使用
    (void)enemyManager;
    (void)boss;
    (void)extraBoss;
    (void)bossPos;
    (void)level;

    if (isFinished_) {
        return;
    }

    timer_++;

    // 15フレームを1セットとして3連突きする
    int cycle = (timer_ - 1) / 15;
    int localTimer = (timer_ - 1) % 15;

    if (cycle >= 3) {
        isFinished_ = true;
        return;
    }

    Vector3 forwardVec = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
    Vector3 rightVec = { std::cosf(casterYaw_), 0.0f, -std::sinf(casterYaw_) };

    if (obj_) {
        if (localTimer == 0) {
            hitTargets_.clear(); // 突きごとにヒット対象をリセット
        }

        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if (cycle == 0) {
            offsetX = -0.4f;
            offsetY = 0.2f;
        } else if (cycle == 1) {
            offsetX = 0.4f;
            offsetY = -0.2f;
        }

        // 槍を一度引いてから前へ大きく突き出す
        float zOffset = 0.0f;
        if (localTimer <= 6) {
            zOffset = -0.8f * (static_cast<float>(localTimer) / 6.0f); // ため
        } else if (localTimer <= 9) {
            float t = static_cast<float>(localTimer - 6) / 3.0f;
            zOffset = -0.8f + (6.8f * t); // 一気に前へ突き出す
            if (cycle == 2) {
                zOffset += 2.0f; // 3段目だけ少し深く突く
            }
        } else {
            float t = static_cast<float>(localTimer - 9) / 5.0f;
            float maxZ = (cycle == 2) ? 8.0f : 6.0f;
            zOffset = maxZ - (maxZ * t); // 元の位置へ戻す
        }

        pos_ = {
            casterPos_.x + forwardVec.x * zOffset + rightVec.x * offsetX,
            casterPos_.y + 1.2f + offsetY,
            casterPos_.z + forwardVec.z * zOffset + rightVec.z * offsetX
        };

        obj_->SetRotation({ 1.5f, casterYaw_, 0.0f });
        obj_->SetTranslation(pos_);
        obj_->Update();

        if (localTimer >= 7 && localTimer <= 9) {
            for (auto& afterimage : afterimages_) {
                if (!afterimage.isActive) {
                    afterimage.isActive = true;
                    afterimage.lifeTimer = defaultAfterimageLife_;
                    CopyTransform(obj_, afterimage.object);
                    afterimage.object->Update();
                    break;
                }
            }
        }

        if (localTimer >= 7 && localTimer <= 9) {
            int windCount = 8;
            for (int i = 0; i < windCount; i++) {
                Vector3 startPos = {
                    pos_.x + rightVec.x * ((rand() % 11 - 5) * 0.05f) - forwardVec.x * 0.5f,
                    pos_.y + ((rand() % 11 - 5) * 0.05f),
                    pos_.z + rightVec.z * ((rand() % 11 - 5) * 0.05f) - forwardVec.z * 0.5f
                };

                float speed = 3.0f + (rand() % 10) * 0.2f;
                Vector3 windVel = forwardVec * speed;
                Vector4 windColor = { 0.7f, 0.2f, 1.0f, 0.5f }; // ボス用の紫軌跡

                GPUParticleManager::GetInstance()->Emit(startPos, windVel, 0.1f, 0.5f, windColor);
            }
        }

        if (cycle == 2 && localTimer == 8) {
            int burstCount = 40;
            for (int i = 0; i < burstCount; i++) {
                float spreadX = (rand() % 21 - 10) * 0.04f;
                float spreadY = (rand() % 21 - 10) * 0.04f;

                Vector3 dir = {
                    forwardVec.x * 1.5f + rightVec.x * spreadX,
                    spreadY,
                    forwardVec.z * 1.5f + rightVec.z * spreadX
                };

                float speed = 1.5f + (rand() % 10) * 0.2f;
                Vector4 windColor = { 0.8f, 0.3f, 1.0f, 0.7f }; // フィニッシュだけ少し明るい紫
                GPUParticleManager::GetInstance()->Emit(pos_, dir * speed, 0.2f, 0.8f, windColor);
            }

            GPUParticleManager::GetInstance()->Emit(
                pos_,
                forwardVec * 1.0f,
                0.1f,
                2.0f,
                { 0.9f, 0.5f, 1.0f, 0.8f }
            );
        }
    }

    for (auto& afterimage : afterimages_) {
        if (afterimage.isActive) {
            afterimage.lifeTimer--;
            float alphaRatio = static_cast<float>(afterimage.lifeTimer) / static_cast<float>(defaultAfterimageLife_);
            Vector4 afterimageColor = { 0.7f, 0.2f, 1.0f, alphaRatio * 0.4f }; // 残像も紫
            afterimage.object->SetColor(afterimageColor);
            afterimage.object->Update();

            if (afterimage.lifeTimer <= 0) {
                afterimage.isActive = false;
            }
        }
    }

    // 3連突きの有効フレームだけプレイヤーに当たり判定を出す
    if (localTimer >= 7 && localTimer <= 11) {
        int currentDamage = damage_ + (rand() % 2) + (cycle == 2 ? 1 : 0);

        if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
            currentDamage /= 2; // 発動元ボスが攻撃デバフ中なら半減
        }

        if (player && !player->IsDead()) {
            auto it = std::find(hitTargets_.begin(), hitTargets_.end(), player);
            if (it == hitTargets_.end()) {
                Vector3 playerPos = player->GetPosition();
                Vector3 diff = { playerPos.x - pos_.x, 0.0f, playerPos.z - pos_.z };

                if (Length(diff) < 1.8f) { // 槍の先端は細い：プレイヤー体幹に合わせた判定
                    player->TakeDamage(currentDamage, pos_);
                    hitTargets_.push_back(player);
                }
            }
        }
    }
}

void BossSpearEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }

    for (auto& afterimage : afterimages_) {
        if (afterimage.isActive && afterimage.object) {
            afterimage.object->Draw();
        }
    }
}

void BossSpearEffect::CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj) {
    if (!sourceObj || !destinationObj) {
        return;
    }

    destinationObj->SetTranslation(sourceObj->GetTranslation());
    destinationObj->SetRotation(sourceObj->GetRotation());
    destinationObj->SetScale(sourceObj->GetScale());
}