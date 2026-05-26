#include "ClawEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

// デバッグ用変数の実体と初期値（例として1ダメージ）
int ClawEffect::debugBaseDamage = 1;

void ClawEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;
	timer_ = 0;
	hasHit_ = false;
	casterYaw_ = casterYaw;

	casterPos_ = casterPos;

	Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };
	pos_ = {
			casterPos.x + forward.x * 1.8f, // 勢いを出すため少し遠くから
			casterPos.y + 1.2f,             // 胴体狙いの高さ
			casterPos.z + forward.z * 1.8f
	};

	obj_ = Obj3d::Create("claw_model");
	if ( obj_ ) {
		obj_->SetCamera(camera);
		obj_->SetScale(scale_);
		obj_->SetTranslation(pos_);

		Model* model = obj_->GetModel();
		if ( model && model->GetMaterial() ) {
			Model::Material* mat = model->GetMaterial();
			mat->color = { 0.1f, 0.9f, 1.0f, 1.0f }; // より鮮やかなシアン
			mat->emissive = 3.0f;                   // 限界まで発光
		}
	}
}

void ClawEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){
	if ( isFinished_ ) return;

	timer_++;

	// 10フレーム目に判定リセット（高速化に合わせて判定タイミングも早める）
	if ( timer_ == 10 ) {
		hasHit_ = false;
	}

	if ( obj_ ) {
		float slashYaw = 0.0f;
		float slashPitch = 0.0f;
		float slashRoll = 0.0f;

		//  1撃目 (1〜10フレーム)：超高速・横薙ぎ（右→左）
		if ( timer_ < 10 ) {
			float progress = static_cast< float >(timer_) / 10.0f;
			slashYaw = -1.5f + progress * 3.0f;
			float currentYaw = casterYaw_ + slashYaw;
			// pos_ をスイング弧に沿って更新
			pos_ = {
				casterPos_.x + std::sinf(currentYaw) * 2.0f,
				casterPos_.y + 1.2f,
				casterPos_.z + std::cosf(currentYaw) * 2.0f
			};
			obj_->SetRotation({ 0.0f, currentYaw, 0.0f });
			obj_->SetTranslation(pos_);
		}
		//  2撃目 (10〜20フレーム)：超高速・斬り上げ（下→上）
		else if ( timer_ < 20 ) {
			float progress = static_cast< float >(timer_ - 10) / 10.0f;
			slashPitch = 1.2f - progress * 2.4f;
			slashRoll = 0.6f;
			// 2撃目は真正面の位置で縦振り
			pos_ = {
				casterPos_.x + std::sinf(casterYaw_) * 2.0f,
				casterPos_.y + 1.2f + slashPitch,
				casterPos_.z + std::cosf(casterYaw_) * 2.0f
			};
			obj_->SetRotation({ slashPitch, casterYaw_, slashRoll });
			obj_->SetTranslation(pos_);
		}

		obj_->Update();

		//  【残像】勢いを出すため「毎フレーム」出す！
		Vector3 tracePos = obj_->GetTranslation();
		Vector4 traceColor = { 0.0f, 0.6f, 1.0f, 0.6f };
		GPUParticleManager::GetInstance()->Emit(tracePos, { 0,0,0 }, 0.25f, 1.8f, traceColor);

		// ⚡ 【激しい火花】速度と量を倍増
		for ( int i = 0; i < 5; i++ ) {
			Vector3 sparkVel = {
				( rand() % 11 - 5 ) * 0.8f, // 速度 0.4 → 0.8 へ強化！
				( rand() % 11 - 5 ) * 0.8f,
				( rand() % 11 - 5 ) * 0.8f
			};
			// 閃光のような白に近い青
			GPUParticleManager::GetInstance()->Emit(tracePos, sparkVel, 0.1f, 0.08f, { 0.8f, 1.0f, 1.0f, 1.0f });
		}
	}

	// 当たり判定 (判定時間を短くして「一瞬で切り裂く」感を出す)
	bool isAttacking = ( timer_ >= 2 && timer_ <= 6 ) || ( timer_ >= 12 && timer_ <= 16 );
	if ( isAttacking && !hasHit_ ) {

		// ImGuiで設定したダメージをここで使う！
		int randomDamage = damage_ <= 0 ? 1 : damage_ + (rand() % 2);
		if ( isPlayerCaster_ ) {
			// 敵への判定（略：既存のダメージロジックと同じ）
			if ( enemyManager ) {
				for ( auto& enemy : enemyManager->GetEnemies() ) {
					if ( !enemy || enemy->IsDead() ) continue;
					Vector3 ePos = enemy->GetPosition();
					Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };
					if ( Length(diff) < 3.0f ) {
						enemy->TakeDamage(randomDamage);
						// シアン斬撃エフェクト
						GPUParticleManager::GetInstance()->Emit(ePos, {0,0,0}, 0.1f, 1.5f, {0.3f, 1.0f, 1.0f, 0.9f});
						for ( int j = 0; j < 15; j++ ) {
							Vector3 sv = { (rand()%11-5)*0.5f, (rand()%11-5)*0.5f, (rand()%11-5)*0.5f };
							GPUParticleManager::GetInstance()->Emit(ePos, sv, 0.2f, 0.18f, {0.5f, 1.0f, 1.0f, 1.0f});
						}
						hasHit_ = true; break;
					}
				}
			}
			// 分裂戦では近い個体だけにダメージを入れる
			Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(pos_, 3.5f, boss, extraBoss);
			if ( hitBoss ) {
				hitBoss->TakeDamage(randomDamage);
				// シアン斬撃エフェクト
				GPUParticleManager::GetInstance()->Emit(pos_, {0,0,0}, 0.1f, 1.5f, {0.3f, 1.0f, 1.0f, 0.9f});
				for ( int j = 0; j < 15; j++ ) {
					Vector3 sv = { (rand()%11-5)*0.5f, (rand()%11-5)*0.5f, (rand()%11-5)*0.5f };
					GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.2f, 0.18f, {0.5f, 1.0f, 1.0f, 1.0f});
				}
				hasHit_ = true;
			}
		} else {
			// プレイヤーへの判定
			if ( player && !player->IsDead() ) {
				Vector3 pPos = player->GetPosition();
				Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };
				if ( Length(diff) < 1.8f ) {
					player->TakeDamage(randomDamage, pos_);
					// シアン斬撃エフェクト
					GPUParticleManager::GetInstance()->Emit(pos_, {0,0,0}, 0.1f, 1.5f, {0.3f, 1.0f, 1.0f, 0.9f});
					for ( int j = 0; j < 15; j++ ) {
						Vector3 sv = { (rand()%11-5)*0.5f, (rand()%11-5)*0.5f, (rand()%11-5)*0.5f };
						GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.2f, 0.18f, {0.5f, 1.0f, 1.0f, 1.0f});
					}
					hasHit_ = true;
				}
			}
		}
	}

	if ( timer_ >= 25 ) isFinished_ = true;
}

void ClawEffect::Draw(){
	if ( !isFinished_ && obj_ ) {
		obj_->Draw();
	}
}
