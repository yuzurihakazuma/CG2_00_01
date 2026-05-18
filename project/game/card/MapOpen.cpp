#include "MapOpen.h"
#include "game/map/Minimap.h"

void MapOpen::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	// 使った瞬間に、ミニマップを全開放する！
	if (minimap_) {
		minimap_->RevealAllMap();
	}
	isFinished_ = true;
}

void MapOpen::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) {
}

void MapOpen::Draw() {
}
