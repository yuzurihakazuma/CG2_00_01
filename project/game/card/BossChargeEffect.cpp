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

	// 突進開始の予兆バースト（前方に赤い衝撃波）
	for (int i = 0; i < 16; i++) {
		float a = (3.14159f * 2.0f / 16.0f) * i;
		Vector3 v = { std::sinf(a) * 1.2f, 0.1f, std::cosf(a) * 1.2f };
		GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 0.5f, pos_.z }, v, 0.25f, 0.35f, { 1.0f, 0.2f, 0.1f, 1.0f });
	}
	// 前方方向へ集中する赤い突進予告ライン
	for (int i = 0; i < 12; i++) {
		float spread = static_cast<float>(rand() % 11 - 5) * 0.06f;
		float fwdSp  = 1.5f + static_cast<float>(rand() % 8) * 0.2f;
		Vector3 v = { direction_.x * fwdSp + spread, 0.05f + static_cast<float>(rand() % 5) * 0.03f, direction_.z * fwdSp + spread };
		GPUParticleManager::GetInstance()->Emit(pos_, v, 0.2f, 0.22f, { 1.0f, 0.35f, 0.1f, 0.9f });
	}
}

void BossChargeEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
	// この効果では未使用
	(void)enemyManager;
	(void)boss;
	(void)bossPos;

	if (isFinished_) {
		return;
	}

	// 発動元ボスが消えているなら継続できない
	if (!casterBoss_ || casterBoss_->IsDead()) {
		isFinished_ = true;
		return;
	}

	// ボス本体を突進方向へ進める
	Vector3 currentPos = casterBoss_->GetPosition();
	Vector3 nextPos = currentPos + direction_ * speed_;

	// 突進先が壁に当たるなら壁衝突エフェクトを出して終了する
	if (Collision::CheckBlockCollision(nextPos, 1.0f, level)) {
		// 壁衝突：爆発的な赤白の飛び散り
		for (int i = 0; i < 30; i++) {
			float a = static_cast<float>(rand() % 628) * 0.01f;
			float sp = 0.8f + static_cast<float>(rand() % 12) * 0.15f;
			Vector3 v = { std::cosf(a) * sp, 0.5f + static_cast<float>(rand() % 8) * 0.15f, std::sinf(a) * sp };
			Vector4 c = (rand() % 2 == 0)
				? Vector4{ 1.0f, 0.3f, 0.1f, 1.0f }
				: Vector4{ 1.0f, 0.85f, 0.6f, 1.0f };
			GPUParticleManager::GetInstance()->Emit(pos_, v, 0.4f, 0.25f + static_cast<float>(rand() % 4) * 0.06f, c);
		}
		// 壁衝突リング
		for (int i = 0; i < 20; i++) {
			float a = (3.14159f * 2.0f / 20.0f) * i;
			Vector3 v = { std::sinf(a) * 1.4f, 0.05f, std::cosf(a) * 1.4f };
			GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 0.3f, pos_.z }, v, 0.3f, 0.35f, { 1.0f, 0.5f, 0.2f, 1.0f });
		}
		isFinished_ = true;
		return;
	}

	// 壁に当たっていない時だけ位置を更新する
	casterBoss_->SetPosition(nextPos);
	pos_ = nextPos;

	// 向きは突進方向へ固定する
	if (Length(direction_) > 0.01f) {
		Vector3 rot = casterBoss_->GetRotation();
		rot.y = std::atan2f(direction_.x, direction_.z);
		casterBoss_->SetRotation(rot);
	}

	// ① 本体後方の赤炎トレイル
	for (int i = 0; i < 10; ++i) {
		Vector3 trailPos = {
			pos_.x + (rand() % 17 - 8) * 0.07f,
			pos_.y + 0.8f + (rand() % 9 - 4) * 0.07f,
			pos_.z + (rand() % 17 - 8) * 0.07f
		};
		Vector3 trailVel = {
			-direction_.x * 0.2f + (rand() % 9 - 4) * 0.02f,
			0.02f + (rand() % 5) * 0.01f,
			-direction_.z * 0.2f + (rand() % 9 - 4) * 0.02f
		};
		Vector4 col = (rand() % 2 == 0)
			? Vector4{ 1.0f, 0.25f, 0.1f, 0.9f }
			: Vector4{ 1.0f, 0.6f,  0.2f, 0.7f };
		GPUParticleManager::GetInstance()->Emit(trailPos, trailVel, 0.3f, 0.28f, col);
	}
	// ② 地面をえぐる土煙（足元左右に広がる）
	for (int i = 0; i < 5; ++i) {
		float side = (i % 2 == 0) ? 1.0f : -1.0f;
		Vector3 dustVel = {
			-direction_.z * side * (0.4f + static_cast<float>(rand() % 5) * 0.1f),
			0.15f + static_cast<float>(rand() % 5) * 0.05f,
			 direction_.x * side * (0.4f + static_cast<float>(rand() % 5) * 0.1f)
		};
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.1f, pos_.z }, dustVel, 0.4f, 0.3f, { 0.7f, 0.4f, 0.2f, 0.6f });
	}
	// ③ 白熱コア（ボス直後の白い閃光点）
	GPUParticleManager::GetInstance()->Emit(
		{ pos_.x - direction_.x * 0.3f, pos_.y + 0.8f, pos_.z - direction_.z * 0.3f },
		{ 0.0f, 0.0f, 0.0f }, 0.08f, 0.55f, { 1.0f, 0.9f, 0.7f, 0.95f });

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

			// 命中エフェクト：全方向に赤白の小粒バースト
			for (int i = 0; i < 35; ++i) {
				float a = static_cast<float>(rand() % 628) * 0.01f;
				float sp = 1.0f + static_cast<float>(rand() % 12) * 0.2f;
				Vector3 sv = { std::cosf(a) * sp, 0.6f + static_cast<float>(rand() % 8) * 0.15f, std::sinf(a) * sp };
				Vector4 sc = (rand() % 3 == 0)
					? Vector4{ 1.0f, 1.0f, 0.8f, 1.0f }
					: Vector4{ 1.0f, 0.3f, 0.1f, 1.0f };
				GPUParticleManager::GetInstance()->Emit(pos_, sv, 0.35f, 0.22f + static_cast<float>(rand() % 4) * 0.06f, sc);
			}
			// 衝撃波リング
			for (int i = 0; i < 20; ++i) {
				float a = (3.14159f * 2.0f / 20.0f) * i;
				Vector3 rv = { std::sinf(a) * 1.6f, 0.0f, std::cosf(a) * 1.6f };
				GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 0.4f, pos_.z }, rv, 0.25f, 0.3f, { 1.0f, 0.5f, 0.2f, 1.0f });
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
