#include "BossClawEffect.h"
#include "game/player/Player.h"
#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "engine/particle/GPUParticleManager.h" 
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

	// 10フレーム目に判定リセット
	if (timer_ == 10) {
		hasHit_ = false;
	}

	if (obj_) {
		float slashYaw = 0.0f;
		float slashPitch = 0.0f;
		float slashRoll = 0.0f;

		// ★ ボス用に振りかぶる距離と高さを大きくする
		float offsetDist = 5.0f;
		float heightOffset = 2.0f;

		//  1撃目 (1〜10フレーム)：超高速・横薙ぎ（右→左）
		if (timer_ < 10) {
			float progress = static_cast<float>(timer_) / 10.0f;
			slashYaw = -1.5f + progress * 3.0f;
			float currentYaw = casterYaw_ + slashYaw;

			pos_ = {
				casterPos_.x + std::sinf(currentYaw) * offsetDist,
				casterPos_.y + heightOffset,
				casterPos_.z + std::cosf(currentYaw) * offsetDist
			};
			obj_->SetRotation({ 0.0f, currentYaw, 0.0f });
			obj_->SetTranslation(pos_);
		}
		//  2撃目 (10〜20フレーム)：超高速・斬り上げ（下→上）
		else if (timer_ < 20) {
			float progress = static_cast<float>(timer_ - 10) / 10.0f;
			slashPitch = 1.2f - progress * 2.4f;
			slashRoll = 0.6f;

			pos_ = {
				casterPos_.x + std::sinf(casterYaw_) * offsetDist,
				casterPos_.y + heightOffset + (slashPitch * 2.0f), // 縦の動きもダイナミックに
				casterPos_.z + std::cosf(casterYaw_) * offsetDist
			};
			obj_->SetRotation({ slashPitch, casterYaw_, slashRoll });
			obj_->SetTranslation(pos_);
		}

		obj_->Update();

		// =========================================================
		// ★ ボス専用の巨大な赤いパーティクル！
		// =========================================================
		Vector3 tracePos = obj_->GetTranslation();
		Vector4 traceColor = { 1.0f, 0.1f, 0.1f, 0.6f }; // 禍々しい赤
		GPUParticleManager::GetInstance()->Emit(tracePos, { 0,0,0 }, 0.25f, 3.0f, traceColor); // スケールを大きく(3.0f)

		// ⚡ 激しい火花（速度と大きさをボス用に強化）
		for (int i = 0; i < 5; i++) {
			Vector3 sparkVel = {
				(rand() % 11 - 5) * 1.5f,
				(rand() % 11 - 5) * 1.5f,
				(rand() % 11 - 5) * 1.5f
			};
			// 赤〜オレンジの火花
			GPUParticleManager::GetInstance()->Emit(tracePos, sparkVel, 0.15f, 0.15f, { 1.0f, 0.4f, 0.0f, 1.0f });
		}
	}

	// 当たり判定 (判定時間を短くして「一瞬で切り裂く」感を出す)
	bool isAttacking = (timer_ >= 2 && timer_ <= 6) || (timer_ >= 12 && timer_ <= 16);

	if (isAttacking && !hasHit_) {
		// ボス専用の魔法なので、プレイヤーへの当たり判定のみ行う
		if (player && !player->IsDead()) {
			Vector3 playerPos = player->GetPosition();
			Vector3 diff = { playerPos.x - pos_.x, 0.0f, playerPos.z - pos_.z };

			// ★ ボスの巨大な爪に合わせて、当たり判定も広くする (4.0f)
			if (Length(diff) < 4.0f) {
				int finalDamage = damage_;

				// 発動元のボスが攻撃デバフ中ならダメージを半減する
				if (casterBoss_ && casterBoss_->IsAttackDebuffed()) {
					finalDamage = finalDamage / 2;
				}

				player->TakeDamage(finalDamage, pos_);
				hasHit_ = true;
			}
		}
	}

	if (timer_ >= 25) {
		isFinished_ = true;
	}
}

void BossClawEffect::Draw() {
	if (obj_) {
		obj_->Draw();
	}
}


