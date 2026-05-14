#pragma once
#include "game/card/ICardEffect.h"

class CostBoostEffect : public ICardEffect {
public:

	// コンストラクタ（CSVのeffectValueで「最大コストの増加量」を受け取る想定）
	CostBoostEffect(int costBoostAmount) : costBoostAmount_(costBoostAmount) {}

	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) override;
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, const Vector3 &bossPos, const LevelData &level) override;
	void Draw() override {}
	bool IsFinished() const override { return isFinished_; }
	
private:
	int costBoostAmount_ = 3; // 増やす最大コストの量
	int duration_ = 600; // 効果時間
	int timer_ = 0; // 効果の経過時間

	bool isPlayerCaster_ = true;
	bool isFinished_ = false;
};

