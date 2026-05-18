#pragma once
#include "game/card/ICardEffect.h"

class Minimap;

class MapOpen : public ICardEffect {
public:
	// コンストラクタで Minimap を受け取れるようにする
	MapOpen(Minimap *minimap) : minimap_(minimap) {}

	// ICardEffectの必須関数
	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) override;
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) override;
	void Draw() override;
	bool IsFinished() const override { return isFinished_; }
private:
	Minimap *minimap_ = nullptr;
	bool isFinished_ = false;
};

