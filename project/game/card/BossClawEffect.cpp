#include "BossClawEffect.h"
#include "game/player/Player.h"
#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "engine/particle/GPUParticleManager.h" 
#include <algorithm>
#include <cmath>

using namespace VectorMath;

void BossClawEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
	isFinished_ = false;
	timer_ = 0; // 例：24フレーム（約0.4秒）で攻撃の判定が消える
	hasHit_ = false;


	// この攻撃を発動したボス本人を保持する
// 今は直接使っていないが、分裂ボス対応で発動元を失わないようにしておく
	casterBoss_ = casterBoss;

	// ボスの位置と向きを記憶
	casterYaw_ = casterYaw;
	casterPos_ = casterPos;
	lastSafeBossPos_ = casterPos;

	

	// プレイヤーのClawと同じモデル（sphere）を生成
	scale_ = { 40.0f, 40.0f, 40.0f }; // プレイヤー(20.0f)の2倍の巨大爪！

	obj_ = Obj3d::Create("claw_model");
	if (obj_) {
		obj_->SetCamera(camera);
		obj_->SetScale(scale_);
		obj_->SetTranslation(pos_);

		Model *model = obj_->GetModel();
		if (model && model->GetMaterial()) {
			Model::Material *mat = model->GetMaterial();
			mat->color = { 0.7f, 0.0f, 1.0f, 1.0f }; // ボスらしい凶悪な赤色
			mat->emissive = 3.0f;                    // 限界まで発光
		}
	}
}

void BossClawEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {
	if (isFinished_) return;

	timer_++;

	// X斬り2撃目に入る直前に当たり判定をリセット
	if (timer_ == 30) {
		hasHit_ = false;
	}

	if (obj_) {
		static constexpr float kPi = 3.14159265f;
		static constexpr float kBackDist   = 4.0f; // 後退距離
		static constexpr float kLeapDist   = 8.0f; // 着地点（ボス正面）
		static constexpr float kSlashDist  = 5.0f; // X斬り時の爪オフセット
		static constexpr float kSlashRoll  = 0.785f; // 45度 = π/4

		// --------------------------------------------------
		// Phase 1: 後退・ため (0〜12フレーム)
		// --------------------------------------------------
		if (timer_ < 12) {
			float t = static_cast<float>(timer_) / 12.0f;
			float backOffset = t * kBackDist;

			// ボス本体を後退させる
			Vector3 bossMovedPos = {
				casterPos_.x - std::sinf(casterYaw_) * backOffset,
				casterPos_.y,
				casterPos_.z - std::cosf(casterYaw_) * backOffset
			};
			bossMovedPos = ApplyBossPosition(bossMovedPos, level);
			if (casterBoss_) casterBoss_->SetPosition(bossMovedPos);

			// クローはボスの少し前に構える
			pos_ = { bossMovedPos.x, bossMovedPos.y + 1.5f, bossMovedPos.z };
			float chargePitch = -0.6f * t;
			obj_->SetRotation({ chargePitch, casterYaw_ + kPi, 0.0f });
			obj_->SetTranslation(pos_);
		}
		// --------------------------------------------------
		// Phase 2: 前方へ飛び込み (12〜22フレーム)
		// --------------------------------------------------
		else if (timer_ < 22) {
			float t = static_cast<float>(timer_ - 12) / 10.0f;
			// t² で加速感
			float leapOffset = t * t * kLeapDist;
			float heightArc  = std::sinf(t * kPi) * 3.5f;

			// ボス本体を飛び込ませる
			Vector3 bossLeapPos = {
				casterPos_.x + std::sinf(casterYaw_) * leapOffset,
				casterPos_.y + heightArc,
				casterPos_.z + std::cosf(casterYaw_) * leapOffset
			};
			bossLeapPos = ApplyBossPosition(bossLeapPos, level);
			if (casterBoss_) casterBoss_->SetPosition(bossLeapPos);

			// クローはボスに追従
			pos_ = { bossLeapPos.x, bossLeapPos.y + 1.5f, bossLeapPos.z };
			float divePitch = -1.0f * t;
			obj_->SetRotation({ divePitch, casterYaw_ + kPi, 0.0f });
			obj_->SetTranslation(pos_);

			// 着地点を毎フレーム更新 (Phase3/4 の基準位置)
			landPos_ = { bossLeapPos.x, casterPos_.y, bossLeapPos.z };
		}
		// --------------------------------------------------
		// Phase 3: X斬り1撃目 右上→左下 (22〜30フレーム)
		// --------------------------------------------------
		else if (timer_ < 30) {
			// ボスは着地点で静止
			landPos_ = ApplyBossPosition(landPos_, level);
			if (casterBoss_) casterBoss_->SetPosition(landPos_);

			float t = static_cast<float>(timer_ - 22) / 8.0f;
			float pitch    = -1.2f + t * 2.4f;
			float yawSweep =  1.2f - t * 2.4f;
			float currentYaw = casterYaw_ + yawSweep;
			pos_ = {
				landPos_.x + std::sinf(currentYaw) * kSlashDist,
				landPos_.y + 2.0f + pitch * 1.5f,
				landPos_.z + std::cosf(currentYaw) * kSlashDist
			};
			obj_->SetRotation({ pitch, currentYaw + kPi, kSlashRoll });
			obj_->SetTranslation(pos_);
		}
		// --------------------------------------------------
		// Phase 4: X斬り2撃目 左上→右下 (30〜38フレーム)
		// --------------------------------------------------
		else if (timer_ < 38) {
			landPos_ = ApplyBossPosition(landPos_, level);
			if (casterBoss_) casterBoss_->SetPosition(landPos_);

			float t = static_cast<float>(timer_ - 30) / 8.0f;
			float pitch    = -1.2f + t * 2.4f;
			float yawSweep = -1.2f + t * 2.4f;
			float currentYaw = casterYaw_ + yawSweep;
			pos_ = {
				landPos_.x + std::sinf(currentYaw) * kSlashDist,
				landPos_.y + 2.0f + pitch * 1.5f,
				landPos_.z + std::cosf(currentYaw) * kSlashDist
			};
			obj_->SetRotation({ pitch, currentYaw + kPi, -kSlashRoll });
			obj_->SetTranslation(pos_);
		}
		// --------------------------------------------------
		// Phase 終了後: ボスを着地点に固定
		// --------------------------------------------------
		else {
			landPos_ = ApplyBossPosition(landPos_, level);
			if (casterBoss_) casterBoss_->SetPosition(landPos_);
		}

		obj_->Update();

		// =========================================================
		// パーティクル: フェーズごとに色を変える
		// =========================================================
		Vector3 tracePos = obj_->GetTranslation();

		if (timer_ < 12) {
			// ため中: 紫の蓄積エフェクト
			GPUParticleManager::GetInstance()->Emit(tracePos, { 0,0,0 }, 0.3f, 2.0f, { 0.6f, 0.0f, 1.0f, 0.5f });
		} else if (timer_ < 22) {
			// -----------------------------------------------
			// 踏み切り爆発 (timer_==12 の1フレームだけ)
			// -----------------------------------------------
			if (timer_ == 12) {
				// 地面から外側に広がる土煙・砂埃
				for (int i = 0; i < 16; i++) {
					float angle = (3.14159265f * 2.0f / 16.0f) * i;
					float speed = 2.5f + (rand() % 10) * 0.3f;
					Vector3 dustVel = {
						std::cosf(angle) * speed,
						(rand() % 5) * 0.3f,       // 少し上にも飛ぶ
						std::sinf(angle) * speed
					};
					// 土色〜砂色
					float brown = 0.55f + (rand() % 10) * 0.03f;
					GPUParticleManager::GetInstance()->Emit(
						casterPos_, dustVel, 0.35f, 1.8f,
						{ brown, brown * 0.7f, 0.1f, 0.8f });
				}
				// 中心の爆発フラッシュ (白〜黄)
				for (int i = 0; i < 8; i++) {
					Vector3 flashVel = {
						(rand() % 7 - 3) * 0.5f,
						(rand() % 5) * 0.8f,
						(rand() % 7 - 3) * 0.5f
					};
					GPUParticleManager::GetInstance()->Emit(
						casterPos_, flashVel, 0.2f, 2.5f,
						{ 1.0f, 0.95f, 0.6f, 0.9f });
				}
			}

			// -----------------------------------------------
			// 飛翔中トレイル: 進行方向の逆に流れる青白い残像
			// -----------------------------------------------
			{
				// 進行方向の逆ベクトル (後方へ流す)
				Vector3 backDir = {
					-std::sinf(casterYaw_) * 3.0f,
					-1.0f,
					-std::cosf(casterYaw_) * 3.0f
				};
				// メインの白トレイル
				GPUParticleManager::GetInstance()->Emit(
					tracePos, backDir, 0.18f, 2.0f,
					{ 0.85f, 0.9f, 1.0f, 0.75f });

				// 周囲にランダムな残像粒子 (青みがかった白)
				for (int i = 0; i < 4; i++) {
					Vector3 trailVel = {
						backDir.x + (rand() % 7 - 3) * 0.4f,
						backDir.y + (rand() % 5 - 2) * 0.3f,
						backDir.z + (rand() % 7 - 3) * 0.4f
					};
					Vector3 trailPos = {
						tracePos.x + (rand() % 5 - 2) * 0.2f,
						tracePos.y + (rand() % 5 - 2) * 0.2f,
						tracePos.z + (rand() % 5 - 2) * 0.2f
					};
					GPUParticleManager::GetInstance()->Emit(
						trailPos, trailVel, 0.12f, 1.2f,
						{ 0.6f, 0.75f, 1.0f, 0.55f });
				}
			}

			// -----------------------------------------------
			// 着地衝撃波 (timer_==21 の1フレームだけ)
			// -----------------------------------------------
			if (timer_ == 21) {
				// 地面から外側に破片が放射状に飛ぶ
				for (int i = 0; i < 24; i++) {
					float angle = (3.14159265f * 2.0f / 24.0f) * i;
					float speed = 3.5f + (rand() % 12) * 0.4f;
					Vector3 debrisVel = {
						std::cosf(angle) * speed,
						(rand() % 8) * 0.5f,        // 高く飛び散る
						std::sinf(angle) * speed
					};
					// 暗い灰色〜白の破片
					float gray = 0.5f + (rand() % 10) * 0.05f;
					GPUParticleManager::GetInstance()->Emit(
						landPos_, debrisVel, 0.4f, 1.5f,
						{ gray, gray, gray, 0.9f });
				}
				// 着地点の爆発フラッシュ (紫〜白)
				for (int i = 0; i < 12; i++) {
					Vector3 flashVel = {
						(rand() % 9 - 4) * 0.8f,
						(rand() % 8) * 0.6f,
						(rand() % 9 - 4) * 0.8f
					};
					GPUParticleManager::GetInstance()->Emit(
						landPos_, flashVel, 0.25f, 3.0f,
						{ 0.8f, 0.4f, 1.0f, 0.95f });
				}
				// 地面に沿って広がるリング状の衝撃波
				for (int i = 0; i < 20; i++) {
					float angle = (3.14159265f * 2.0f / 20.0f) * i;
					float speed = 5.0f;
					Vector3 ringVel = {
						std::cosf(angle) * speed,
						0.1f,
						std::sinf(angle) * speed
					};
					GPUParticleManager::GetInstance()->Emit(
						landPos_, ringVel, 0.2f, 0.8f,
						{ 1.0f, 1.0f, 1.0f, 0.7f });
				}
			}
		} else {
			// X斬り中: 禍々しい赤い火花
			GPUParticleManager::GetInstance()->Emit(tracePos, { 0,0,0 }, 0.25f, 3.0f, { 1.0f, 0.1f, 0.1f, 0.6f });
			for (int i = 0; i < 5; i++) {
				Vector3 sparkVel = {
					(rand() % 11 - 5) * 1.5f,
					(rand() % 11 - 5) * 1.5f,
					(rand() % 11 - 5) * 1.5f
				};
				GPUParticleManager::GetInstance()->Emit(tracePos, sparkVel, 0.15f, 0.15f, { 1.0f, 0.4f, 0.0f, 1.0f });
			}
		}
	}

	// 当たり判定: X斬り1撃目(22〜28f) と 2撃目(30〜36f)
	bool isAttacking = (timer_ >= 22 && timer_ <= 28) || (timer_ >= 30 && timer_ <= 36);

	if (isAttacking && !hasHit_) {
		if (player && !player->IsDead()) {
			Vector3 playerPos = player->GetPosition();
			Vector3 diff = { playerPos.x - pos_.x, 0.0f, playerPos.z - pos_.z };

			if (Length(diff) < 2.5f) {
				int finalDamage = damage_;
				if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
					finalDamage = finalDamage / 2;
				}
				player->TakeDamage(finalDamage, pos_);
				hasHit_ = true;
			}
		}
	}

	if (timer_ >= 45) {
		isFinished_ = true;
	}
}

