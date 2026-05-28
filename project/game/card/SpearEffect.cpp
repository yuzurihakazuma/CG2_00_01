#include "SpearEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/camera/Camera.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

namespace {
const Vector4 kEnemySpearColor = { 1.0f, 0.22f, 0.25f, 1.0f };
}

void SpearEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hitTargets_.clear();
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;
    camera_ = camera;

    obj_ = Obj3d::Create("spear_model");
    if ( obj_ ) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model* model = obj_->GetModel();
        if ( model && model->GetMaterial() ) {
            Model::Material* mat = model->GetMaterial();
            if ( !isPlayerCaster_ ) {
                model->SetTexture("resources/white1x1.png");
                mat->color = kEnemySpearColor;
            }
            mat->emissive = 2.0f;
        }
    }

    afterimages_.clear();
    for ( int index = 0; index < maxAfterimageCount_; index++ ) {
        AfterimageData afterimage;
        afterimage.object = Obj3d::Create("spear_model");
        if ( afterimage.object ) {
            afterimage.object->SetCamera(camera);
            afterimage.object->SetScale(scale_);
        }
        afterimage.isActive = false;
        afterimages_.push_back(std::move(afterimage));
    }
}

void SpearEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;
    timer_++;

    // 🌟 修正：1回の突きサイクルを「10フレーム」から「15フレーム」に延ばし、タメを作ります
    int cycle = ( timer_ - 1 ) / 15;
    int localTimer = ( timer_ - 1 ) % 15;

    if ( cycle >= 3 ) {
        isFinished_ = true;
        return;
    }

    Vector3 forwardVec = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
    Vector3 rightVec = { std::cosf(casterYaw_), 0.0f, -std::sinf(casterYaw_) };

    if ( obj_ ) {
        if ( localTimer == 0 ) { hitTargets_.clear(); }

        float offsetX = 0.0f; float offsetY = 0.0f;
        if ( cycle == 0 ) { offsetX = -0.4f; offsetY = 0.2f; } else if ( cycle == 1 ) { offsetX = 0.4f; offsetY = -0.2f; } else if ( cycle == 2 ) { offsetX = 0.0f; offsetY = 0.0f; }

        // ==========================================
        // 🌟 修正：発生を遅らせる（タメる）計算
        // ==========================================
        float zOffset = 0.0f;
        if ( localTimer <= 6 ) {
            // 0〜6フレーム：槍を手前にグッと引き絞る（タメ）
            zOffset = -0.8f * ( static_cast< float >( localTimer ) / 6.0f );
        } else if ( localTimer <= 9 ) {
            // 7〜9フレーム：一気に突き出す！（発生が遅くなった分、速度が上がります）
            float t = static_cast< float >( localTimer - 6 ) / 3.0f;
            zOffset = -0.8f + ( 7.8f * t );
            if ( cycle == 2 ) zOffset += 2.0f;
        } else {
            // 10〜14フレーム：元の位置にスッと戻る
            float t = static_cast< float >( localTimer - 9 ) / 5.0f;
            float maxZ = ( cycle == 2 ) ? 8.0f : 6.0f;
            zOffset = maxZ - ( maxZ * t );
        }

        pos_ = {
            casterPos_.x + forwardVec.x * zOffset + rightVec.x * offsetX,
            casterPos_.y + 1.2f + offsetY,
            casterPos_.z + forwardVec.z * zOffset + rightVec.z * offsetX
        };

        obj_->SetRotation({ 1.5f, casterYaw_, 0.0f });
        obj_->SetTranslation(pos_);
        obj_->Update();

        // タメ中（0〜6フレーム）：槍の穂先に向かって風が収束する
        if ( localTimer <= 6 ) {
            for ( int i = 0; i < 3; i++ ) {
                float angle = static_cast<float>(rand() % 628) * 0.01f;
                float r = 0.4f + static_cast<float>(rand() % 5) * 0.1f;
                Vector3 from = {
                    pos_.x + std::sinf(angle) * r,
                    pos_.y + static_cast<float>(rand() % 7 - 3) * 0.1f,
                    pos_.z + std::cosf(angle) * r
                };
                float dx = pos_.x - from.x, dy = pos_.y - from.y, dz = pos_.z - from.z;
                float d = std::sqrtf(dx*dx + dy*dy + dz*dz) + 0.001f;
                float sp = 4.0f;
                GPUParticleManager::GetInstance()->Emit(
                    from, {dx/d*sp, dy/d*sp, dz/d*sp}, d/sp + 0.03f, 0.12f,
                    isPlayerCaster_ ? Vector4{0.6f, 1.0f, 1.0f, 0.8f} : Vector4{1.0f, 0.2f, 0.25f, 0.8f});
            }
        }

        // 🌟 残像の発生タイミングも突きの瞬間に合わせる
        if ( localTimer >= 7 && localTimer <= 9 ) {
            for ( auto& afterimage : afterimages_ ) {
                if ( !afterimage.isActive ) {
                    afterimage.isActive = true;
                    afterimage.lifeTimer = defaultAfterimageLife_;
                    CopyTransform(obj_, afterimage.object);
                    afterimage.object->Update();
                    break;
                }
            }
        }

        // 🌟 風の刃エフェクトも発生を遅らせる
        if ( localTimer >= 7 && localTimer <= 9 ) {
            int windCount = 8;
            for ( int i = 0; i < windCount; i++ ) {
                Vector3 startPos = {
                    pos_.x + rightVec.x * ( ( rand() % 11 - 5 ) * 0.05f ) - forwardVec.x * 0.5f,
                    pos_.y + ( ( rand() % 11 - 5 ) * 0.05f ),
                    pos_.z + rightVec.z * ( ( rand() % 11 - 5 ) * 0.05f ) - forwardVec.z * 0.5f
                };

                float speed = 3.0f + ( rand() % 10 ) * 0.2f;
                Vector3 windVel = forwardVec * speed;
                Vector4 windColor = isPlayerCaster_
                    ? Vector4{ 0.8f, 1.0f, 1.0f, 0.5f }
                    : Vector4{ 1.0f, 0.18f, 0.22f, 0.5f };

                GPUParticleManager::GetInstance()->Emit(startPos, windVel, 0.1f, 0.5f, windColor);
            }
        }

        // 🌟 3段目のソニックブームエフェクト
        if ( cycle == 2 && localTimer == 8 ) {
            int burstCount = 40;
            for ( int i = 0; i < burstCount; i++ ) {
                float spreadX = ( rand() % 21 - 10 ) * 0.04f;
                float spreadY = ( rand() % 21 - 10 ) * 0.04f;

                Vector3 dir = {
                    forwardVec.x * 1.5f + rightVec.x * spreadX,
                    spreadY,
                    forwardVec.z * 1.5f + rightVec.z * spreadX
                };

                float speed = 1.5f + ( rand() % 10 ) * 0.2f;
                Vector4 windColor = isPlayerCaster_
                    ? Vector4{ 0.9f, 1.0f, 1.0f, 0.7f }
                    : Vector4{ 1.0f, 0.22f, 0.25f, 0.7f };
                GPUParticleManager::GetInstance()->Emit(pos_, dir * speed, 0.2f, 0.8f, windColor);
            }
            GPUParticleManager::GetInstance()->Emit(pos_, forwardVec * 1.0f, 0.1f, 2.0f, { 1.0f, 1.0f, 1.0f, 0.8f });
        }
    }

    for ( auto& afterimage : afterimages_ ) {
        if ( afterimage.isActive ) {
            afterimage.lifeTimer--;
            float alphaRatio = static_cast< float >( afterimage.lifeTimer ) / static_cast< float >( defaultAfterimageLife_ );
            Vector4 afterimageColor = isPlayerCaster_
                ? Vector4{ 0.3f, 0.8f, 1.0f, alphaRatio * 0.4f }
                : Vector4{ 1.0f, 0.2f, 0.25f, alphaRatio * 0.4f };
            afterimage.object->SetColor(afterimageColor);
            afterimage.object->Update();

            if ( afterimage.lifeTimer <= 0 ) {
                afterimage.isActive = false;
            }
        }
    }

    // 🌟 当たり判定も突きの発生（7フレーム目以降）に合わせる
    if ( localTimer >= 7 && localTimer <= 11 ) {

        int currentDamage = (cycle == 2)
            ? damage_ + (rand() % 2) + 1
            : 1 + (rand() % 2);

        // 3段目命中時の歪みエフェクト用ヘルパー
        auto TriggerSpearDistortion = [&](const Vector3& hitPos) {
            if ( cycle != 2 || !camera_ ) return;
            const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
            float cx = hitPos.x*vp.m[0][0] + hitPos.y*vp.m[1][0] + hitPos.z*vp.m[2][0] + vp.m[3][0];
            float cy = hitPos.x*vp.m[0][1] + hitPos.y*vp.m[1][1] + hitPos.z*vp.m[2][1] + vp.m[3][1];
            float cw = hitPos.x*vp.m[0][3] + hitPos.y*vp.m[1][3] + hitPos.z*vp.m[2][3] + vp.m[3][3];
            if ( std::abs(cw) > 0.0001f ) {
                PostEffect::GetInstance()->TriggerDistortion(
                    cx/cw * 0.5f + 0.5f, -cy/cw * 0.5f + 0.5f, 0.20f, 10);
            }
        };

        if ( isPlayerCaster_ ) {
            if ( enemyManager ) {
                for ( auto& enemy : enemyManager->GetEnemies() ) {
                    if ( !enemy || enemy->IsDead() ) continue;

                    auto it = std::find(hitTargets_.begin(), hitTargets_.end(), enemy.get());
                    if ( it != hitTargets_.end() ) continue;

                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    if ( Length(diff) < 2.0f ) {
                        enemy->TakeDamage(currentDamage);
                        hitTargets_.push_back(enemy.get());
                        TriggerSpearDistortion(ePos);

                        for ( int i = 0; i < 8; i++ ) {
                            Vector3 sparkDir = forwardVec * 1.5f + rightVec * ( ( rand() % 11 - 5 ) * 0.1f );
                            sparkDir.y += ( rand() % 11 - 5 ) * 0.1f;
                            Vector4 sparkColor = { 1.0f, 0.8f, 0.2f, 1.0f };
                            GPUParticleManager::GetInstance()->Emit(ePos, sparkDir, 0.15f, 0.4f, sparkColor);
                        }
                    }
                }
            }
            // 槍はキャスター→穂先の軸線に対する最短距離でチェック（見た目通りの判定）
            auto SpearShaftDist = [&](const Vector3& targetPos) -> float {
                float sx = pos_.x - casterPos_.x, sz = pos_.z - casterPos_.z;
                float lenSq = sx * sx + sz * sz;
                if ( lenSq < 0.0001f ) {
                    float dx = targetPos.x - casterPos_.x, dz = targetPos.z - casterPos_.z;
                    return std::sqrtf(dx * dx + dz * dz);
                }
                float bx = targetPos.x - casterPos_.x, bz = targetPos.z - casterPos_.z;
                float t = std::max(0.0f, std::min(1.0f, (bx * sx + bz * sz) / lenSq));
                float cx = casterPos_.x + sx * t - targetPos.x;
                float cz = casterPos_.z + sz * t - targetPos.z;
                return std::sqrtf(cx * cx + cz * cz);
            };

            if ( boss && !boss->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), boss);
                if ( it == hitTargets_.end() ) {
                    if ( SpearShaftDist(bossPos) < 1.8f ) {
                        boss->TakeDamage(currentDamage);
                        hitTargets_.push_back(boss);
                        TriggerSpearDistortion(bossPos);
                    }
                }
            }
            if ( extraBoss && !extraBoss->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), extraBoss);
                if ( it == hitTargets_.end() ) {
                    if ( SpearShaftDist(extraBoss->GetPosition()) < 1.8f ) {
                        extraBoss->TakeDamage(currentDamage);
                        hitTargets_.push_back(extraBoss);
                        TriggerSpearDistortion(extraBoss->GetPosition());
                    }
                }
            }
        } else {
            if ( player && !player->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), player);
                if ( it == hitTargets_.end() ) {
                    Vector3 pPos = player->GetPosition();
                    Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
                    if ( Length(diff) < 1.5f ) {
                        player->TakeDamage(currentDamage, pos_);
                        hitTargets_.push_back(player);
                    }
                }
            }
        }
    }
}

void SpearEffect::Draw(){
    if ( !isFinished_ && obj_ ) {
        obj_->Draw();
    }
    for ( auto& afterimage : afterimages_ ) {
        if ( afterimage.isActive && afterimage.object ) {
            afterimage.object->Draw();
        }
    }
}

void SpearEffect::CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj){
    if ( !sourceObj || !destinationObj ) return;
    destinationObj->SetTranslation(sourceObj->GetTranslation());
    destinationObj->SetRotation(sourceObj->GetRotation());
    destinationObj->SetScale(sourceObj->GetScale());
}
