#include "SwordEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

void SwordEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera){
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hasHit_ = false;
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
            material->emissive = 2.0f; // 派手に発光させる
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

void SwordEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;

    timer_++;

    if ( isPlayerCaster_ && player ) {
        Vector3 playerPosition = player->GetPosition();

        if ( obj_ ) {
            // アニメーション時間（15フレームで振り抜く）
            float swingProgress = static_cast< float >( timer_ ) / 15.0f;
            if ( swingProgress > 1.0f ) swingProgress = 1.0f;

            // イージング(EaseInOut)
            float easeT = swingProgress < 0.5f ? 2.0f * swingProgress * swingProgress : 1.0f - std::powf(-2.0f * swingProgress + 2.0f, 2.0f) / 2.0f;

            // =========================================================================================
            // 🌟 修正：あなたが作った「一番良かった横薙ぎ」の軌道に戻します！
            // =========================================================================================
            float startYawOffset = -1.2f;
            float endYawOffset = 1.2f;
            float currentRotationYaw = casterYaw_ + startYawOffset + ( endYawOffset - startYawOffset ) * easeT;

            // =========================================================================================
            // 🌟 修正：スイングの支点を「胸の前」にズラす（体にめり込まないようにする）
            // =========================================================================================
            Vector3 forwardVec = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
            float offsetForward = 0.6f; // プレイヤーの少し前を支点にする

            Vector3 pivotPos = {
                playerPosition.x + forwardVec.x * offsetForward,
                playerPosition.y + 1.2f, // 🌟 高さは一定（水平な横薙ぎ！）
                playerPosition.z + forwardVec.z * offsetForward
            };

            float swingRadius = 1.2f; // 剣の振る半径

            pos_ = {
                pivotPos.x + std::sinf(currentRotationYaw) * swingRadius,
                pivotPos.y,
                pivotPos.z + std::cosf(currentRotationYaw) * swingRadius
            };

            // =========================================================================================
            // 🌟 回転の適用（Z軸は0固定で、刃筋がピシッと通った横薙ぎになる！）
            // =========================================================================================
            float rotationPitchX = 1.57f;
            float rotationYawOffset = 0.0f;
            float rotationRollZ = 0.0f;

            obj_->SetRotation({ rotationPitchX, currentRotationYaw + rotationYawOffset, rotationRollZ });
            obj_->SetTranslation(pos_);
            obj_->Update();

            // グレーの斬撃軌跡エフェクト
            Vector3 zeroVelocity = { 0.0f, 0.0f, 0.0f };
            Vector4 grayColor = { 0.4f, 0.4f, 0.4f, 0.1f };
            GPUParticleManager::GetInstance()->Emit(pos_, zeroVelocity, 0.1f, 1.2f, grayColor);

            // クロー風の残像の更新
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

        // 残像のフェードアウト処理
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

        // 水色の斬撃パーティクル
        if ( timer_ < 15 ) {
            int particleTraceCount = 4;
            for ( int index = 0; index < particleTraceCount; index++ ) {
                Vector3 particlePosition = {
                    pos_.x + static_cast< float >(rand() % 11 - 5) * 0.05f,
                    pos_.y + static_cast< float >(rand() % 11 - 5) * 0.05f,
                    pos_.z + static_cast< float >(rand() % 11 - 5) * 0.05f
                };
                Vector3 zeroVelocity = { 0.0f, 0.0f, 0.0f };
                Vector4 cyanColor = { 0.4f, 0.9f, 1.0f, 1.0f };
                GPUParticleManager::GetInstance()->Emit(particlePosition, zeroVelocity, 0.25f, 0.4f, cyanColor);
            }
        }
    }

    // 当たり判定
    bool isAttacking = ( timer_ >= 3 && timer_ <= 12 );
    if ( isAttacking && !hasHit_ ) {
        int randomDamage = damage_ + ( rand() % 2 );

        if ( isPlayerCaster_ ) {
            if ( enemyManager ) {
                for ( auto& currentEnemy : enemyManager->GetEnemies() ) {
                    if ( !currentEnemy || currentEnemy->IsDead() ) continue;
                    Vector3 enemyPosition = currentEnemy->GetPosition();
                    Vector3 distanceVector = { enemyPosition.x - pos_.x, 0.0f, enemyPosition.z - pos_.z };

                    if ( Length(distanceVector) < 2.5f ) {
                        currentEnemy->TakeDamage(randomDamage);
                        for ( int sparkIndex = 0; sparkIndex < 10; sparkIndex++ ) {
                            Vector3 sparkVelocity = { ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f };
                            Vector4 sparkColor = { 1.0f, 1.0f, 0.8f, 1.0f };
                            GPUParticleManager::GetInstance()->Emit(pos_, sparkVelocity, 0.2f, 0.2f, sparkColor);
                        }
                        hasHit_ = true;
                        break;
                    }
                }
            }
            if ( boss && !boss->IsDead() ) {
                Vector3 distanceVector = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if ( Length(distanceVector) < 4.0f ) {
                    boss->TakeDamage(randomDamage);
                    hasHit_ = true;
                }
            }
        } else {
            int enemyRandomDamage = damage_ + ( rand() % 2 );
            if ( player && !player->IsDead() ) {
                Vector3 playerPosition = player->GetPosition();
                Vector3 distanceVector = { playerPosition.x - pos_.x, 0.0f, playerPosition.z - pos_.z };
                if ( Length(distanceVector) < 2.5f ) {
                    player->TakeDamage(enemyRandomDamage, pos_);
                    hasHit_ = true;
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