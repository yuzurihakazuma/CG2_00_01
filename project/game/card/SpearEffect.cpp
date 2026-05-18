#include "SpearEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

void SpearEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hitTargets_.clear(); // ヒット履歴をリセット
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;

    // ※"spear_model" は適宜読み込んだ槍のモデル名に変更してください
    obj_ = Obj3d::Create("spear_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);

        // 原点フラッシュ防止
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model *model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material *mat = model->GetMaterial();
            mat->emissive = 1.0f;
        }
    }
}

void SpearEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    if (isFinished_) return;
    timer_++;

    // ==========================================
    // ★ 3連続突きの管理（10フレーム × 3回 ＝ 計30フレーム）
    // ==========================================
    int cycle = (timer_ - 1) / 10;      // 現在何回目の突きか（0, 1, 2）
    int localTimer = (timer_ - 1) % 10; // 1回の突きの中でのフレーム（0〜9）

    // 3回の突きが終わったら終了
    if (cycle >= 3) {
        isFinished_ = true;
        return;
    }

    if (obj_) {
        // ★新しい突きが始まった瞬間に、ヒット履歴をリセットして再度当たるようにする！
        if (localTimer == 0) {
            hitTargets_.clear();
        }

        // ==========================================
        // ★ 乱れ突き感を出すために、突く位置を少しズラす
        // ==========================================
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        if (cycle == 0) { offsetX = -0.4f; offsetY = 0.2f; }      // 1発目：左上
        else if (cycle == 1) { offsetX = 0.4f; offsetY = -0.2f; } // 2発目：右下
        else if (cycle == 2) { offsetX = 0.0f; offsetY = 0.0f; }  // 3発目：ど真ん中（フィニッシュ！）

        // ==========================================
        // ★ 前後（Z軸）の動き計算
        // ==========================================
        float zOffset = 0.0f;
        if (localTimer <= 3) {
            // 手前に引き絞る
            zOffset = -0.5f * (static_cast<float>(localTimer) / 3.0f);
        } else if (localTimer <= 6) {
            // 一気に突き出す
            float t = static_cast<float>(localTimer - 3) / 3.0f;
            zOffset = -0.5f + (6.0f * t);
            if (cycle == 2) zOffset += 1.5f; // 3発目はさらに深く刺さる！
        } else {
            // 戻す
            float t = static_cast<float>(localTimer - 6) / 3.0f;
            zOffset = 5.5f - (5.5f * t);
        }

        // プレイヤーの向いている方向(Yaw)から、前方向と右方向のベクトルを作る
        Vector3 forwardVec = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
        Vector3 rightVec = { std::cosf(casterYaw_), 0.0f, -std::sinf(casterYaw_) };

        // 最終的な槍の位置
        pos_ = {
            casterPos_.x + forwardVec.x * zOffset + rightVec.x * offsetX,
            casterPos_.y + 1.2f + offsetY,
            casterPos_.z + forwardVec.z * zOffset + rightVec.z * offsetX
        };

        // 槍を前に倒す (モデルによって tiltX は微調整してください)
        float tiltX = 1.57f;
        float rotY = casterYaw_;
        float rotZ = 0.0f;

        obj_->SetRotation({ tiltX, rotY, rotZ });
        obj_->SetTranslation(pos_);
        obj_->Update();

        // 突き出し時の風パーティクル（3発目は色を変えて派手に！）
        if (localTimer >= 4 && localTimer <= 7) {
            Vector3 windVel = { forwardVec.x * 0.8f, 0.0f, forwardVec.z * 0.8f };
            Vector4 pColor = (cycle == 2) ? Vector4{ 1.0f, 0.8f, 0.2f, 0.4f } : Vector4{ 0.8f, 1.0f, 1.0f, 0.3f };
            float pSize = (cycle == 2) ? 0.15f : 0.08f;
            GPUParticleManager::GetInstance()->Emit(pos_, windVel, pSize, 0.5f, pColor);
        }
    }

    // ==========================================
    // ★ 当たり判定（各突きの 4~7フレーム目）
    // ==========================================
    if (localTimer >= 4 && localTimer <= 7) {

        // 3発目だけダメージにボーナス（+1）をつける！
        int currentDamage = damage_ + (rand() % 2) + (cycle == 2 ? 1 : 0);

        if (isPlayerCaster_) {
            if (enemyManager) {
                for (auto &enemy : enemyManager->GetEnemies()) {
                    if (!enemy || enemy->IsDead()) continue;

                    // 今回の突きで既にヒットしていたら無視
                    auto it = std::find(hitTargets_.begin(), hitTargets_.end(), enemy.get());
                    if (it != hitTargets_.end()) continue;

                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    if (Length(diff) < 1.5f) {
                        enemy->TakeDamage(currentDamage);
                        hitTargets_.push_back(enemy.get()); // ヒット記録に追加

                        for (int i = 0; i < 5; i++) {
                            Vector3 sparkVel = { (rand() % 11 - 5) * 0.1f, (rand() % 11 - 5) * 0.1f, (rand() % 11 - 5) * 0.1f };
                            GPUParticleManager::GetInstance()->Emit(ePos, sparkVel, 0.15f, 0.5f, { 1.0f, 0.2f, 0.2f, 1.0f });
                        }
                    }
                }
            }
            if (boss && !boss->IsDead()) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), boss);
                if (it == hitTargets_.end()) {
                    Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                    if (Length(diff) < 2.5f) {
                        boss->TakeDamage(currentDamage);
                        hitTargets_.push_back(boss);
                    }
                }
            }
        } else {
            // 敵が使った場合
            if (player && !player->IsDead()) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), player);
                if (it == hitTargets_.end()) {
                    Vector3 pPos = player->GetPosition();
                    Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
                    if (Length(diff) < 1.5f) {
                        player->TakeDamage(currentDamage, pos_);
                        hitTargets_.push_back(player);
                    }
                }
            }
        }
    }
}

void SpearEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}