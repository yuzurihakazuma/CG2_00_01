#include "BossSummonEffect.h"
#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "engine/particle/GPUParticleManager.h"

void BossSummonEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera) {

    isFinished_ = false;
    timer_ = 60;

    // ボスの足元にエフェクトを出す
    pos_ = casterPos;
    pos_.y -= 2.0f; // 地面に埋まらないように少し浮かせる

    obj_ = Obj3d::Create("sphere");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetTranslation(pos_);
        //  最初から最大サイズで固定する
        obj_->SetScale({ 4.0f, 0.1f, 4.0f });

        Model* model = obj_->GetModel();
        if (model) {
            model->SetTexture("resources/white1x1.png");
            Model::Material* material = model->GetMaterial();
            if (material) {
                //  最初は完全に透明（Alpha=0.0f）にしておく
                material->color = { 1.0f, 0.0f, 0.0f, 0.0f };
                material->emissive = 0.0f;
            }
        }
        obj_->Update();
    }
}

void BossSummonEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level) {
    if (isFinished_) {
        return;
    }

    timer_--;

    if (obj_) {
        // 🌟 1. 魔法陣が「フワァ…」と浮かび上がる（フェードイン）演出
        float progress = static_cast<float>(60 - timer_) / 60.0f; // 0.0 〜 1.0

        Model* model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material* material = model->GetMaterial();
            // 時間経過で透明度(Alpha)と発光(Emissive)を上げていく
            material->color.w = progress * 0.5f; // 最大で半透明(0.5f)まで
            material->emissive = progress * 3.0f; // 最大で3.0fまで光る
        }

        // 回転させる
        Vector3 rot = obj_->GetRotation();
        rot.y += 0.1f; // 回転速度を少し抑えめに
        obj_->SetRotation(rot);
        obj_->Update();

        //  2. 魔法陣のフチから吹き出す激しいオーラ
        // 発生量を時間経過（progress）に合わせて徐々に増やす
        int emitCount = static_cast<int>(progress * 8.0f);
        for (int i = 0; i < emitCount; i++) {
            float angle = static_cast<float>(rand()) / RAND_MAX * 3.141592f * 2.0f;
            //  魔法陣の固定サイズ(4.0f)に合わせて、フチ(3.8f付近)から発生させる
            float radius = 3.8f;

            Vector3 emitPos = {
                pos_.x + std::sinf(angle) * radius,
                pos_.y + 0.2f,
                pos_.z + std::cosf(angle) * radius
            };

            // 上に向かって立ち昇るオーラ
            Vector3 emitVel = { 0.0f, 0.1f + static_cast<float>(rand() % 5) * 0.02f, 0.0f };
            // パーティクルも時間経過で徐々に濃く(Alpha値を高く)する
            Vector4 auraColor = { 1.0f, 0.1f, 0.1f, progress * 0.8f };

            GPUParticleManager::GetInstance()->Emit(emitPos, emitVel, 1.0f, 0.1f, auraColor);
        }
    }

    //  3. 召喚完了の瞬間（timerが0になった時）に大爆発エフェクト！
    if (timer_ <= 0) {
        for (int i = 0; i < 100; i++) {
            Vector3 sparkVel = {
                (rand() % 11 - 5) * 0.3f,
                (rand() % 11 - 5) * 0.3f + 0.2f, // 上方向へ散らす
                (rand() % 11 - 5) * 0.3f
            };
            GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.5f, 0.15f, { 1.0f, 0.2f, 0.0f, 1.0f });
        }

        if (boss && !boss->IsDead()) {
            boss->RequestSummon(spawnCount_);
        }
        isFinished_ = true;
    }
}

void BossSummonEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}