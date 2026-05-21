#include "RuinBeamEffect.h"

#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

namespace {

float NormalizeAngle(float angle) {
    constexpr float pi = 3.14159265f;
    constexpr float twoPi = pi * 2.0f;

    while (angle > pi) {
        angle -= twoPi;
    }
    while (angle < -pi) {
        angle += twoPi;
    }
    return angle;
}

}

void RuinBeamEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {

    (void)isPlayerCaster;

    isFinished_ = false;
    hitTimer_ = 0;
    lifeTimer_ = kLifeDuration;
    // このビームを発動したボス本人を保持する
// 分裂ボス時でも、発動元のデバフ状態を正しく参照できるようにする
    casterBoss_ = casterBoss;

    originPos_ = casterPos;
    rot_ = { 0.0f, casterYaw, 0.0f };

    Vector3 forward = {
        std::sinf(rot_.y),
        0.0f,
        std::cosf(rot_.y)
    };

    // 発射直後から前方を大きく塞ぐように、中心を少し前へ置く
    pos_ = {
        casterPos.x + forward.x * (baseScale_.z * 0.90f),
        casterPos.y - 0.8f,
        casterPos.z + forward.z * (baseScale_.z * 0.90f)
    };
    obj_ = Obj3d::Create("sphere");
    if (obj_) {
        obj_->SetCamera(camera);
        obj_->SetRotation(rot_);
        obj_->SetTranslation(pos_);
        obj_->SetScale(baseScale_);

        Model *model = obj_->GetModel();
        if (model) {
            // テクスチャを白紙に変更
            model->SetTexture("resources/white1x1.png");

            Model::Material *material = model->GetMaterial();
            if (material) {
                // ビームの色と透明度を設定 { R, G, B, A }
                // 禍々しい赤紫色（A=0.4f で少し透けるように設定）
                material->color = { 1.0f, 0.0f, 0.5f, 0.4f };

                // 発光（Emissive）を強くして、中央が白く光るようにする
                material->emissive = 3.0f;
            }
        }
        obj_->Update();
    }
}

void RuinBeamEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
    (void)enemyManager;
    (void)bossPos;
    (void)level;

    if (isFinished_) {
        return;
    }

    if (player && !player->IsDead()) {
        Vector3 toPlayer = {
            player->GetPosition().x - originPos_.x,
            0.0f,
            player->GetPosition().z - originPos_.z
        };
        if (Length(toPlayer) > 0.01f) {
            const float targetYaw = std::atan2f(toPlayer.x, toPlayer.z);
            const float yawDiff = std::clamp(
                NormalizeAngle(targetYaw - rot_.y),
                -kBeamTurnSpeed,
                kBeamTurnSpeed
            );
            rot_.y += yawDiff;
        }
    }

    Vector3 forward = { std::sinf(rot_.y), 0.0f, std::cosf(rot_.y) };
    pos_ = {
        originPos_.x + forward.x * (baseScale_.z * 0.90f),
        originPos_.y - 0.8f,
        originPos_.z + forward.z * (baseScale_.z * 0.90f)
    };
    const float hitHalfLength = GetHitHalfLength();
    Vector3 startPos = {
        pos_.x - forward.x * hitHalfLength,
        pos_.y,
        pos_.z - forward.z * hitHalfLength
    };
    Vector3 endPos = {
        pos_.x + forward.x * hitHalfLength,
        pos_.y,
        pos_.z + forward.z * hitHalfLength
    };

    if (lifeTimer_ > 0) {
        for (int i = 0; i < 20; ++i) { // 量を増やす
            Vector3 corePos = pos_;
            corePos.x += (rand() % 21 - 10) * 0.1f;
            corePos.y += (rand() % 21 - 10) * 0.1f;
            corePos.z += (rand() % 21 - 10) * 0.1f;

            // ビームの進行方向に沿って噴射する速度
            Vector3 coreVel = {
                forward.x * (0.2f + (rand() % 10) * 0.05f) + (rand() % 11 - 5) * 0.05f,
                (rand() % 11 - 5) * 0.05f,
                forward.z * (0.2f + (rand() % 10) * 0.05f) + (rand() % 11 - 5) * 0.05f
            };
            // 眩しい白とピンク
            Vector4 color = (rand() % 2 == 0) ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 0.4f, 1.0f, 1.0f };
            GPUParticleManager::GetInstance()->Emit(corePos, coreVel, 0.3f, 0.7f, color);
        }

        // 2. ビームの本体（Core）を流れるエネルギーの奔流（★大量・高速）
        // ビームの線上に沿って、200箇所にパーティクルを発生させる。
        for (int i = 0; i < 200; ++i) {
            float t = static_cast<float>(i) / 199.0f;
            Vector3 particlePos = {
                startPos.x + (endPos.x - startPos.x) * t,
                startPos.y + (endPos.y - startPos.y) * t,
                startPos.z + (endPos.z - startPos.z) * t
            };

            // ビームの中心軸に固まらせるオフセット
            particlePos.x += (rand() % 11 - 5) * 0.1f;
            particlePos.y += (rand() % 11 - 5) * 0.1f;
            particlePos.z += (rand() % 11 - 5) * 0.1f;

            // 速度：始点から終点へ向かって超高速で流す（0.8f〜1.2f）
            Vector3 particleVel = {
                forward.x * (0.8f + (rand() % 5) * 0.1f) + (rand() % 11 - 5) * 0.02f,
                (rand() % 11 - 5) * 0.02f,
                forward.z * (0.8f + (rand() % 5) * 0.1f) + (rand() % 11 - 5) * 0.02f
            };

            // 色：中心を白（眩しさ）、周囲をピンク。
            Vector4 color = (rand() % 10 < 3) ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 0.2f, 0.8f, 1.0f };

            // サイズ：眩しく、一本の「芯」に見えるようにする。
            float scale = 0.4f + (rand() % 4) * 0.1f;

            // 寿命を少し長く（0.5秒）して、流れる線を綺麗に作る。
            GPUParticleManager::GetInstance()->Emit(particlePos, particleVel, 0.5f, scale, color);
        }

        // 3. ビームから漏れる「放電」と「プラズマ
        for (int i = 0; i < 50; ++i) {
            float t = static_cast<float>(rand()) / RAND_MAX; // 線上のランダムな位置
            Vector3 particlePos = {
                startPos.x + (endPos.x - startPos.x) * t,
                startPos.y + (endPos.y - startPos.y) * t,
                startPos.z + (endPos.z - startPos.z) * t
            };

            // ビームの中心軸から、外側に向かって不規則に散らす
            particlePos.x += (rand() % 31 - 15) * 0.15f; // 少し広めに
            particlePos.y += (rand() % 31 - 15) * 0.15f;
            particlePos.z += (rand() % 31 - 15) * 0.15f;

            // 速度：ビームの進行方向＋外側へ散らす
            Vector3 particleVel = {
                forward.x * 0.3f + (rand() % 21 - 10) * 0.1f,
                (rand() % 21 - 10) * 0.1f,
                forward.z * 0.3f + (rand() % 21 - 10) * 0.1f
            };

            // 色：青紫色。ピンクと白の中に混ぜる。
            Vector4 color = { 0.6f, 0.0f, 1.0f, 1.0f };

            // サイズ：不規則で雷のように見せる。
            float scale = 0.5f + (rand() % 6) * 0.1f;

            // 寿命を短く（0.3秒）して、常にバチバチしているように見せる。
            GPUParticleManager::GetInstance()->Emit(particlePos, particleVel, 0.3f, scale, color);
        }

        // 4. 先端（着弾点）の「空間破壊」エネルギー球（★巨大・その場に留まる）
        // ダサい飛び散るスパークは最小限にし、代わりにその場に留まる巨大な渦にする。
        for (int i = 0; i < 30; ++i) {
            Vector3 expPos = endPos;
            expPos.x += (rand() % 31 - 15) * 0.1f; // 少し広めに
            expPos.y += (rand() % 31 - 15) * 0.1f;
            expPos.z += (rand() % 31 - 15) * 0.1f;

            // 速度：その場に留まるか、渦を巻くようにする。
            Vector3 expVel = {
                (rand() % 11 - 5) * 0.05f,
                (rand() % 11 - 5) * 0.05f,
                (rand() % 11 - 5) * 0.05f
            };
            // 眩しい白とピンク。
            Vector4 color = (rand() % 2 == 0) ? Vector4{ 1.0f, 1.0f, 1.0f, 1.0f } : Vector4{ 1.0f, 0.4f, 1.0f, 1.0f };
            // サイズを巨大に！ (1.0f 〜 2.0f)
            float scale = 1.0f + (rand() % 11) * 0.1f;
            // 寿命を長くして（1.0秒）残像を作る。
            GPUParticleManager::GetInstance()->Emit(expPos, expVel, 1.0f, scale, color);
        }
    }
    if (hitTimer_ > 0) {
        hitTimer_--;
    }

    if (obj_) {
        float t = 1.0f - (static_cast<float>(lifeTimer_) / static_cast<float>(kLifeDuration));
        float pulse = 1.0f + std::sinf(t * 12.56636f) * 0.12f;

        // 出始めは細く、途中で一気に広がる
        float grow = std::clamp(t * 1.8f, 0.0f, 1.0f);
        float width = (0.35f + grow * 1.10f) * pulse;
        float height = 0.45f + grow * 0.75f;

        obj_->SetScale({
            baseScale_.x * width,
            baseScale_.y * height,
            baseScale_.z
        });
        obj_->SetRotation(rot_);
        obj_->SetTranslation(pos_);
        obj_->Update();
        Model* model = obj_->GetModel();
        if (model && model->GetMaterial()) {
            Model::Material* material = model->GetMaterial();
            // grow と pulse に合わせた透明度アニメーション
            material->color.w = grow * pulse * 0.6f;
        }
        obj_->Update();
    }

    if (player && !player->IsDead() && hitTimer_ <= 0) {
        if (IsPlayerInsideBeam(player->GetPosition())) {
            int finalDamage = (std::max)(1, damage_ / 3);

            // 発動元のボスが攻撃デバフ中ならダメージを半減する
            // 分裂ボス時に boss 引数へ依存すると、別個体を参照する可能性がある
            if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
                finalDamage /= 2;
            }

            const int hpBefore = player->GetHP();
            player->TakeContinuousDamage(finalDamage);
            if (player->GetHP() < hpBefore) {
                hitTimer_ = hitInterval_;
            }
        }
    }

    lifeTimer_--;
    if (lifeTimer_ <= 0) {
        isFinished_ = true;
    }
}

void RuinBeamEffect::Draw() {
    if (!isFinished_ && obj_) {
        obj_->Draw();
    }
}

bool RuinBeamEffect::IsPlayerInsideBeam(const Vector3& playerPos) const {
    Vector3 diff = {
        playerPos.x - pos_.x,
        0.0f,
        playerPos.z - pos_.z
    };

    float sinY = std::sinf(rot_.y);
    float cosY = std::cosf(rot_.y);

    float localX = diff.x * cosY - diff.z * sinY;
    float localZ = diff.x * sinY + diff.z * cosY;

    const float halfWidth = GetDebugHalfWidth();
    const float halfLength = GetDebugHalfLength();

    return std::fabs(localX) <= halfWidth && std::fabs(localZ) <= halfLength;
}
