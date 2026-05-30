#include "FangEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "game/card/BossTargetUtils.h"
#include "engine/collision/Collision.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/camera/Camera.h"
#include "game/audio/GameSE.h"
#include <cmath>

using namespace VectorMath;

namespace {
const Vector4 kPlayerFangColor = { 0.55f, 0.38f, 0.15f, 1.0f };
const Vector4 kEnemyFangColor = { 0.75f, 0.16f, 0.08f, 1.0f };
}

void FangEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	// 使用者情報を保存
	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;
	fangs_.clear();
	indicators_.clear();

	// 撃った人（敵）の位置を記憶しておく
	casterPos_ = casterPos;
	camera_ = camera;
	hasShaken_ = false;

	// 正面方向を計算
	Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };


	// 前方に順番に5本並べる
	const float kFloorY = -1.0f;
	for ( int i = 0; i < 5; i++ ) {
		FangData fang;
		fang.pos = {
			casterPos.x + forward.x * ( 1.5f + i * 2.0f ),
			kFloorY,
			casterPos.z + forward.z * ( 1.5f + i * 2.0f )
		};
		// 地面の下深くからスタートさせる
		fang.currentY = kFloorY - 3.5f;

		// 順番に出る間隔と、地上に留まる時間
		fang.delayTimer = 15 + (i * 8);
		fang.activeTimer = 40;
		fang.isActive = false;
		fang.hasHit = false;
		fang.hasEmergedParticle = false;

		// 各トゲ専用のObj3dを生成（共有すると1本しか描画されない問題の修正）
		fang.obj = std::unique_ptr<Obj3d>(Obj3d::Create("Fang"));
		if ( fang.obj ) {
			fang.obj->SetCamera(camera);
			fang.obj->SetScale(scale_);
			Model* fangModel = fang.obj->GetModel();
			if ( fangModel ) {
				fangModel->SetTexture("resources/white1x1.png");
			}
			fang.obj->SetColor(isPlayerCaster_ ? kPlayerFangColor : kEnemyFangColor); // 敵版は赤茶で区別する
			fang.obj->SetEmissive(0.5f, 0);
			fang.obj->SetTranslation({ 0.0f, -1000.0f, 0.0f });
			fang.obj->Update();
		}

		fangs_.push_back(std::move(fang));
		// move後は fangs_.back() で参照する
		const Vector3& fangPos = fangs_.back().pos;
		// ==========================================
		// ★ 追加：IceBulletと同じ「赤い予兆円」の作成
		// ==========================================
		auto indicator = std::unique_ptr<Obj3d>(Obj3d::Create("sphere"));
		if (indicator) {
			indicator->SetCamera(camera);
			// 地面にめり込まないよう、少しだけ浮かせる(y + 0.05f)
			indicator->SetTranslation({ fangPos.x, fangPos.y + 0.05f, fangPos.z });

			// 当たり判定の範囲(半径1.5f)に合わせた大きさで平たくする
			indicator->SetScale({ 1.0f, 0.05f, 1.5f });

			Model *model = indicator->GetModel();
			if (model) {
				model->SetTexture("resources/white1x1.png");
			}
			// 氷魔法と同じように赤色で最初は薄く(0.1f)設定
			indicator->SetColor({ 1.0f, 0.0f, 0.0f, 0.1f });
			indicator->SetEmissive(1.0f, 0);
			indicator->Update();
		}
		indicators_.push_back(std::move(indicator));
	}

	

}

void FangEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss,
	const Vector3& bossPos, const LevelData& level){

	// 終了済みなら何もしない
	if ( isFinished_ ) {
		return;
	}

	bool allDone = true;
	for (size_t i = 0; i < fangs_.size(); ++i) {
		auto &fang = fangs_[i];

		// 出現待機中
		if (fang.delayTimer > 0) {
			fang.delayTimer--;

			// ==========================================
			// ★ 追加：予兆円の透明度を更新（だんだん濃くする）
			// ==========================================
			if (i < indicators_.size() && indicators_[i]) {
				// それぞれのトゲの初期待機時間(30 + i*8)を計算
				float maxDelay = 30.0f + (i * 8.0f);
				// 残り時間から進行度(0.0〜1.0)を出す
				float progress = 1.0f - (static_cast<float>(fang.delayTimer) / maxDelay);

				// 透明度を 0.1（薄い）から 0.5（濃い）へ変化させる
				indicators_[i]->SetColor({ 1.0f, 0.0f, 0.0f, 0.1f + (progress * 0.4f) });
				indicators_[i]->Update();
			}
			// 待機が終わったら有効化
			if ( fang.delayTimer <= 0 ) {
				fang.isActive = true;
			}

			allDone = false;
		}
		// 出現中
		else if ( fang.isActive ) {

			// --- 1. アニメーション（上下移動）処理 ---
			if ( fang.activeTimer > 0 ) {
				if ( fang.hasHit ) {
					fang.activeTimer = 0;
				}
				
				fang.activeTimer--;



				// まだ地上に出ていなければ、上に伸びる
				if ( fang.currentY < fang.pos.y ) {
					fang.currentY += 0.25f;
					if ( fang.currentY > fang.pos.y ) {
						fang.currentY = fang.pos.y;
					}
				}
				if ( !fang.hasEmergedParticle && fang.currentY >= fang.pos.y ) {
					fang.hasEmergedParticle = true;
					GameSE::Fang();

					// カメラシェイクは全体で1回だけ（最初のトゲ出現時のみ）
					if ( camera_ && !hasShaken_ ) {
						camera_->TriggerShake(0.18f, 10);
						hasShaken_ = true;
					}

					// 【地面から爆散】下から上に勢いよく飛び出す破片
					for ( int i = 0; i < 40; i++ ) {
						Vector3 pos = {
							fang.pos.x + ( rand() % 11 - 5 ) * 0.1f,
							fang.pos.y,
							fang.pos.z + ( rand() % 11 - 5 ) * 0.1f
						};
						Vector3 vel = {
							( rand() % 11 - 5 ) * 0.25f,                          // 横に広がる
							1.5f + static_cast< float >(rand() % 8) * 0.3f,       // 上に強く爆散
							( rand() % 11 - 5 ) * 0.25f
						};
						Vector4 color = { 0.6f, 0.35f, 0.1f, 1.0f };             // 土色
						float life = 0.4f + static_cast< float >( rand() % 4 ) * 0.1f;
						float scale = 0.2f + static_cast< float >( rand() % 4 ) * 0.08f;
						GPUParticleManager::GetInstance()->Emit(pos, vel, life, scale, color);
					}

					// 【火花】白と黄色の光の粒が四方八方に散る
					for ( int i = 0; i < 60; i++ ) {
						Vector3 pos = {
							fang.pos.x + ( rand() % 7 - 3 ) * 0.1f,
							fang.pos.y + static_cast< float >(rand() % 10) * 0.1f, // 高さランダム
							fang.pos.z + ( rand() % 7 - 3 ) * 0.1f
						};
						Vector3 vel = {
							( rand() % 11 - 5 ) * 0.5f,
							0.8f + static_cast< float >(rand() % 10) * 0.4f,      // 上方向強め
							( rand() % 11 - 5 ) * 0.5f
						};
						Vector4 color = ( rand() % 100 < 60 )
							? Vector4 { 1.0f, 0.9f, 0.3f, 1.0f }   // 黄色
						: Vector4 { 1.0f, 1.0f, 1.0f, 1.0f };  // 白
						float life = 0.15f + static_cast< float >( rand() % 3 ) * 0.05f;
						float scale = 0.05f + static_cast< float >( rand() % 3 ) * 0.03f;
						GPUParticleManager::GetInstance()->Emit(pos, vel, life, scale, color);
					}

					// 【大きい塊】ゆっくり上に舞い上がる大粒
					for ( int i = 0; i < 10; i++ ) {
						Vector3 pos = {
							fang.pos.x + ( rand() % 9 - 4 ) * 0.15f,
							fang.pos.y,
							fang.pos.z + ( rand() % 9 - 4 ) * 0.15f
						};
						Vector3 vel = {
							( rand() % 7 - 3 ) * 0.1f,
							0.6f + static_cast< float >(rand() % 5) * 0.15f,
							( rand() % 7 - 3 ) * 0.1f
						};
						Vector4 color = { 0.5f, 0.3f, 0.1f, 1.0f };
						float life = 0.6f + static_cast< float >( rand() % 3 ) * 0.1f;
						float scale = 0.4f + static_cast< float >( rand() % 3 ) * 0.1f;
						GPUParticleManager::GetInstance()->Emit(pos, vel, life, scale, color);
					}
				}
			} else {
				// 時間が来たらゆっくり地面に潜る
				fang.currentY -= 0.15f;
			}

			// 壁の中に出た場合は即終了
			if ( Collision::CheckBlockCollision(fang.pos, 0.5f, level) ) {
				fang.isActive = false;
				fang.activeTimer = 0;
				continue;
			}


			// --- 2. 当たり判定（出現中・停滞中のみ。沈降フェーズに入ったら判定終了） ---
			if ( fang.activeTimer > 0 && fang.currentY >= fang.pos.y - 0.5f ) {

				// プレイヤーが使った場合
				if ( isPlayerCaster_ ) {
					Vector3 playerPos = { 0,0,0 };
					if ( enemyManager ) {
						for ( auto& enemy : enemyManager->GetEnemies() ) {
							if ( !fang.hasHit && enemy && !enemy->IsDead() ) {
								Vector3 ePos = enemy->GetPosition();
								Vector3 diff = { ePos.x - fang.pos.x, 0.0f, ePos.z - fang.pos.z };

								if ( Length(diff) < 2.0f ) {
									Vector3 toEnemy = { ePos.x - playerPos.x, 0.0f, ePos.z - playerPos.z };
									float distanceToPlayer = Length(toEnemy);
									int finalDamage = damage_;

									if ( distanceToPlayer <= 3.0f ) {
										finalDamage += 2;
									} else if ( distanceToPlayer >= 8.0f ) {
										finalDamage -= 2;
									}
									finalDamage += ( rand() % 3 ) - 1;
									if ( finalDamage < 1 ) finalDamage = 1;

									enemy->TakeDamage(finalDamage);
									fang.hasHit = true;
								}
							}
						}
					}

					// 分裂戦では近い個体だけにダメージを入れる
					Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(fang.pos, 2.5f, boss, extraBoss);
					if ( !fang.hasHit && hitBoss ) {
						Vector3 targetBossPos = hitBoss->GetPosition();
						Vector3 toBoss = { targetBossPos.x - playerPos.x, 0.0f, targetBossPos.z - playerPos.z };
						float distanceToPlayer = Length(toBoss);
						int finalDamage = damage_;

						if ( distanceToPlayer <= 3.0f ) {
							finalDamage += 2;
						} else if ( distanceToPlayer >= 8.0f ) {
							finalDamage -= 2;
						}
						finalDamage += ( rand() % 3 ) - 1;
						if ( finalDamage < 1 ) finalDamage = 1;

						hitBoss->TakeDamage(finalDamage);
						fang.hasHit = true;
					}
				}
				// 敵またはボスが使った場合
				else {
					if ( !fang.hasHit && player && !player->IsDead() ) {
						Vector3 playerPos = player->GetPosition();
						Vector3 diff = { playerPos.x - fang.pos.x, 0.0f, playerPos.z - fang.pos.z };

						if ( Length(diff) < 1.5f ) {
							Vector3 toCaster = { casterPos_.x - playerPos.x, 0.0f, casterPos_.z - playerPos.z };
							float distanceToCaster = Length(toCaster);
							int finalDamage = damage_;

							if ( distanceToCaster <= 3.0f ) {
								finalDamage += 2;
							} else if ( distanceToCaster >= 8.0f ) {
								finalDamage -= 2;
							}
							finalDamage += ( rand() % 3 ) - 1;
							if ( finalDamage < 1 ) finalDamage = 1;

							player->TakeDamage(finalDamage, fang.pos);
							fang.hasHit = true;
						}
					}
				}
			} // --- 当たり判定ここまで ---


			// --- 3. 消滅処理（完全に地面の下に潜り切った時） ---
			if ( fang.activeTimer <= 0 && fang.currentY <= fang.pos.y - 3.5f ) {
				fang.isActive = false;

				for ( int i = 0; i < 15; i++ ) {
					Vector3 pos = { fang.pos.x, fang.pos.y, fang.pos.z };
					Vector3 vel = {
						( rand() % 11 - 5 ) * 0.08f,
						0.05f + static_cast< float >(rand() % 3) * 0.04f,
						( rand() % 11 - 5 ) * 0.08f
					};
					Vector4 color = { 0.6f, 0.4f, 0.2f, 1.0f };
					GPUParticleManager::GetInstance()->Emit(pos, vel, 0.25f, 0.15f, color);
				}

			}

			allDone = false;
		}
	}

	// 全てのトゲが終わったら効果終了
	if ( allDone ) {
		isFinished_ = true;
	}
}

void FangEffect::Draw(){
	if ( isFinished_ ) {
		return;
	}

	// 予兆円の描画（トゲが地面に隠れている待機中のみ）
	for (size_t i = 0; i < fangs_.size(); ++i) {
		if (fangs_[i].delayTimer > 0 && i < indicators_.size() && indicators_[i]) {
			indicators_[i]->Draw();
		}
	}

	// 有効なトゲを各自の obj で描画（毎フレーム SetColor で色を強制上書きして全本に反映させる）
	for ( auto& fang : fangs_ ) {
		if ( fang.isActive && fang.currentY > fang.pos.y - 1.0f && fang.obj ) {
			Vector3 drawPos = { fang.pos.x, fang.currentY, fang.pos.z };
			fang.obj->SetColor(isPlayerCaster_ ? kPlayerFangColor : kEnemyFangColor); // 毎フレーム強制適用
			fang.obj->SetTranslation(drawPos);
			fang.obj->Update();
			fang.obj->Draw();
		}
	}
}