bool BossClawEffect::IsBossPositionBlocked(const Vector3& position, const LevelData& level) const {
	if (level.width <= 0 || level.height <= 0 || level.tileSize <= 0.0f) {
		return false;
	}

	constexpr float kBossWallHalfSize = 2.05f;
	constexpr float kWallHalfSize = 1.0f;
	const float blockDistance = kBossWallHalfSize + kWallHalfSize;

	int bossGridX = static_cast<int>(std::round(position.x / level.tileSize));
	int bossGridZ = static_cast<int>(std::round(position.z / level.tileSize));
	int startX = std::max(0, bossGridX - 2);
	int endX = std::min(level.width - 1, bossGridX + 2);
	int startZ = std::max(0, bossGridZ - 2);
	int endZ = std::min(level.height - 1, bossGridZ + 2);

	for (int z = startZ; z <= endZ; z++) {
		for (int x = startX; x <= endX; x++) {
			if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
				continue;
			}

			float worldX = x * level.tileSize;
			float worldZ = z * level.tileSize;
			if (std::fabs(position.x - worldX) < blockDistance &&
				std::fabs(position.z - worldZ) < blockDistance) {
				return true;
			}
		}
	}

	return false;
}

Vector3 BossClawEffect::ApplyBossPosition(const Vector3& position, const LevelData& level) {
	if (!IsBossPositionBlocked(position, level)) {
		lastSafeBossPos_ = position;
		return position;
	}

	return { lastSafeBossPos_.x, position.y, lastSafeBossPos_.z };
}

void BossClawEffect::Draw() {
	if (obj_) {
		obj_->Draw();
	}
}


