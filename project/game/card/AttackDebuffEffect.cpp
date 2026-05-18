#include "AttackDebuffEffect.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "engine/particle/GPUParticleManager.h"
#include "game/player/Player.h"
#include "game/card/BossTargetUtils.h"
#include <cmath> 

void AttackDebuffEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	// この魔法はプレイヤー専用
	isPlayerCaster_ = true;
	isFinished_ = false;

	// タイマーを「効果時間(duration_)」と同じにする
	timer_ = duration_;
	casterPos_ = casterPos;

	
}

void AttackDebuffEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) {
	if (isFinished_) return;

	// 1フレーム目（発動した瞬間）に、敵全員にデバフを付与する
	if (timer_ == duration_) {
		if (enemyManager) {
			for (auto& enemy : enemyManager->GetEnemies()) {
				if (enemy && !enemy->IsDead()) {
					enemy->ApplyAttackDebuff(duration_);
				}
			}
		}
		// 分裂戦では生きている個体それぞれへデバフを入れる
		BossTargetUtils::ApplyAttackDebuffToAliveBosses(duration_, boss, extraBoss);
	}

	// 継続エフェクト：処理を軽くするため、3フレームに1回のペースで敵の体から毒を発生させる
	if (timer_ % 3 == 0) {
		Vector4 poisonColor1 = { 0.5f, 0.0f, 0.8f, 0.8f }; // 毒々しい紫
		Vector4 poisonColor2 = { 0.2f, 0.8f, 0.2f, 0.8f }; // 毒々しい緑

		// --- 雑魚敵全員から大きな毒の泡を出す ---
		if (enemyManager) {
			for (auto& enemy : enemyManager->GetEnemies()) {
				if (enemy && !enemy->IsDead() && enemy->IsAttackDebuffed()) {
					Vector3 ePos = enemy->GetPosition();
					// 体の周囲からフワッと上に昇る
					Vector3 pPos = { ePos.x + (rand() % 11 - 5) * 0.08f, ePos.y + 0.5f, ePos.z + (rand() % 11 - 5) * 0.08f };
					Vector3 pVel = { 0.0f, 0.03f + (rand() % 5) * 0.01f, 0.0f };
					Vector4 color = (rand() % 2 == 0) ? poisonColor1 : poisonColor2;

					// スケールを 0.1f -> 0.3f〜0.4f に拡大。寿命も 0.8f にして頭上までしっかり昇らせる
					float scale = 0.3f + (rand() % 10) * 0.01f;
					GPUParticleManager::GetInstance()->Emit(pPos, pVel, 0.8f, scale, color);
				}
			}
		}

		// --- ボスからはさらに巨大な毒の泡を出す ---
		Boss* debuffedBosses[2] = { boss, extraBoss };
		for (Boss* debuffedBoss : debuffedBosses) {
			if (!debuffedBoss || debuffedBoss->IsDead() || !debuffedBoss->IsAttackDebuffed()) {
				continue;
			}

			Vector3 bPos = debuffedBoss->GetPosition();
			Vector3 pPos = { bPos.x + (rand() % 21 - 10) * 0.15f, bPos.y + 1.0f, bPos.z + (rand() % 21 - 10) * 0.15f };
			Vector3 pVel = { 0.0f, 0.04f + (rand() % 5) * 0.01f, 0.0f };
			Vector4 color = (rand() % 2 == 0) ? poisonColor1 : poisonColor2;

			// ボス用はさらに大きく 0.4f〜0.5f くらいにし、1秒間(1.0f)表示する
			float scale = 0.4f + (rand() % 10) * 0.01f;
			GPUParticleManager::GetInstance()->Emit(pPos, pVel, 1.0f, scale, color);
		}
	}

	timer_--;

	// 効果時間が切れたらエフェクト終了
	if (timer_ <= 0) {
		isFinished_ = true;
	}
}
