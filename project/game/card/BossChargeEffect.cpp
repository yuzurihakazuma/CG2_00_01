#include "BossChargeEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Boss.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>

using namespace VectorMath;

void BossChargeEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
	// この効果はプレイヤー使用を想定していない
	(void)isPlayerCaster;

	// この突進を発動したボス本人を保持する
	// 分裂ボス時に左右どちらの個体を前進させるかを失わないようにする
	casterBoss_ = casterBoss;

	isFinished_ = false;
	hasHit_ = false;
	timer_ = duration_;
	pos_ = casterPos;

	// 開始時の向きで突進方向を固定する
	direction_ = {
		std::sinf(casterYaw),
		0.0f,
		std::cosf(casterYaw)
	};

	obj_ = Obj3d::Create("sphere");
	if (obj_) {
		obj_->SetCamera(camera);
		obj_->SetTranslation(pos_);
		obj_->SetRotation({ 0.0f, casterYaw, 0.0f });
		obj_->SetScale(scale_);

		Model* model = obj_->GetModel();
		if (model) {
			model->SetTexture("resources/white1x1.png");

			Model::Material* material = model->GetMaterial();
			if (material) {
				// 突進予兆が分かりやすいように赤寄りで発光させる
				material->color = { 1.0f, 0.2f, 0.2f, 0.45f };
				material->emissive = 2.8f;
			}
		}

		obj_->Update();
	}
}

void BossChargeEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, const Vector3& bossPos, const LevelData& level) {
	// この効果では未使用
	(void)enemyManager;
	(void)boss;
	(void)bossPos;
	(void)level;

	if (isFinished_) {
		return;
	}

	// 発動元ボスが消えているなら継続できない
	if (!casterBoss_ || casterBoss_->IsDead()) {
		isFinished_ = true;
		return;
	}

	// ボス本体を突進方向へ前進させる
	Vector3 nextPos = casterBoss_->GetPosition();
	nextPos += direction_ * speed_;
	casterBoss_->SetPosition(nextPos);
	pos_ = nextPos;

	// 向きは突進方向へ固定する
	if (Length(direction_) > 0.01f) {
		Vector3 rot = casterBoss_->GetRotation();
		rot.y = std::atan2f(direction_.x, direction_.z);
		casterBoss_->SetRotation(rot);
	}

	// 突進中の軌跡パーティクル
	for (int i = 0; i < 5; ++i) {
		Vector3 trailPos = {
			pos_.x + (rand() % 21 - 10) * 0.08f,
			pos_.y + 1.0f + (rand() % 11 - 5) * 0.08f,
			pos_.z + (rand() % 21 - 10) * 0.08f
		};

		Vector3 trailVel = {
			-direction_.x * 0.08f + (rand() % 11 - 5) * 0.01f,
			0.02f + (rand() % 5) * 0.01f,
			-direction_.z * 0.08f + (rand() % 11 - 5) * 0.01f
		};

		GPUParticleManager::GetInstance()->Emit(
			trailPos,
			trailVel,
			0.35f,
			0.6f,
			{ 1.0f, 0.25f, 0.15f, 0.85f }
		);
	}

	// 見た目オブジェクトを本体へ追従させる
	if (obj_) {
		Vector3 rot = casterBoss_->GetRotation();
		obj_->SetTranslation(pos_);
		obj_->SetRotation({ 0.0f, rot.y, 0.0f });
		obj_->SetScale(scale_);
		obj_->Update();
	}

	// プレイヤーへは1回だけ当たる
	if (player && !player->IsDead() && !hasHit_) {
		Vector3 playerPos = player->GetPosition();
		Vector3 diff = {
			playerPos.x - pos_.x,
			0.0f,
			playerPos.z - pos_.z
		};

		if (Length(diff) < hitRadius_) {
			int finalDamage = damage_;

			// 発動元のボスが攻撃デバフ中ならダメージを半減する
			if (casterBoss_->IsAttackDebuffed()) {
				finalDamage = finalDamage / 2;
			}

			player->TakeDamage(finalDamage, pos_);
			hasHit_ = true;

			// 命中時に火花を出す
			for (int i = 0; i < 20; ++i) {
				Vector3 sparkVel = {
					(rand() % 21 - 10) * 0.08f,
					(rand() % 11) * 0.05f,
					(rand() % 21 - 10) * 0.08f
				};

				GPUParticleManager::GetInstance()->Emit(
					pos_,
					sparkVel,
					0.4f,
					0.8f,
					{ 1.0f, 0.7f, 0.2f, 1.0f }
				);
			}
		}
	}

	timer_--;
	if (timer_ <= 0) {
		isFinished_ = true;
	}
}

void BossChargeEffect::Draw() {
	if (!isFinished_ && obj_) {
		obj_->Draw();
	}
}
