#include "FistEffect.h"
#include "Engine/3D/Obj/Obj3d.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <memory>
#include <cmath>

using namespace VectorMath;

void FistEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;
	punchTimer_ = 10;
	casterYaw_ = casterYaw;

	// 胴体に当たるように少しだけ高さを上げる
	startPos_ = { casterPos.x, casterPos.y + 1.0f, casterPos.z };

	// 最初は使用者のすぐ目の前からスタート
	Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };
	pos_ = {
		startPos_.x + forward.x * 0.5f,
		startPos_.y,
		startPos_.z + forward.z * 0.5f
	};

	obj_ = Obj3d::Create("Fist_model");
	if ( obj_ ) {
		obj_->SetCamera(camera);
		obj_->SetScale(scale_);
		obj_->SetTranslation(pos_);

		// 拳の向きをプレイヤーの向いている方向（yaw）に合わせる
		obj_->SetRotation({ 0.0f, casterYaw_, 0.0f });

		Model* model = obj_->GetModel();
		if ( model ) {
			model->SetTexture("resources/white1x1.png");
			Model::Material* material = model->GetMaterial();
			if ( material ) {
				material->color = { 1.0f, 0.85f, 0.2f, 1.0f };
				material->emissive = 1.2f;
			}
		}
		obj_->Update();

		Vector3 fwd = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };

		// ① 前方への炎ストリーク（「この方向に打つ」という方向表示）
		for (int i = 0; i < 20; i++) {
			float spread = static_cast<float>(rand() % 9 - 4) * 0.06f;
			float speed = 0.6f + static_cast<float>(rand() % 10) * 0.08f;
			float sc = 0.12f + static_cast<float>(rand() % 4) * 0.04f;
			Vector3 vel = {
				fwd.x * speed + spread * 0.4f,
				0.02f + static_cast<float>(rand() % 4) * 0.02f,
				fwd.z * speed + spread * 0.4f
			};
			GPUParticleManager::GetInstance()->Emit(
				pos_, vel, 0.15f, sc, { 1.0f, 0.9f, 0.3f, 1.0f });
		}

		// ② 水平リング（小さく鋭い衝撃波）
		for (int i = 0; i < 12; i++) {
			float angle = (3.14159f * 2.0f / 12.0f) * static_cast<float>(i);
			Vector3 ringVel = { std::sinf(angle) * 0.5f, 0.0f, std::cosf(angle) * 0.5f };
			GPUParticleManager::GetInstance()->Emit(
				pos_, ringVel, 0.14f, 0.2f, { 1.0f, 0.6f, 0.1f, 0.9f });
		}

		// ③ 前方散開スパーク（打撃寸前の火の粉）
		for (int i = 0; i < 15; i++) {
			float upward = 0.08f + static_cast<float>(rand() % 8) * 0.06f;
			float hSpread = static_cast<float>(rand() % 11 - 5) * 0.12f;
			Vector3 vel = {
				fwd.x * (0.3f + static_cast<float>(rand() % 5) * 0.1f) + hSpread,
				upward,
				fwd.z * (0.3f + static_cast<float>(rand() % 5) * 0.1f) + hSpread
			};
			GPUParticleManager::GetInstance()->Emit(
				pos_, vel, 0.2f, 0.18f, { 1.0f, 0.65f, 0.05f, 1.0f });
		}
	}
}

void FistEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss,
	const Vector3& bossPos, const LevelData& level){

	if ( isFinished_ ) {
		return;
	}

	punchTimer_--;
	if ( punchTimer_ <= 0 ) {
		isFinished_ = true;
		return;
	}

	// ==================================================
	// 🌟 1. 前に突き出すアニメーション ＆ オーラ
	// ==================================================
	// 10フレームかけて、0.5fの距離から2.5fの距離まで一気に拳を前に飛ばす
	float progress = static_cast< float >( 10 - punchTimer_ ) / 10.0f;
	float distance = 0.5f + progress * 2.0f;
	Vector3 forward = { std::sinf(casterYaw_), 0.0f, std::cosf(casterYaw_) };

	pos_ = {
		startPos_.x + forward.x * distance,
		startPos_.y,
		startPos_.z + forward.z * distance
	};

	if ( obj_ ) {
		obj_->SetTranslation(pos_);
		obj_->Update();

		// 拳の炎トレイル（進行方向の逆に流れる火の粉）
		for (int i = 0; i < 3; i++) {
			Vector3 trailPos = {
				pos_.x + (rand() % 5 - 2) * 0.06f,
				pos_.y + static_cast<float>(rand() % 3) * 0.08f,
				pos_.z + (rand() % 5 - 2) * 0.06f
			};
			Vector3 vel = {
				-forward.x * (0.08f + static_cast<float>(rand() % 3) * 0.04f) + (rand() % 5 - 2) * 0.03f,
				0.02f + static_cast<float>(rand() % 3) * 0.02f,
				-forward.z * (0.08f + static_cast<float>(rand() % 3) * 0.04f) + (rand() % 5 - 2) * 0.03f
			};
			GPUParticleManager::GetInstance()->Emit(
				trailPos, vel, 0.18f, 0.2f, { 1.0f, 0.7f, 0.15f, 0.9f });
		}
		// 拳のコアに鋭い光点（小さめ）
		GPUParticleManager::GetInstance()->Emit(
			pos_, { 0.0f, 0.0f, 0.0f }, 0.12f, 0.6f, { 1.0f, 0.6f, 0.1f, 0.65f });
	}

	// ==================================================
	// 🌟 2. 当たり判定とヒット時の爆発エフェクト
	// ==================================================
	if ( isPlayerCaster_ ) {
		int randomDamage = damage_ + ( rand() % 2 );

		if ( enemyManager ) {
			for ( auto& enemy : enemyManager->GetEnemies() ) {
				if ( enemy && !enemy->IsDead() ) {
					Vector3 ePos = enemy->GetPosition();
					Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

					if ( Length(diff) < 2.0f ) {
						enemy->TakeDamage(randomDamage);

						// 💥 敵に当たった瞬間に重い打撃の火花を出す！
						for ( int i = 0; i < 20; i++ ) {
							Vector3 sparkVel = {
								( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f
							};
							GPUParticleManager::GetInstance()->Emit(
								pos_, sparkVel, 0.2f, 0.2f, { 1.0f, 0.8f, 0.2f, 1.0f });
						}

						isFinished_ = true;
						return;
					}
				}
			}
		}
		// 分裂戦では近い個体だけにダメージを入れる
		Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(pos_, 3.0f, boss, extraBoss);
		if ( hitBoss ) {
				hitBoss->TakeDamage(randomDamage);

				// 💥 ボスヒット時の火花
				for ( int i = 0; i < 20; i++ ) {
					Vector3 sparkVel = {
						( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f
					};
					GPUParticleManager::GetInstance()->Emit(
						pos_, sparkVel, 0.2f, 0.2f, { 1.0f, 0.8f, 0.2f, 1.0f });
				}

				isFinished_ = true;
				return;
		}
	} else {
		// 敵・ボスの攻撃
		if ( player && !player->IsDead() ) {
			Vector3 playerPos = player->GetPosition();
			Vector3 diff = { playerPos.x - pos_.x, 0.0f, playerPos.z - pos_.z };

			if ( Length(diff) < 2.0f ) {
				int randomDamage = damage_ + ( rand() % 2 );
				if ( boss && boss->IsAttackDebuffed() ) {
					randomDamage = damage_ + ( rand() % 2 );
				}
				player->TakeDamage(randomDamage, pos_);

				// 💥 プレイヤーヒット時の火花
				for ( int i = 0; i < 20; i++ ) {
					Vector3 sparkVel = {
						( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f, ( rand() % 11 - 5 ) * 0.2f
					};
					GPUParticleManager::GetInstance()->Emit(
						pos_, sparkVel, 0.2f, 0.2f, { 1.0f, 0.2f, 0.2f, 1.0f });
				}

				isFinished_ = true;
				return;
			}
		}
	}
}

void FistEffect::Draw(){
	if ( isFinished_ ) return;
	if ( obj_ ) obj_->Draw();
}
