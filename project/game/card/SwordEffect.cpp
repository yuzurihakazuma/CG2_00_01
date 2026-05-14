#include "SwordEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void SwordEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
    // この効果は発動元ボスを使わない
    (void)casterBoss;
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hasHit_ = false;
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;

    // 剣のモデル（※読み込んでいる剣のモデル名に合わせてください）
    // もしまだモデルが無ければ、仮で "cube" 等にしてスケールを細長くしてもOKです
    obj_ = Obj3d::Create("sword_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f }); 
        obj_->Update();

        Model *model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material *mat = model->GetMaterial();
            mat->emissive = 1.5f;
        }
    }
}

void SwordEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, const Vector3 &bossPos, const LevelData &level) {
    if (isFinished_) return;

    timer_++;

    if (obj_) {
        // 右から左へ大きく振り抜くアニメーション（15フレーム）
        float progress = static_cast<float>(timer_) / 15.0f;
        if (progress > 1.0f) progress = 1.0f;

        // プレイヤーの向いている方向(casterYaw_)を基準にスイング
        float slashYaw = -1.2f + (progress * 2.4f);
        float currentYaw = casterYaw_ + slashYaw;

        // ==========================================
        // ★ 修正1：斧の「柄」をプレイヤーの手元に固定する！
        // この数値を斧の長さの半分くらいにすると、柄が手元にピタッと固定されます。
        // （長すぎる場合は 0.8f くらいに減らしてください）
        // ==========================================
        float radius = 1.2f;

        // プレイヤーの位置を支点にして、刃が外を回るように計算
        pos_ = {
            casterPos_.x + std::sinf(currentYaw) * radius,
            casterPos_.y + 1.2f, // 手の高さ
            casterPos_.z + std::cosf(currentYaw) * radius
        };

        // ==========================================
        // ★ 修正2：360度対応の回転（ジンバルロック対策）
        // Z軸で寝かせるとバグるので、必ず「X軸」で斧を前に倒します！
        // ==========================================
        float tiltX = 1.57f;  // X軸で前に倒す（柄が外を向く）
        float offsetY = 0.0f; // 刃の向きの微調整（※後述）
        float tiltZ = 0.0f;   // Z軸は絶対に「0.0f」に固定！

        // 回転と座標を適用
        obj_->SetRotation({ tiltX, currentYaw + offsetY, tiltZ });
        obj_->SetTranslation(pos_);
        obj_->Update();

        // 斬撃の軌跡エフェクト
       
            GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.1f, 1.2f, { 0.4f, 0.4f, 0.4f, 0.1f });
        
    }

    // 当たり判定 (振り抜き中のフレームで判定)
    bool isAttacking = (timer_ >= 3 && timer_ <= 12);
    if (isAttacking && !hasHit_) {

        if (isPlayerCaster_) {
            // ① プレイヤーの攻撃（ImGuiの数値を使用）
            int randomDamage = damage_ + (rand() % 2);

            // 敵への判定（剣は複数の敵を巻き込めるように hasHit_ で break しない！）
            if (enemyManager) {
                for (auto &enemy : enemyManager->GetEnemies()) {
                    if (!enemy || enemy->IsDead()) continue;
                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    if (Length(diff) < 3.0f) { // 剣は範囲が広い
                        enemy->TakeDamage(randomDamage);

                        // ヒット時の火花
                        for (int i = 0; i < 10; i++) {
                            Vector3 sparkVel = { (rand() % 11 - 5) * 0.5f, (rand() % 11 - 5) * 0.5f, (rand() % 11 - 5) * 0.5f };
                            GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.2f, 0.2f, { 1.0f, 1.0f, 0.8f, 1.0f });
                        }
                    }
                }
            }

            // ボスへの判定
            if (boss && !boss->IsDead()) {
                Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if (Length(diff) < 4.0f) {
                    boss->TakeDamage(randomDamage);
                    hasHit_ = true; // ボスに当たったら複数ヒットを防ぐ
                }
            }
        } else {
            // ② 敵の攻撃（プレイヤーへの判定。元の damage_ を使用）
            int enemyRandomDamage = damage_ + (rand() % 2);
            if (player && !player->IsDead()) {
                Vector3 pPos = player->GetPosition();
                Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
                if (Length(diff) < 2.5f) {
                    player->TakeDamage(enemyRandomDamage, pos_);
                    hasHit_ = true;
                }
            }
        }
    }

    // 演出終了
    if (timer_ >= 20) {
        isFinished_ = true;
    }
}

void SwordEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}
