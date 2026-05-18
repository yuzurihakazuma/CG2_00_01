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

    // 🌟 本体モデルの生成
    obj_ = Obj3d::Create("sword_model");
    if ( obj_ ) {
        obj_->SetCamera(camera);
        obj_->SetScale(scale_);
        obj_->SetTranslation({ 0.0f, -1000.0f, 0.0f });
        obj_->Update();

        Model* model = obj_->GetModel();
        if ( model && model->GetMaterial() ) {
            Model::Material* mat = model->GetMaterial();
            mat->emissive = 1.5f;
        }
    }

    // 🌟 残像用モデルの初期化
    afterimages_.clear();
    for ( int i = 0; i < kAfterimageCount; i++ ) {
        Afterimage ai;
        ai.obj = Obj3d::Create("sword_model");
        if ( ai.obj ) {
            ai.obj->SetCamera(camera);
            ai.obj->SetScale(scale_);
        }
        ai.isActive = false;
        afterimages_.push_back(std::move(ai));
    }
}

void SwordEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;

    timer_++;

    if ( isPlayerCaster_ && player ) {
        Vector3 playerPos = player->GetPosition();

        // ==========================================
        // 🌟 横薙ぎモーションの計算
        // ==========================================
        float swingDuration = 15.0f;
        float t = std::clamp(static_cast< float >( timer_ ) / swingDuration, 0.0f, 1.0f);

        float baseTilt = 0.5f; // 前傾
        float startYawOffset = -1.5f; // 右へ振りかぶる角度
        float endYawOffset = 1.5f; // 左へ振り抜く角度

        // イージング(EaseInOut)で滑らかに振る
        float easeT = t < 0.5f ? 2.0f * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        float currentYaw = casterYaw_ + startYawOffset + ( endYawOffset - startYawOffset ) * easeT;

        // スイング軌道（少し下がりながら上がる）
        float tiltX = baseTilt + 0.4f * std::sinf(t * 3.14159f);

        // ==========================================
        // 🌟 修正：剣の位置をプレイヤーの「前方」にする！
        // プレイヤーの中心から radius 分だけ離れた円周上を移動させます
        // ==========================================
        float radius = 1.5f; // プレイヤーから剣を離す距離（剣の長さに合わせて微調整してください）
        float height = 1.0f; // 剣を振る高さ（腰〜胸のあたり）

        pos_ = {
            playerPos.x + std::sinf(currentYaw) * radius,
            playerPos.y + height,
            playerPos.z + std::cosf(currentYaw) * radius
        };

        if ( obj_ ) {
            // 回転と座標を適用
            // もし剣の向きが90度ズレている場合は、currentYaw + 1.57f などを試してください
            obj_->SetRotation({ tiltX, currentYaw, 0.0f });
            obj_->SetTranslation(pos_);
            obj_->Update();
        }

        // ==========================================
        // 🌟 残像の更新（ポーズをコピー）
        // ==========================================
        if ( timer_ < 15 && obj_ ) {
            for ( auto& ai : afterimages_ ) {
                if ( !ai.isActive ) {
                    ai.isActive = true;
                    ai.lifeTimer = kAfterimageLife;
                    CopyPRS(obj_, ai.obj);
                    ai.obj->Update();
                    break;
                }
            }
        }

        // 残像のフェードアウト処理
        for ( auto& ai : afterimages_ ) {
            if ( ai.isActive ) {
                ai.lifeTimer--;
                float alpha = static_cast< float >( ai.lifeTimer ) / static_cast< float >( kAfterimageLife );
                Vector4 color = { 0.2f, 0.9f, 1.0f, alpha * 0.4f }; // 水色の残像
                ai.obj->SetColor(color);
                ai.obj->Update();

                if ( ai.lifeTimer <= 0 ) {
                    ai.isActive = false;
                }
            }
        }

        // ==========================================
        // 🌟 斬撃パーティクル（剣の刃に沿って発生）
        // ==========================================
        if ( timer_ < 15 ) {
            int traceCount = 5;
            for ( int i = 0; i < traceCount; i++ ) {
                // 剣の先端の方（さらに外側）にパーティクルを散らす
                Vector3 tracePos = {
                    pos_.x + std::sinf(currentYaw) * ( static_cast< float >(rand() % 15) * 0.1f ),
                    pos_.y + static_cast< float >(rand() % 10 - 5) * 0.1f, // 上下に広く
                    pos_.z + std::cosf(currentYaw) * ( static_cast< float >(rand() % 15) * 0.1f )
                };
                Vector4 color = { 0.4f, 0.9f, 1.0f, 1.0f }; // 鮮やかな水色
                GPUParticleManager::GetInstance()->Emit(tracePos, { 0.0f, 0.0f, 0.0f }, 0.25f, 0.4f, color);
            }
        }
    }

    // ==========================================
    // 🌟 当たり判定（剣の位置 pos_ を基準にするので薙ぎ払いに応じて動く！）
    // ==========================================
    bool isAttacking = ( timer_ >= 3 && timer_ <= 12 );
    if ( isAttacking && !hasHit_ ) {
        int randomDamage = damage_ + ( rand() % 2 );

        if ( isPlayerCaster_ ) {
            if ( enemyManager ) {
                for ( auto& enemy : enemyManager->GetEnemies() ) {
                    if ( !enemy || enemy->IsDead() ) continue;
                    Vector3 ePos = enemy->GetPosition();
                    Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

                    // 剣の位置から半径2.5f以内ならヒット
                    if ( Length(diff) < 2.5f ) {
                        enemy->TakeDamage(randomDamage);
                        for ( int i = 0; i < 10; i++ ) {
                            Vector3 sparkVel = { ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f, ( rand() % 11 - 5 ) * 0.5f };
                            GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.2f, 0.2f, { 1.0f, 1.0f, 0.8f, 1.0f });
                        }
                        hasHit_ = true;
                        break;
                    }
                }
            }
            if ( boss && !boss->IsDead() ) {
                Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
                if ( Length(diff) < 4.0f ) {
                    boss->TakeDamage(randomDamage);
                    hasHit_ = true;
                }
            }
        } else {
            int enemyRandomDamage = damage_ + ( rand() % 2 );
            if ( player && !player->IsDead() ) {
                Vector3 pPos = player->GetPosition();
                Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
                if ( Length(diff) < 2.5f ) {
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
    for ( auto& ai : afterimages_ ) {
        if ( ai.isActive && ai.obj ) ai.obj->Draw();
    }
}

void SwordEffect::CopyPRS(const std::unique_ptr<Obj3d>& source, const std::unique_ptr<Obj3d>& dest){
    if ( !source || !dest ) return;
    dest->SetTranslation(source->GetTranslation());
    dest->SetRotation(source->GetRotation());
    dest->SetScale(source->GetScale());
}