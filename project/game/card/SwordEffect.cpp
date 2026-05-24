#include "SwordEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

// 🌟 修正：引数に Boss* casterBoss を追加
void SwordEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hitTargets_.clear();
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;

    obj_ = Obj3d::Create("sword_model");
    if ( obj_ ) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model* model = obj_->GetModel();
        if ( model && model->GetMaterial() ) {
            Model::Material* material = model->GetMaterial();
            material->emissive = 2.0f;
        }
    }

    afterimages_.clear();
    for ( int index = 0; index < maxAfterimageCount_; index++ ) {
        AfterimageData afterimage;
        afterimage.object = Obj3d::Create("sword_model");
        if ( afterimage.object ) {
            afterimage.object->SetCamera(camera);
            afterimage.object->SetScale(scale_);
        }
        afterimage.isActive = false;
        afterimages_.push_back(std::move(afterimage));
    }
}

// 🌟 修正：引数に Boss* extraBoss を追加
void SwordEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;

    timer_++;

    if ( isPlayerCaster_ && player ) {
        Vector3 playerPosition = player->GetPosition();

        if ( obj_ ) {
            float swingProgress = static_cast< float >( timer_ ) / 15.0f;
            if ( swingProgress > 1.0f ) swingProgress = 1.0f;

            float easeT = swingProgress < 0.5f ? 2.0f * swingProgress * swingProgress : 1.0f - std::powf(-2.0f * swingProgress + 2.0f, 2.0f) / 2.0f;

            float startYawOffset = -3.14159f;
            float endYawOffset = 3.14159f;
            float currentRotationYaw = casterYaw_ + startYawOffset + ( endYawOffset - startYawOffset ) * easeT;

            Vector3 pivotPos = {
                playerPosition.x,
                playerPosition.y + 1.2f,
                playerPosition.z
            };

            float swingRadius = 1.5f;

            pos_ = {
                pivotPos.x + std::sinf(currentRotationYaw) * swingRadius,
                pivotPos.y,
                pivotPos.z + std::cosf(currentRotationYaw) * swingRadius
            };

            float rotationPitchX = 1.57f;
            float rotationRollZ = 0.0f;

            obj_->SetRotation({ rotationPitchX, currentRotationYaw, rotationRollZ });
            obj_->SetTranslation(pos_);
            obj_->Update();

            Vector3 zeroVelocity = { 0.0f, 0.0f, 0.0f };
            Vector4 grayColor = { 0.4f, 0.4f, 0.4f, 0.1f };
            GPUParticleManager::GetInstance()->Emit(pos_, zeroVelocity, 0.1f, 1.2f, grayColor);

            // 振り始め1フレーム目のみ：風の衝撃波バースト
            if (timer_ == 1) {
                for (int i = 0; i < 12; i++) {
                    float angle = (3.14159f * 2.0f / 12.0f) * static_cast<float>(i);
                    Vector3 vel = { std::sinf(angle) * 0.6f, 0.05f, std::cosf(angle) * 0.6f };
                    GPUParticleManager::GetInstance()->Emit(pos_, vel, 0.2f, 0.25f, {0.5f, 0.9f, 1.0f, 0.8f});
                }
                GPUParticleManager::GetInstance()->Emit(pos_, {0.0f, 0.0f, 0.0f}, 0.1f, 1.8f, {0.3f, 0.8f, 1.0f, 0.9f});
            }

            if ( timer_ < 15 ) {
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
        }

        for ( auto& afterimage : afterimages_ ) {
            if ( afterimage.isActive ) {
                afterimage.lifeTimer--;
                float alphaRatio = static_cast< float >( afterimage.lifeTimer ) / static_cast< float >( defaultAfterimageLife_ );
                Vector4 afterimageColor = { 0.2f, 0.9f, 1.0f, alphaRatio * 0.4f };
                afterimage.object->SetColor(afterimageColor);
                afterimage.object->Update();

                if ( afterimage.lifeTimer <= 0 ) {
                    afterimage.isActive = false;
                }
            }
        }

        if ( timer_ < 15 ) {
            int particleTraceCount = 5;
            for ( int index = 0; index < particleTraceCount; index++ ) {
                Vector3 particlePosition = {
                    pos_.x + static_cast< float >(rand() % 11 - 5) * 0.08f,
                    pos_.y + static_cast< float >(rand() % 11 - 5) * 0.08f,
                    pos_.z + static_cast< float >(rand() % 11 - 5) * 0.08f
                };
                Vector3 zeroVelocity = { 0.0f, 0.0f, 0.0f };
                Vector4 cyanColor = { 0.4f, 0.9f, 1.0f, 1.0f };
                GPUParticleManager::GetInstance()->Emit(particlePosition, zeroVelocity, 0.25f, 0.4f, cyanColor);
            }
        }
    }

    bool isAttacking = ( timer_ >= 2 && timer_ <= 15 );
    if ( isAttacking ) {

        int randomDamage = damage_ + ( rand() % 2 );

        if ( isPlayerCaster_ ) {
            if ( enemyManager ) {
                for ( auto& currentEnemy : enemyManager->GetEnemies() ) {
                    if ( !currentEnemy || currentEnemy->IsDead() ) continue;

                    auto it = std::find(hitTargets_.begin(), hitTargets_.end(), currentEnemy.get());
                    if ( it != hitTargets_.end() ) continue;

                    Vector3 enemyPosition = currentEnemy->GetPosition();
                    Vector3 distanceVector = { enemyPosition.x - pos_.x, 0.0f, enemyPosition.z - pos_.z };

                    if ( Length(distanceVector) < 3.0f ) {
                        currentEnemy->TakeDamage(randomDamage);
                        hitTargets_.push_back(currentEnemy.get());

                        for ( int sparkIndex = 0; sparkIndex < 10; sparkIndex++ ) {
                            Vector3 sparkVelocity = { ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f };
                            Vector4 sparkColor = { 1.0f, 1.0f, 0.8f, 1.0f };
                            GPUParticleManager::GetInstance()->Emit(pos_, sparkVelocity, 0.2f, 0.2f, sparkColor);
                        }
                    }
                }
            }
            // ボスへの判定
            if ( boss && !boss->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), boss);
                if ( it == hitTargets_.end() ) {
                    Vector3 distanceVector = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                    if ( Length(distanceVector) < 5.0f ) {
                        boss->TakeDamage(randomDamage);
                        hitTargets_.push_back(boss);
                    }
                }
            }
            // 🌟 追加：分裂ボス (extraBoss) への判定
            if ( extraBoss && !extraBoss->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), extraBoss);
                if ( it == hitTargets_.end() ) {
                    Vector3 distanceVector = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z }; // extraBossの座標があればそれに変更してください
                    if ( Length(distanceVector) < 5.0f ) {
                        extraBoss->TakeDamage(randomDamage);
                        hitTargets_.push_back(extraBoss);
                    }
                }
            }
        } else {
            if ( player && !player->IsDead() ) {
                auto it = std::find(hitTargets_.begin(), hitTargets_.end(), player);
                if ( it == hitTargets_.end() ) {
                    Vector3 playerPosition = player->GetPosition();
                    Vector3 distanceVector = { playerPosition.x - pos_.x, 0.0f, playerPosition.z - pos_.z };
                    if ( Length(distanceVector) < 3.0f ) {
                        int enemyRandomDamage = damage_ + ( rand() % 2 );
                        player->TakeDamage(enemyRandomDamage, pos_);
                        hitTargets_.push_back(player);
                    }
                }
            }
        }
    }

    if ( timer_ >= effectDuration_ ) {
        isFinished_ = true;
    }
}

void SwordEffect::Draw(){
    if ( isFinished_ ) return;
    if ( obj_ ) obj_->Draw();
    for ( auto& afterimage : afterimages_ ) {
        if ( afterimage.isActive && afterimage.object ) {
            afterimage.object->Draw();
        }
    }
}

void SwordEffect::CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj){
    if ( !sourceObj || !destinationObj ) return;
    destinationObj->SetTranslation(sourceObj->GetTranslation());
    destinationObj->SetRotation(sourceObj->GetRotation());
    destinationObj->SetScale(sourceObj->GetScale());
}