#include "DecoyEffect.h"
#include <cmath>
#include "engine/particle/GPUParticleManager.h"
#include "engine/collision/Collision.h"

using namespace VectorMath;

void DecoyEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss *casterBoss) {
	(void)casterBoss;
	isFinished_ = false;
	isBeingHit_ = false;
	timer_ = duration_;

	casterPos_ = casterPos;

	Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };
	pos_ = { casterPos.x + forward.x * 2.0f, casterPos.y, casterPos.z + forward.z * 2.0f };

	model_ = SkinnedObj3d::Create("playerDecoy", "resources/player", "player.gltf");
	if (model_) {
		model_->SetName("Decoy");
		model_->SetCamera(camera);
		model_->SetLoopAnimation(true);
		model_->SetIsWalking(false);
		model_->SetScale(scale_);
		model_->SetRotation({ 0.0f, casterYaw, 0.0f });
		model_->SetTranslation(pos_);

		if (model_->GetModel()) {
			model_->GetModel()->SetColor({ 0.70f, 0.90f, 1.25f, 0.95f });
			if (model_->GetModel()->GetMaterial()) {
				model_->GetModel()->GetMaterial()->emissive = 1.6f;
			}
		}

		model_->Update();
	}

	// =========================================
	// 出現バースト（大）
	// =========================================

	// ① 外側に弾け飛ぶ粒子（50個）
	for (int i = 0; i < 50; i++) {
		Vector3 burstVel = {
			(rand() % 21 - 10) * 0.22f,
			0.3f + (rand() % 12) * 0.09f,
			(rand() % 21 - 10) * 0.22f
		};
		Vector3 burstPos = {
			pos_.x + (rand() % 9 - 4) * 0.1f,
			pos_.y + 0.4f + (rand() % 8) * 0.2f,
			pos_.z + (rand() % 9 - 4) * 0.1f
		};
		// シアン～白をランダムに混ぜる
		float brightness = 0.7f + (rand() % 4) * 0.1f;
		GPUParticleManager::GetInstance()->Emit(burstPos, burstVel, 0.5f, 0.28f, { brightness * 0.5f, brightness * 0.9f, 1.0f, 1.0f });
	}

	// ② 地面リング（低め）
	for (int i = 0; i < 20; i++) {
		float angle = (3.14159f * 2.0f / 20.0f) * i;
		float speed = 0.45f;
		Vector3 ringVel = { std::sinf(angle) * speed, 0.05f, std::cosf(angle) * speed };
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.15f, pos_.z },
			ringVel, 0.35f, 0.9f, { 0.3f, 0.8f, 1.0f, 1.0f }
		);
	}

	// ③ 中段リング
	for (int i = 0; i < 16; i++) {
		float angle = (3.14159f * 2.0f / 16.0f) * i;
		float speed = 0.35f;
		Vector3 ringVel = { std::sinf(angle) * speed, 0.08f, std::cosf(angle) * speed };
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.9f, pos_.z },
			ringVel, 0.3f, 0.7f, { 0.55f, 0.9f, 1.0f, 0.95f }
		);
	}
	// ④ 上段リング
	for (int i = 0; i < 12; i++) {
		float angle = (3.14159f * 2.0f / 12.0f) * i;
		float speed = 0.25f;
		Vector3 ringVel = { std::sinf(angle) * speed, 0.1f, std::cosf(angle) * speed };
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 1.7f, pos_.z },
			ringVel, 0.28f, 0.55f, { 0.7f, 0.95f, 1.0f, 0.9f }
		);
	}

	// ⑤ 上昇コラム（真上に吹き上がる）
	for (int i = 0; i < 15; i++) {
		Vector3 colVel = {
			(rand() % 7 - 3) * 0.05f,
			1.2f + (rand() % 10) * 0.18f,
			(rand() % 7 - 3) * 0.05f
		};
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.3f, pos_.z },
			colVel, 0.55f, 0.22f, { 0.6f, 0.92f, 1.0f, 0.95f }
		);
	}

	// ⑥ 大フラッシュ（中心）
	GPUParticleManager::GetInstance()->Emit(
		{ pos_.x, pos_.y + 0.9f, pos_.z },
		{ 0, 0, 0 }, 0.15f, 4.5f, { 0.75f, 0.95f, 1.0f, 0.95f }
	);
	// ⑦ 白コアフラッシュ
	GPUParticleManager::GetInstance()->Emit(
		{ pos_.x, pos_.y + 0.9f, pos_.z },
		{ 0, 0, 0 }, 0.1f, 2.5f, { 1.0f, 1.0f, 1.0f, 1.0f }
	);
}

void DecoyEffect::NotifyHit() {
	isBeingHit_ = true;
}

void DecoyEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) {
	(void)player;
	(void)enemyManager;
	(void)boss;
	(void)bossPos;
	(void)level;

	if (isFinished_) return;

	// ==========================================
	// 最初の1フレーム目だけ壁判定を行う
	// ==========================================
	if (timer_ == duration_) {

		bool isWall = Collision::CheckBlockCollision(pos_, 0.5f, level);

		if (isWall) {
			// 目の前が壁だったら、保存しておいたプレイヤーの足元に書き換える
			pos_ = casterPos_;

			// モデルの座標を即座に上書きしてチラつきを防ぐ
			if (model_) {
				model_->SetTranslation(pos_);
				model_->Update();
			}
		}
	}

	timer_--;

	// =========================================
	// 消滅エフェクト（timer_ が 0 になった瞬間）
	// =========================================
	if (timer_ <= 0) {
		// 外側に散る粒子（40個）
		for (int i = 0; i < 40; i++) {
			Vector3 vel = {
				(rand() % 21 - 10) * 0.18f,
				0.2f + (rand() % 10) * 0.1f,
				(rand() % 21 - 10) * 0.18f
			};
			Vector3 p = {
				pos_.x + (rand() % 7 - 3) * 0.1f,
				pos_.y + 0.3f + (rand() % 8) * 0.2f,
				pos_.z + (rand() % 7 - 3) * 0.1f
			};
			GPUParticleManager::GetInstance()->Emit(p, vel, 0.5f, 0.22f, { 0.4f, 0.85f, 1.0f, 0.9f });
		}
		// 地面リング（消滅波）
		for (int i = 0; i < 18; i++) {
			float angle = (3.14159f * 2.0f / 18.0f) * i;
			float speed = 0.5f;
			Vector3 rv = { std::sinf(angle) * speed, 0.0f, std::cosf(angle) * speed };
			GPUParticleManager::GetInstance()->Emit(
				{ pos_.x, pos_.y + 0.1f, pos_.z },
				rv, 0.3f, 0.8f, { 0.3f, 0.75f, 1.0f, 0.85f }
			);
		}
		// 上昇パーティクル（残像のように漂う）
		for (int i = 0; i < 10; i++) {
			Vector3 dv = {
				(rand() % 9 - 4) * 0.04f,
				0.7f + (rand() % 8) * 0.12f,
				(rand() % 9 - 4) * 0.04f
			};
			GPUParticleManager::GetInstance()->Emit(
				{ pos_.x, pos_.y + 0.5f, pos_.z },
				dv, 0.7f, 0.18f, { 0.6f, 0.92f, 1.0f, 0.8f }
			);
		}
		// 消滅フラッシュ（白くぽん）
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.9f, pos_.z },
			{ 0, 0, 0 }, 0.2f, 3.5f, { 0.8f, 0.95f, 1.0f, 0.9f }
		);
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.9f, pos_.z },
			{ 0, 0, 0 }, 0.12f, 1.8f, { 1.0f, 1.0f, 1.0f, 1.0f }
		);

		isFinished_ = true;
		return;
	}

	// =========================================
	// 被弾リアクション（NotifyHit() が呼ばれたフレーム）
	// =========================================
	if (isBeingHit_) {
		isBeingHit_ = false;

		// 衝撃スパーク（20個）
		for (int i = 0; i < 20; i++) {
			Vector3 sv = {
				(rand() % 21 - 10) * 0.2f,
				0.1f + (rand() % 8) * 0.1f,
				(rand() % 21 - 10) * 0.2f
			};
			Vector3 sp = {
				pos_.x + (rand() % 7 - 3) * 0.12f,
				pos_.y + 0.5f + (rand() % 6) * 0.2f,
				pos_.z + (rand() % 7 - 3) * 0.12f
			};
			GPUParticleManager::GetInstance()->Emit(sp, sv, 0.35f, 0.2f, { 0.5f, 0.88f, 1.0f, 1.0f });
		}
		// 衝撃リング（8個）
		for (int i = 0; i < 8; i++) {
			float angle = (3.14159f * 2.0f / 8.0f) * i;
			float speed = 0.4f;
			Vector3 rv = { std::sinf(angle) * speed, 0.0f, std::cosf(angle) * speed };
			GPUParticleManager::GetInstance()->Emit(
				{ pos_.x, pos_.y + 0.8f, pos_.z },
				rv, 0.25f, 0.65f, { 0.35f, 0.8f, 1.0f, 0.9f }
			);
		}
		// 被弾フラッシュ（白く点滅）
		GPUParticleManager::GetInstance()->Emit(
			{ pos_.x, pos_.y + 0.9f, pos_.z },
			{ 0, 0, 0 }, 0.1f, 2.0f, { 1.0f, 1.0f, 1.0f, 0.85f }
		);
	}

	// =========================================
	// 通常オーラ（毎フレーム）
	// =========================================
	if (model_) {
		model_->SetIsWalking(false);
		model_->SetTranslation(pos_);
		model_->Update();

		// 周囲をただよう青白パーティクル（5個/frame）
		for (int i = 0; i < 5; i++) {
			float angle = static_cast<float>(rand() % 628) * 0.01f;
			float radius = 0.3f + static_cast<float>(rand() % 6) * 0.1f;
			Vector3 emitPos = {
				pos_.x + std::cosf(angle) * radius,
				pos_.y + 0.4f + static_cast<float>(rand() % 10) * 0.18f,
				pos_.z + std::sinf(angle) * radius
			};
			Vector3 vel = {
				std::cosf(angle) * 0.04f,
				0.1f + static_cast<float>(rand() % 5) * 0.05f,
				std::sinf(angle) * 0.04f
			};
			float life = 0.35f + static_cast<float>(rand() % 3) * 0.1f;
			float sc = 0.1f + static_cast<float>(rand() % 4) * 0.05f;
			GPUParticleManager::GetInstance()->Emit(emitPos, vel, life, sc, { 0.5f, 0.85f, 1.0f, 0.75f });
		}

		// 8フレームごとにグロウパルス
		if ((duration_ - timer_) % 8 == 0) {
			GPUParticleManager::GetInstance()->Emit(
				{ pos_.x, pos_.y + 0.9f, pos_.z },
				{ 0, 0, 0 }, 0.12f, 1.8f, { 0.55f, 0.9f, 1.0f, 0.45f }
			);
		}
	}
}

void DecoyEffect::Draw() {
	if (!isFinished_ && model_) {
		model_->Draw();
	}
}