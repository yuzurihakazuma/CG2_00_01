#include "FireballEffect.h"
#include "Engine/3D/Obj/Obj3d.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "game/enemy/EnemyManager.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/postEffect/PostEffect.h"
#include <memory>
#include <cmath>

using namespace VectorMath;

void FireballEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	// 使用者情報を保存
	isPlayerCaster_ = isPlayerCaster;
	camera_ = camera;
	isFinished_ = false;

	// 正面方向を計算
	Vector3 forward = {
		std::sinf(casterYaw),
		0.0f,
		std::cosf(casterYaw)
	};

	// 使用者の少し前から発射する
	pos_ = {
		casterPos.x + forward.x * 1.5f,
		casterPos.y,
		casterPos.z + forward.z * 1.5f
	};

	// 発射速度を設定する
	velocity_ = {
		forward.x * 0.3f,
		0.0f,
		forward.z * 0.3f
	};

	obj_ = Obj3d::Create("fireball_sphere");
	if ( obj_ ) {
		obj_->SetCamera(camera);
		obj_->SetScale(scale_);
		obj_->SetTranslation(pos_);
		obj_->Update();

		// 色・テクスチャ・発光の設定
		Model* model = obj_->GetModel();
		if ( model ) {
			// ベースを無地にする
			model->SetTexture("resources/white1x1.png");
			if (Model::Material* material = model->GetMaterial()) {
				material->emissive = 2.2f; // Bloomに乗るように自己発光を強める
			}
		}
	}
}

void FireballEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss,
	const Vector3& bossPos, const LevelData& level){

	// 終了済みなら何もしない
	if ( isFinished_ ) {
		return;
	}

	// 弾を進める
	pos_ += velocity_;

	const float kPI = 3.14159265f;
	rotAngle_ += 0.15f;   // 1フレームあたりの回転速度（大きいほど速く回る）


	// GPUパーティクル放出

	// 炎パーティクルを回転させながら放出
	{

		// 円周上の3点からパーティクルを出す
		const int kEmitCount = 5;
		const float kRadius = 0.5f; // ボールの周りの半径

		for ( int i = 0; i < kEmitCount; i++ ) {
			float angle = rotAngle_ + ( 2.0f * kPI / kEmitCount ) * i;

			// 円周上の放出位置
			Vector3 emitPos = {
				pos_.x + std::cosf(angle) * kRadius,
				pos_.y,
				pos_.z + std::sinf(angle) * kRadius
			};

			// 接線方向（回転方向）＋上向きの速度
			Vector3 vel = {
			  -std::sinf(angle) * 1.5f,
			  0.3f + static_cast< float >( rand() % 5 ) * 0.1f,  // 1.5f → 0.3f（ゆっくり上昇）
			  std::cosf(angle) * 1.5f
			};

			// 炎の色（オレンジ〜赤）
			Vector4 color = {
					1.0f,
					0.2f + static_cast< float >( rand() % 5 ) * 0.1f,
					0.0f,
					1.0f
			};

			float lifeTime = 0.5f + static_cast< float >( rand() % 3 ) * 0.1f; // 0.2〜0.4秒
			float scale = 0.5f + static_cast< float >( rand() % 3 ) * 0.05f;  // 0.15〜0.25

			GPUParticleManager::GetInstance()->Emit(emitPos, vel, lifeTime, scale, color);


			// コアの中心 白から黄色の明るい色
			{


				for ( int i = 0; i < 3; i++ ){


					Vector3 corePos = {
						pos_.x + ( rand() % 3 - 1 ) * 0.1f,
						pos_.y,
						pos_.z + ( rand() % 3 - 1 ) * 0.1f,

					};


					Vector3 coreVel = {
						( rand() % 3 - 1 ) * 0.3f,
						0.8f + static_cast< float >( rand() % 5 ) * 0.1f,
						( rand() % 3 - 1 ) * 0.3f
					};

					Vector4 coreColor = { 1.0f,1.0f,0.3f,1.0f }; // 白から黄色
					
					GPUParticleManager::GetInstance()->Emit(corePos, coreVel, 0.2f, 0.3f, coreColor);


				}


			}

			// --- Layer3: 軌跡（トレイル）― 進行方向と逆側に暗い炎を残す ---
			{
				Vector3 trailBase = {
					pos_.x - velocity_.x * 3.0f,
					pos_.y,
					pos_.z - velocity_.z * 3.0f
				};
				for ( int i = 0; i < 4; i++ ) {
					Vector3 trailPos = {
						trailBase.x + ( rand() % 5 - 2 ) * 0.2f,
						trailBase.y,
						trailBase.z + ( rand() % 5 - 2 ) * 0.2f
					};
					Vector3 trailVel = {
						( rand() % 3 - 1 ) * 0.2f,
						0.5f + static_cast< float >(rand() % 5) * 0.1f,
						( rand() % 3 - 1 ) * 0.2f
					};
					float g = 0.1f + static_cast< float >( rand() % 3 ) * 0.1f;
					Vector4 trailColor = { 0.8f, g, 0.0f, 0.9f }; // 暗い赤〜オレンジ
					float trailScale = 0.6f + static_cast< float >( rand() % 4 ) * 0.1f;
					GPUParticleManager::GetInstance()->Emit(trailPos, trailVel, 0.6f, trailScale, trailColor);
				}
			}

			// --- Layer4: 火の粉（スパーク）― 4フレームに1回飛び散る ---
			{
				sparkTimer_++;
				if ( sparkTimer_ % 4 == 0 ) {
					for ( int i = 0; i < 6; i++ ) {
						float sparkAngle = static_cast< float >(rand() % 628) * 0.01f;
						Vector3 sparkVel = {
							std::cosf(sparkAngle) * ( 1.5f + static_cast< float >(rand() % 5) * 0.3f ),
							1.0f + static_cast< float >(rand() % 8) * 0.2f,
							std::sinf(sparkAngle) * ( 1.5f + static_cast< float >(rand() % 5) * 0.3f )
						};
						Vector4 sparkColor = { 1.0f, 0.8f + static_cast< float >(rand() % 2) * 0.2f, 0.0f, 1.0f };
						GPUParticleManager::GetInstance()->Emit(pos_, sparkVel, 0.3f, 0.15f, sparkColor);
					}
				}
			}



		}
	}

	// 表示位置を更新
	if ( obj_ ) {
		obj_->SetTranslation(pos_);
		obj_->Update();
	}

	// プレイヤーが使った弾
	if ( isPlayerCaster_ ) {

		int randomDamage = damage_ + (rand() % 3);

		if ( enemyManager ) {
			for ( auto& enemy : enemyManager->GetEnemies() ) {
				// 雑魚敵への判定
				if ( enemy && !enemy->IsDead() ) {

					// ループ中の個別の敵の座標を取得する！
					Vector3 ePos = enemy->GetPosition();

					// Y軸を無視してXZ平面で距離判定する
					Vector3 diff = {
						ePos.x - pos_.x,
						0.0f,
						ePos.z - pos_.z
					};

					if ( Length(diff) < 2.3f ) {
						enemy->TakeDamage(randomDamage);
						// 爆発エフェクト
						GPUParticleManager::GetInstance()->Emit(pos_, {0,0,0}, 0.12f, 2.0f, {1.0f, 0.9f, 0.4f, 1.0f});
						for ( int j = 0; j < 20; j++ ) {
							float a = static_cast<float>(rand() % 628) * 0.01f;
							float sp = 1.5f + static_cast<float>(rand() % 10) * 0.2f;
							Vector3 ev = { std::cosf(a)*sp, 0.5f + static_cast<float>(rand()%8)*0.15f, std::sinf(a)*sp };
							Vector4 ec = { 1.0f, 0.2f + static_cast<float>(rand()%4)*0.1f, 0.0f, 1.0f };
							GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.35f, 0.3f, ec);
						}
						for ( int j = 0; j < 8; j++ ) {
							Vector3 sv = { (rand()%11-5)*0.4f, 1.2f+(rand()%6)*0.2f, (rand()%11-5)*0.4f };
							GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.25f, 0.15f, {1.0f, 1.0f, 0.5f, 1.0f});
						}
						// 着弾点に熱波歪みを一時発生（炎の爆発演出）
						if ( camera_ ) {
							const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
							float hcx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
							float hcy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
							float hcw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
							if ( hcw > 0.001f ) {
								PostEffect::GetInstance()->TriggerDistortion(
									hcx/hcw*0.5f+0.5f, -hcy/hcw*0.5f+0.5f, 0.20f, 45);
							}
						}
						isFinished_ = true;
						return;
					}
				}
			}
		}

		// 分裂戦では近い個体だけに命中させる
		Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(pos_, 2.8f, boss, extraBoss);
		if ( hitBoss ) {
			hitBoss->TakeDamage(randomDamage);
			// 爆発エフェクト
			GPUParticleManager::GetInstance()->Emit(pos_, {0,0,0}, 0.12f, 2.0f, {1.0f, 0.9f, 0.4f, 1.0f});
			for ( int j = 0; j < 20; j++ ) {
				float a = static_cast<float>(rand() % 628) * 0.01f;
				float sp = 1.5f + static_cast<float>(rand() % 10) * 0.2f;
				Vector3 ev = { std::cosf(a)*sp, 0.5f + static_cast<float>(rand()%8)*0.15f, std::sinf(a)*sp };
				Vector4 ec = { 1.0f, 0.2f + static_cast<float>(rand()%4)*0.1f, 0.0f, 1.0f };
				GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.35f, 0.3f, ec);
			}
			for ( int j = 0; j < 8; j++ ) {
				Vector3 sv = { (rand()%11-5)*0.4f, 1.2f+(rand()%6)*0.2f, (rand()%11-5)*0.4f };
				GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.25f, 0.15f, {1.0f, 1.0f, 0.5f, 1.0f});
			}
			// 着弾点に熱波歪みを一時発生（炎の爆発演出）
			if ( camera_ ) {
				const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
				float hcx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
				float hcy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
				float hcw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
				if ( hcw > 0.001f ) {
					PostEffect::GetInstance()->TriggerDistortion(
						hcx/hcw*0.5f+0.5f, -hcy/hcw*0.5f+0.5f, 0.20f, 45);
				}
			}
			isFinished_ = true;
			return;
		}

		// 遠くまで飛んだら消す
		if ( pos_.x > 100.0f || pos_.x < -100.0f || pos_.z > 100.0f || pos_.z < -100.0f ) {
			isFinished_ = true;
			return;
		}
	}
	// 敵またはボスが使った弾
	else {

		if ( player && !player->IsDead() ) {
			Vector3 playerPos = player->GetPosition();

			// Y軸を無視してXZ平面で距離判定する
			Vector3 diff = {
				playerPos.x - pos_.x,
				0.0f,
				playerPos.z - pos_.z
			};

			if ( Length(diff) < 1.5f ) {
				int randomDamage = damage_ <= 0 ? 1 : damage_ + (rand() % 2);

				if ( boss && boss->IsAttackDebuffed() ) {
					randomDamage = damage_ <= 0 ? 1 : damage_ + (rand() % 2);
				}

				player->TakeDamage(randomDamage, pos_);
				// 爆発エフェクト
				GPUParticleManager::GetInstance()->Emit(pos_, {0,0,0}, 0.12f, 2.0f, {1.0f, 0.9f, 0.4f, 1.0f});
				for ( int j = 0; j < 20; j++ ) {
					float a = static_cast<float>(rand() % 628) * 0.01f;
					float sp = 1.5f + static_cast<float>(rand() % 10) * 0.2f;
					Vector3 ev = { std::cosf(a)*sp, 0.5f + static_cast<float>(rand()%8)*0.15f, std::sinf(a)*sp };
					Vector4 ec = { 1.0f, 0.2f + static_cast<float>(rand()%4)*0.1f, 0.0f, 1.0f };
					GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.35f, 0.3f, ec);
				}
				for ( int j = 0; j < 8; j++ ) {
					Vector3 sv = { (rand()%11-5)*0.4f, 1.2f+(rand()%6)*0.2f, (rand()%11-5)*0.4f };
					GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.25f, 0.15f, {1.0f, 1.0f, 0.5f, 1.0f});
				}
				// 着弾点に熱波歪みを一時発生（炎の爆発演出）
				if ( camera_ ) {
					const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
					float hcx = pos_.x*vp.m[0][0]+pos_.y*vp.m[1][0]+pos_.z*vp.m[2][0]+vp.m[3][0];
					float hcy = pos_.x*vp.m[0][1]+pos_.y*vp.m[1][1]+pos_.z*vp.m[2][1]+vp.m[3][1];
					float hcw = pos_.x*vp.m[0][3]+pos_.y*vp.m[1][3]+pos_.z*vp.m[2][3]+vp.m[3][3];
					if ( hcw > 0.001f ) {
						PostEffect::GetInstance()->TriggerDistortion(
							hcx/hcw*0.5f+0.5f, -hcy/hcw*0.5f+0.5f, 0.20f, 45);
					}
				}
				isFinished_ = true;
				return;
			}
		}
	}

	// 当たり判定の後で壁との衝突を見る
	if ( Collision::CheckBlockCollision(pos_, 0.5f, level) ) {
		// 壁衝突：小さめの炎の散り
		for ( int j = 0; j < 10; j++ ) {
			float a = static_cast<float>(rand() % 628) * 0.01f;
			float sp = 0.8f + static_cast<float>(rand() % 5) * 0.15f;
			Vector3 ev = { std::cosf(a)*sp, 0.3f+(rand()%5)*0.1f, std::sinf(a)*sp };
			GPUParticleManager::GetInstance()->Emit(pos_, ev, 0.25f, 0.2f, {1.0f, 0.3f, 0.0f, 0.9f});
		}
		isFinished_ = true;
		return;
	}
}

void FireballEffect::Draw(){
	
}
