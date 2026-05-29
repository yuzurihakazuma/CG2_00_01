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
    (void)isPlayerCaster;

    isFinished_ = false;
    timer_ = 0;
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;
    casterBoss_ = casterBoss;

    // 左・中央・右の角度オフセット
    const float angles[3] = { -kSpreadAngle, 0.0f, kSpreadAngle };

    for (int i = 0; i < 3; i++) {
        spears_[i].yaw = casterYaw_ + angles[i];
        spears_[i].hitDone = false;
        spears_[i].pos = casterPos_;

        spears_[i].obj = std::unique_ptr<Obj3d>(Obj3d::Create("spear_model"));
        if (spears_[i].obj) {
            spears_[i].obj->SetCamera(camera);
            spears_[i].obj->SetScale(scale_);
            spears_[i].obj->SetTranslation({ 0.0f, -1000.0f, 0.0f });
            spears_[i].obj->Update();

            Model* model = spears_[i].obj->GetModel();
            if (model && model->GetMaterial()) {
                Model::Material* mat = model->GetMaterial();
                mat->color = { 0.7f, 0.2f, 1.0f, 1.0f };
                mat->emissive = 2.8f;
            }
        }
    }
}

void BossSpearEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    (void)enemyManager;
    (void)boss;
    (void)extraBoss;
    (void)bossPos;
    (void)level;

    if (isFinished_) return;

    timer_++;

    // フェーズ境界
    const int t0 = kPhaseDeploy;
    const int t1 = t0 + kPhaseThrust;
    const int t2 = t1 + kPhaseRetract;
    const int t3 = t2 + kPhaseFinish;
    const int t4 = t3 + kPhaseFinishR;

    if (timer_ > t4) {
        isFinished_ = true;
        return;
    }

    for (int i = 0; i < 3; i++) {
        if (!spears_[i].obj) continue;

        Vector3 forward = { std::sinf(spears_[i].yaw), 0.0f, std::cosf(spears_[i].yaw) };

        // 横オフセット：左=-1, 中=0, 右=+1
        Vector3 right = { std::cosf(casterYaw_), 0.0f, -std::sinf(casterYaw_) };
        float sideOffset = (i - 1) * 0.8f; // -0.8, 0, +0.8

        float zOffset = 0.0f;

        if (timer_ <= t0) {
            // フェーズ0：扇形に展開して浮かぶ（引きながら浮く）
            float t = static_cast<float>(timer_) / static_cast<float>(kPhaseDeploy);
            zOffset = -0.5f - t * 0.5f; // じわっと後ろに引く
        } else if (timer_ <= t1) {
            // フェーズ1：一斉突き
            float t = static_cast<float>(timer_ - t0) / static_cast<float>(kPhaseThrust);
            zOffset = -1.0f + t * 7.0f; // -1 → +6
        } else if (timer_ <= t2) {
            // フェーズ2：引き戻し
            float t = static_cast<float>(timer_ - t1) / static_cast<float>(kPhaseRetract);
            zOffset = 6.0f * (1.0f - t);
        } else if (timer_ <= t3) {
            // フェーズ3：中央のみフィニッシュ追撃、左右は消える
            if (i != 1) {
                spears_[i].obj->SetTranslation({ 0.0f, -1000.0f, 0.0f });
                spears_[i].obj->Update();
                continue;
            }
            float t = static_cast<float>(timer_ - t2) / static_cast<float>(kPhaseFinish);
            zOffset = -0.5f + t * 9.0f; // より深く突く
        } else {
            // フェーズ4：フィニッシュ戻し（中央のみ）
            if (i != 1) {
                spears_[i].obj->SetTranslation({ 0.0f, -1000.0f, 0.0f });
                spears_[i].obj->Update();
                continue;
            }
            float t = static_cast<float>(timer_ - t3) / static_cast<float>(kPhaseFinishR);
            zOffset = 8.5f * (1.0f - t);
        }

        spears_[i].pos = {
            casterPos_.x + forward.x * zOffset + right.x * sideOffset,
            casterPos_.y + 1.2f,
            casterPos_.z + forward.z * zOffset + right.z * sideOffset
        };

        spears_[i].obj->SetRotation({ 1.5f, spears_[i].yaw, 0.0f });
        spears_[i].obj->SetTranslation(spears_[i].pos);
        spears_[i].obj->Update();

        // 突き出しフレームにパーティクル
        if (timer_ > t0 && timer_ <= t1) {
            for (int p = 0; p < 6; p++) {
                Vector3 pv = forward * (3.0f + (rand() % 5) * 0.2f);
                GPUParticleManager::GetInstance()->Emit(spears_[i].pos, pv, 0.1f, 0.4f, { 0.7f, 0.2f, 1.0f, 0.5f });
            }
        }

        // フィニッシュのバースト
        if (i == 1 && timer_ == t2 + kPhaseFinish / 2) {
            for (int p = 0; p < 40; p++) {
                float spreadX = (rand() % 21 - 10) * 0.04f;
                float spreadY = (rand() % 21 - 10) * 0.04f;
                Vector3 dir = { forward.x + spreadX, spreadY, forward.z + spreadX };
                float sp = 1.5f + (rand() % 10) * 0.2f;
                GPUParticleManager::GetInstance()->Emit(spears_[i].pos, dir * sp, 0.2f, 0.8f, { 0.9f, 0.5f, 1.0f, 0.7f });
            }
        }
    }

    // 当たり判定：一斉突き（左右含む全3本）
    if (timer_ > t0 && timer_ <= t1) {
        for (int i = 0; i < 3; i++) {
            if (spears_[i].hitDone) continue;
            if (!player || player->IsDead()) continue;

            Vector3 diff = { player->GetPosition().x - spears_[i].pos.x, 0.0f, player->GetPosition().z - spears_[i].pos.z };
            if (Length(diff) < 1.8f) {
                int dmg = damage_ + (rand() % 2);
                if (casterBoss_ && casterBoss_->IsAttackDebuffed()) dmg /= 2;
                if (dmg < 1) dmg = 1;
                player->TakeDamage(dmg, spears_[i].pos);
                spears_[i].hitDone = true;
            }
        }
    }

    // 当たり判定：フィニッシュ（中央のみ、+1ダメージ）
    if (timer_ > t2 && timer_ <= t3) {
        if (!spears_[1].hitDone && player && !player->IsDead()) {
            Vector3 diff = { player->GetPosition().x - spears_[1].pos.x, 0.0f, player->GetPosition().z - spears_[1].pos.z };
            if (Length(diff) < 1.8f) {
                int dmg = damage_ + (rand() % 2) + 1;
                if (casterBoss_ && casterBoss_->IsAttackDebuffed()) dmg /= 2;
                if (dmg < 1) dmg = 1;
                player->TakeDamage(dmg, spears_[1].pos);
                spears_[1].hitDone = true;
            }
        }
    }
}

void BossSpearEffect::Draw() {
    if (isFinished_) return;

    for (int i = 0; i < 3; i++) {
        if (spears_[i].obj) {
            spears_[i].obj->Draw();
        }
    }
}
