#include "FistEffect.h"
#include "Engine/3D/Obj/Obj3d.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
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
	}
}

void FistEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss,
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

		// 拳の周りに風圧のようなオレンジ色のオーラを残す
		GPUParticleManager::GetInstance()->Emit(
			pos_, { 0,0,0 }, 0.2f, 1.0f, { 1.0f, 0.6f, 0.1f, 0.3f });
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
		// ボスへの判定
		if ( boss && !boss->IsDead() ) {
			Vector3 diff = { bossPos.x - pos_.x, 0.0f, bossPos.z - pos_.z };
			if ( Length(diff) < 3.0f ) {
				boss->TakeDamage(randomDamage);

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
