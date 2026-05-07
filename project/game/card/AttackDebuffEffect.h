#pragma once
#include "game/card/ICardEffect.h"
class AttackDebuffEffect : public ICardEffect {
public:
	AttackDebuffEffect(int durationFrames) : duration_(durationFrames) {}

	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera) override;
	
	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss,  const Vector3 &bossPos, const LevelData &level) override;
	
	//　描画
	void Draw() override {}
	bool IsFinished() const override { return isFinished_; }

private:
	int duration_ = 300;
	bool isPlayerCaster_ = true;
	bool isFinished_ = false;

	int timer_ = 0; //  エフェクト表示用のタイマーを追加
	Vector3 casterPos_ = { 0,0,0 }; //  発生位置を保存

};

