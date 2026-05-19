#include "SwordEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <algorithm>

using namespace VectorMath;

void SwordEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera){
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hitTargets_.clear(); // 🌟 攻撃のたびにヒット履歴をリセット
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

void SwordEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;

    timer_++;

    if ( isPlayerCaster_ && player ) {
        Vector3 playerPosition = player->GetPosition();

        if ( obj_ ) {
            // アニメーション時間（15フレームで一気に回転）
            float swingProgress = static_cast< float >( timer_ ) / 15.0f;
            if ( swingProgress > 1.0f ) swingProgress = 1.0f;

            // イージングで少し溜めてからシュバッ！と回る
            float easeT = swingProgress < 0.5f ? 2.0f * swingProgress * swingProgress : 1.0f - std::powf(-2.0f * swingProgress + 2.0f, 2.0f) / 2.0f;

            // =========================================================================================
            // 水平な「360度 回転斬り」の計算
            // =========================================================================================

            // プレイヤーの後ろ（-180度）から始まり、ぐるっと1周（+180度）回る！
            float startYawOffset = -3.14159f; // -PI
            float endYawOffset = 3.14159f;  // +PI
            float currentRotationYaw = casterYaw_ + startYawOffset + ( endYawOffset - startYawOffset ) * easeT;

            // 支点は完全にプレイヤーの中心（回転の軸になる）
            Vector3 pivotPos = {
                playerPosition.x,
                playerPosition.y + 1.2f, // 高さは一定（水平な横斬り）
                playerPosition.z
            };

            // プレイヤーの周囲を回る半径
            float swingRadius = 1.5f;

            pos_ = {
                pivotPos.x + std::sinf(currentRotationYaw) * swingRadius,
                pivotPos.y,
                pivotPos.z + std::cosf(currentRotationYaw) * swingRadius
            };

            //  剣の傾き：Z軸は0、X軸を90度(1.57f)にして「完全に水平（横）」にする
            float rotationPitchX = 1.57f;
            float rotationRollZ = 0.0f;

            // 回転と座標を適用
            obj_->SetRotation({ rotationPitchX, currentRotationYaw, rotationRollZ });
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
            int particleTraceCount = 5; // 円を描くように多めに散らす
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

    // =========================================================================================
    // 周囲の敵「全員」にヒットするように改修！
    // =========================================================================================
    bool isAttacking = ( timer_ >= 2 && timer_ <= 15 );
    if ( isAttacking ) { // 🌟 hasHit_ の判定を消し、毎回チェックする

        int randomDamage = damage_ + ( rand() % 2 );

        if ( isPlayerCaster_ ) {
            if ( enemyManager ) {
                for ( auto& currentEnemy : enemyManager->GetEnemies() ) {
                    if ( !currentEnemy || currentEnemy->IsDead() ) continue;

                    // 🌟 既にこの回転斬りでダメージを与えた敵はスルーする
                    auto it = std::find(hitTargets_.begin(), hitTargets_.end(), currentEnemy.get());
                    if ( it != hitTargets_.end() ) continue;

                    Vector3 enemyPosition = currentEnemy->GetPosition();
                    Vector3 distanceVector = { enemyPosition.x - pos_.x, 0.0f, enemyPosition.z - pos_.z };

                    // 剣の位置から一定範囲内ならヒット
                    if ( Length(distanceVector) < 3.0f ) { // 巻き込みやすく範囲を広めに
                        currentEnemy->TakeDamage(randomDamage);
                        hitTargets_.push_back(currentEnemy.get()); // 🌟 ヒットした敵を記憶

                        for ( int sparkIndex = 0; sparkIndex < 10; sparkIndex++ ) {
                            Vector3 sparkVelocity = { ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f };
                            Vector4 sparkColor = { 1.0f, 1.0f, 0.8f, 1.0f };
                            GPUParticleManager::GetInstance()->Emit(pos_, sparkVelocity, 0.2f, 0.2f, sparkColor);
                        }
                    }
                }
            }
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
        } else {
            // 敵が回転斬りを使った場合
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