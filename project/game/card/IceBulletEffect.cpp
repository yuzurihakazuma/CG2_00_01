#include "IceBulletEffect.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "game/card/BossTargetUtils.h"
#include "engine/math/VectorMath.h"
#include "engine/collision/Collision.h"
#include "engine/particle/GPUParticleManager.h" 
#include <cmath>

using namespace VectorMath;

void IceBulletEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;

	delayTimer_ = 30; // 0.5秒間（30フレーム）の「発動前クールタイム（予兆）」を作る
	timer_ = 60;      // その後、1秒間（60フレーム）雪を降らせる
	startTimer_ = timer_;

	// 魔法陣の中心を少し前(5.0f)に設置
	Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };
	pos_ = {
		casterPos.x + forward.x * 5.0f,
		casterPos.y, // 地面
		casterPos.z + forward.z * 5.0f
	};

	// 範囲インジケーター（危険表示）を作成
	indicatorObj_ = Obj3d::Create("sphere");
	if (indicatorObj_) {
		indicatorObj_->SetCamera(camera);
		indicatorObj_->SetTranslation(pos_);
		indicatorObj_->SetScale({ range_, 0.05f, range_ });

		Model* model = indicatorObj_->GetModel();
		if (model) {
			model->SetTexture("resources/white1x1.png");
			Model::Material* material = model->GetMaterial();
			if (material) {
				material->color = { 1.0f, 0.0f, 0.0f, 0.1f };
				material->emissive = 1.0f;
			}
		}
		indicatorObj_->Update();
	}
}

void IceBulletEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {

	if (isFinished_) return;

	// ==========================================
	// 🌟 発動前の予兆（クールタイム）処理
	// ==========================================
	if (delayTimer_ > 0) {
		delayTimer_--;

		if (indicatorObj_) {
			Model* model = indicatorObj_->GetModel();
			if (model && model->GetMaterial()) {
				Model::Material* material = model->GetMaterial();
				float progress = 1.0f - (static_cast<float>(delayTimer_) / 30.0f);
				material->color.w = 0.1f + (progress * 0.4f);
			}
			indicatorObj_->Update();
		}
		return;
	}

	// ==========================================
	// ❄️ ここから下が実際の発動（氷が降る）処理
	// ==========================================
	timer_--;
	if (timer_ <= 0) {
		isFinished_ = true;
		return;
	}

	if (indicatorObj_) {
		Model* model = indicatorObj_->GetModel();
		if (model && model->GetMaterial()) {
			Model::Material* material = model->GetMaterial();
			float alpha = static_cast<float>(timer_) / static_cast<float>(startTimer_) * 0.5f;
			material->color.w = alpha;
		}
		indicatorObj_->Update();
	}

	// 1. パーティクル：円の範囲に上から雪と氷を降らせる
	for (int i = 0; i < 15; i++) {
		float angle = static_cast<float>(rand()) / RAND_MAX * 3.141592f * 2.0f;
		float r = (static_cast<float>(rand()) / RAND_MAX) * range_;

		Vector3 pPos = {
			pos_.x + std::sinf(angle) * r,
			pos_.y + 3.0f + (rand() % 10 * 0.1f),
			pos_.z + std::cosf(angle) * r
		};

		Vector3 pVel = {
			(rand() % 11 - 5) * 0.02f,
			-3.0f - static_cast<float>(rand() % 20) * 0.1f,
			(rand() % 11 - 5) * 0.02f
		};

		Vector4 color = { 0.5f, 0.8f, 1.0f, 0.8f };
		float scale = 0.15f + static_cast<float>(rand() % 4) * 0.1f;

		if (rand() % 10 == 0) {
			color = { 1.0f, 1.0f, 1.0f, 1.0f };
			scale = 0.4f + static_cast<float>(rand() % 4) * 0.1f;
		}

		GPUParticleManager::GetInstance()->Emit(pPos, pVel, 1.0f, scale, color);
	}

	// ==========================================
	// ❄️ 2. 円形の当たり判定
	// ==========================================
	// 🌟 修正: 連続ヒットをやめ、氷が降り始めた最初の瞬間（timer_ が 59 になった時）に1回だけ判定する！
	if (timer_ == 59) {
		if (isPlayerCaster_) {
			// --- 雑魚敵への判定 ---
			if (enemyManager) {
				for (auto& enemy : enemyManager->GetEnemies()) {
					if (enemy && !enemy->IsDead()) {
						Vector3 ePos = enemy->GetPosition();
						Vector3 diff = { ePos.x - pos_.x, 0.0f, ePos.z - pos_.z };

						if (Length(diff) < range_) {
							enemy->TakeDamage(damage_); // ダメージ1
							enemy->Freeze(120); // 凍結
						}
					}
				}
			}
			// --- ボスへの判定 ---
			Boss* hitBoss = BossTargetUtils::FindClosestAliveBossInRange(pos_, range_, boss, extraBoss);
			if (hitBoss) {
				hitBoss->TakeDamage(damage_); // ダメージ1
				hitBoss->Freeze(60); // 凍結
			}
		}
		// --- 敵が使った場合（プレイヤーへの判定） ---
		else {
			if (player && !player->IsDead()) {
				Vector3 pPos = player->GetPosition();
				Vector3 diff = { pPos.x - pos_.x, 0.0f, pPos.z - pos_.z };

				if (Length(diff) < range_) {
					int dmg = boss && boss->IsAttackDebuffed() ? damage_ / 2 : damage_;
					player->TakeDamage(dmg, pos_);
				}
			}
		}
	}
}

void IceBulletEffect::Draw() {
	if (indicatorObj_ && !isFinished_) {
		indicatorObj_->Draw();
	}
}
