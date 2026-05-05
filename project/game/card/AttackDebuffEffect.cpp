#include "AttackDebuffEffect.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"

void AttackDebuffEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera) {
	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;
}

void AttackDebuffEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss,  const Vector3 &bossPos, const LevelData &level) {
	if (isFinished_) {
		return;
	}

	// プレイヤーが使った場合、敵全員にデバフを付与
	if (isPlayerCaster_) {

		if (boss && !boss->IsDead()) {
			boss->ApplyAttackDebuff(duration_);  // ※Bossクラスに関数を作っておく必要があります
		}

		if (enemyManager) {
			for (auto &enemy : enemyManager->GetEnemies()) {
				if (enemy && !enemy->IsDead()) {
					enemy->ApplyAttackDebuff(duration_); // ※Enemyクラスに関数を作っておく必要があります
				}
				
			}
		}
	}

	isFinished_ = true; // 1フレームで終了
}
