#include "HammerEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void HammerEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
    hasHit_ = false;
    casterYaw_ = casterYaw;
    casterPos_ = casterPos;

    obj_ = Obj3d::Create("hammer_model"); // ※ハンマーのモデル名
    if ( obj_ ) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model* model = obj_->GetModel();
        if ( model && model->GetMaterial() ) {
            Model::Material* mat = model->GetMaterial();
            // 🌟 復活：ハンマー自体を超高熱のマグマ色に発光させる
            mat->color = { 1.0f, 0.3f, 0.0f, 1.0f };
            mat->emissive = 3.0f;
        }
    }

    afterimages_.clear();
    for ( int index = 0; index < maxAfterimageCount_; index++ ) {
        AfterimageData afterimage;
        afterimage.object = Obj3d::Create("hammer_model");
        if ( afterimage.object ) {
            afterimage.object->SetCamera(camera);
            afterimage.object->SetScale(scale_);
        }
        afterimage.isActive = false;
        afterimages_.push_back(std::move(afterimage));
    }
}

void HammerEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;
    timer_++;

    if ( obj_ ) {
        float progress = static_cast< float >( timer_ ) / 20.0f;
        if ( progress > 1.0f ) progress = 1.0f;

        float swingPitch = -0.5f + ( progress * 2.1f );
        float radius = 1.5f;
        float shoulderHeight = 1.2f;

        float forwardDist = std::sinf(swingPitch) * radius;
        float height = std::cosf(swingPitch) * radius;

        pos_ = {
            casterPos_.x + std::sinf(casterYaw_) * forwardDist,
            casterPos_.y + shoulderHeight + height,
            casterPos_.z + std::cosf(casterYaw_) * forwardDist
        };

        float tiltX = swingPitch;
        float rotY = casterYaw_;
        float rotZ = 0.0f;

        obj_->SetRotation({ tiltX, rotY, rotZ });
        obj_->SetTranslation(pos_);
        obj_->Update();

        // モーションブラー（残像）
        if ( timer_ < 14 ) {
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

        // 🌟 復活：残像のフェードアウトを熱を帯びた赤〜オレンジにする
        for ( auto& afterimage : afterimages_ ) {
            if ( afterimage.isActive ) {
                afterimage.lifeTimer--;
                float alphaRatio = static_cast< float >( afterimage.lifeTimer ) / static_cast< float >( defaultAfterimageLife_ );
                Vector4 afterimageColor = { 1.0f, 0.2f, 0.0f, alphaRatio * 0.5f }; // マグマ色
                afterimage.object->SetColor(afterimageColor);
                afterimage.object->Update();

                if ( afterimage.lifeTimer <= 0 ) {
                    afterimage.isActive = false;
                }
            }
        }

        // 🌟 復活：振り下ろしている最中に高熱の火の粉を散らす
        if ( timer_ < 14 ) {
            Vector3 sparkVel = { ( rand() % 11 - 5 ) * 0.02f, 0.05f, ( rand() % 11 - 5 ) * 0.02f };
            Vector4 sparkColor = { 1.0f, 0.4f, 0.0f, 0.8f }; // 鮮やかなオレンジ
            GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.15f, 0.4f, sparkColor);

            // 振り上げタメ（0〜6フレーム）：ハンマーに向かって上からエネルギーが集まる
            if ( timer_ < 7 ) {
                for (int i = 0; i < 4; i++) {
                    float angle = static_cast<float>(rand() % 628) * 0.01f;
                    float radius = 0.5f + static_cast<float>(rand() % 5) * 0.1f;
                    Vector3 from = { pos_.x + std::sinf(angle) * radius, pos_.y + 0.8f + static_cast<float>(rand() % 4) * 0.25f, pos_.z + std::cosf(angle) * radius };
                    float dx = pos_.x - from.x, dy = pos_.y - from.y, dz = pos_.z - from.z;
                    float d = std::sqrtf(dx * dx + dy * dy + dz * dz) + 0.001f;
                    float sp = 3.5f;
                    GPUParticleManager::GetInstance()->Emit(from, {dx/d*sp, dy/d*sp, dz/d*sp}, d/sp + 0.02f, 0.18f, {1.0f, 0.45f, 0.0f, 0.9f});
                }
            }
        }
    }

    // ==========================================
    // 🌟 爆発的ボリューム ＋ オレンジベースの衝撃波（14フレーム目）
    // ==========================================
    if ( timer_ >= 14 && !hasHit_ ) {

        int randomDamage = damage_;

        if ( isPlayerCaster_ ) {
            // 💥 1. 中心の強烈な閃光（白黄色の爆発コア）
            GPUParticleManager::GetInstance()->Emit(pos_, { 0,0,0 }, 0.15f, 4.0f, { 1.0f, 1.0f, 0.6f, 1.0f });

            // 💥 2. 分厚く這う炎の熱波リング（オレンジ＆赤）
            int dustCount = 80;
            for ( int i = 0; i < dustCount; i++ ) {
                float angle = ( static_cast< float >(i) / static_cast< float >(dustCount) ) * 3.14159f * 2.0f;
                Vector3 dir = { std::cosf(angle), 0.0f, std::sinf(angle) };

                float speed = 1.0f + ( rand() % 15 ) * 0.1f;
                float size = 0.5f + ( rand() % 10 ) * 0.1f;

                // 🌟 色をオレンジ〜赤の熱波に変更（密度によるドーム状の爆発になります）
                Vector4 waveColor = { 1.0f, 0.3f + ( rand() % 3 ) * 0.1f, 0.0f, 0.8f };

                Vector3 wavePos = { pos_.x, casterPos_.y + 0.1f, pos_.z };
                GPUParticleManager::GetInstance()->Emit(wavePos, dir * speed, 0.35f, size, waveColor);
            }

            // 💥 3. 鋭く飛び散る「焼け焦げた破片」（暗い赤茶色）
            int debrisCount = 40;
            for ( int i = 0; i < debrisCount; i++ ) {
                Vector3 debrisVel = {
                    ( rand() % 11 - 5 ) * 0.3f,
                    1.0f + ( rand() % 15 ) * 0.15f,
                    ( rand() % 11 - 5 ) * 0.3f
                };
                // 黒に近い赤茶色の岩石
                Vector4 debrisColor = { 0.4f, 0.1f, 0.0f, 1.0f };
                float debrisSize = 0.2f + ( rand() % 5 ) * 0.1f;

                GPUParticleManager::GetInstance()->Emit(pos_, debrisVel, 0.45f, debrisSize, debrisColor);
            }

            // 💥 4. 衝撃で上に吹き上がる爆煙（炎色）
            for ( int i = 0; i < 25; i++ ) {
                Vector3 smokeVel = {
                   ( rand() % 11 - 5 ) * 0.15f,
                   0.8f + ( rand() % 10 ) * 0.15f, // より高く
                   ( rand() % 11 - 5 ) * 0.15f
                };
                // 上に昇るにつれて消えていく熱い煙
                Vector4 smokeColor = { 1.0f, 0.2f, 0.0f, 0.5f };
                GPUParticleManager::GetInstance()->Emit(pos_, smokeVel, 0.5f, 1.5f, smokeColor);
            }

            // --- 当たり判定ロジック ---
            if ( enemyManager ) {
                for ( auto& enemy : enemyManager->GetEnemies() ) {
                    if ( !enemy || enemy->IsDead() ) continue;
                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    if ( Length(diff) < 4.0f ) {
                        enemy->TakeDamage(randomDamage);
                        enemy->SetStun(120);
                    }
                }
            }
            if ( boss && !boss->IsDead() ) {
                Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if ( Length(diff) < 5.0f ) {
                    boss->TakeDamage(randomDamage);
                    boss->SetStun(120);
                }
            }
            if ( extraBoss && !extraBoss->IsDead() ) {
                Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if ( Length(diff) < 5.0f ) {
                    extraBoss->TakeDamage(randomDamage);
                    extraBoss->SetStun(120);
                }
            }
        } else {
            if ( player && !player->IsDead() ) {
                Vector3 pPos = player->GetPosition();
                Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
                if ( Length(diff) < 3.5f ) {
                    player->TakeDamage(randomDamage, pos_);
                    player->SetStun(120);
                }
            }
        }

        hasHit_ = true;
    }

    if ( timer_ >= 30 ) {
        isFinished_ = true;
    }
}

void HammerEffect::Draw(){
    if ( !isFinished_ && obj_ ) {
        obj_->Draw();
    }
    for ( auto& afterimage : afterimages_ ) {
        if ( afterimage.isActive && afterimage.object ) {
            afterimage.object->Draw();
        }
    }
}

void HammerEffect::CopyTransform(const std::unique_ptr<Obj3d>& sourceObj, const std::unique_ptr<Obj3d>& destinationObj){
    if ( !sourceObj || !destinationObj ) return;
    destinationObj->SetTranslation(sourceObj->GetTranslation());
    destinationObj->SetRotation(sourceObj->GetRotation());
    destinationObj->SetScale(sourceObj->GetScale());
}