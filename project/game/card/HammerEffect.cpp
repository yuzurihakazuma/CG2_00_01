#include "HammerEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void HammerEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hasHit_ = false;
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;

    obj_ = Obj3d::Create("sword_model");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);

        // 原点フラッシュ（1フレーム目のチラつき）防止！
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model *model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material *mat = model->GetMaterial();
            mat->emissive = 1.0f;
        }
    }
}

void HammerEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    if (isFinished_) return;
    timer_++;

    if (obj_) {
        // 20フレームかけて振り下ろす（剣より少し重め・遅め）
        float progress = static_cast<float>(timer_) / 20.0f;
        if (progress > 1.0f) progress = 1.0f;

        // ==========================================
        // ★ 縦のスイング計算（Pitch角）
        // -0.5f(後ろに振りかぶった状態) から 1.6f(地面に叩きつけた状態) へ変化
        // ==========================================
        float swingPitch = -0.5f + (progress * 2.1f);

        float radius = 1.5f; // 柄の長さ
        float shoulderHeight = 1.2f; // 肩の高さ

        // 縦の円運動を計算
        // sin で「前後の距離」、cos で「高さ」を計算します
        float forwardDist = std::sinf(swingPitch) * radius;
        float height = std::cosf(swingPitch) * radius;

        // プレイヤーの向いている方向(Yaw)に合わせてワールド座標に変換
        pos_ = {
            casterPos_.x + std::sinf(casterYaw_) * forwardDist,
            casterPos_.y + shoulderHeight + height,
            casterPos_.z + std::cosf(casterYaw_) * forwardDist
        };

        // ==========================================
        // ★ ハンマーの回転
        // ==========================================
        float tiltX = swingPitch;  // X軸で振り下ろす
        float rotY = casterYaw_;   // プレイヤーと同じ方向を向く
        float rotZ = 0.0f;         // Z軸はいじらない

        // ※モデルの向きがおかしい場合は、tiltX, rotY, rotZ に 1.57f などを足して微調整してください
        obj_->SetRotation({ tiltX, rotY, rotZ });
        obj_->SetTranslation(pos_);
        obj_->Update();

        // 振り下ろしている最中の軽い軌跡
        if (timer_ % 3 == 0 && timer_ < 15) {
            GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.05f, 1.0f, { 1.0f, 0.8f, 0.5f, 0.2f });
        }
    }

    // ==========================================
    // ★ 当たり判定（地面に叩きつけた瞬間 ＝ 14フレーム目付近）
    // ==========================================
    if (timer_ >= 14 && !hasHit_) {

        int randomDamage = damage_;

        if (isPlayerCaster_) {
            // ド派手な地面ヒットエフェクト（衝撃波と土煙）
            for (int i = 0; i < 20; i++) {
                // 水平方向に散らばる火花
                Vector3 sparkVel = { (rand() % 11 - 5) * 0.8f, (rand() % 5) * 0.5f, (rand() % 11 - 5) * 0.8f };
                GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.1f, 0.5f, { 1.0f, 0.8f, 0.4f, 1.0f });
            }

            // 敵への判定（ハンマーは衝撃波があるので半径(4.0f)を広くする！）
            if (enemyManager) {
                for (auto &enemy : enemyManager->GetEnemies()) {
                    if (!enemy || enemy->IsDead()) continue;
                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    if (Length(diff) < 4.0f) {
                        enemy->TakeDamage(randomDamage);

                        // 物理的な衝撃で「スタン」させる！
                        enemy->SetStun(120);

                        
                    }
                }
            }
            // ボスへの判定
            if (boss && !boss->IsDead()) {
                Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if (Length(diff) < 5.0f) {
                    boss->TakeDamage(randomDamage);
                    boss->SetStun(30);
                }
            }
        } else {
            // ==========================================
            // ★ 追加：敵が放った時（プレイヤーへの判定）
            // ==========================================
            if (player && !player->IsDead()) {
                Vector3 pPos = player->GetPosition();
                Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };

                // プレイヤーへの判定（少し避けやすいように 3.5f 程度に設定）
                if (Length(diff) < 3.5f) {
                    // プレイヤーにダメージを与える（pos_ はノックバック方向の計算用）
                    player->TakeDamage(randomDamage, pos_);

                    // もしプレイヤーもスタンさせたい場合はここに追記（Playerクラスに実装が必要）
                    player->SetStun(60); 
                }
            }
        }

        hasHit_ = true; // 叩きつけ判定は1回だけ！
    } 

    // 少し余韻を残して25フレームで終了
    if (timer_ >= 25) {
        isFinished_ = true;
    }
}

void HammerEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}