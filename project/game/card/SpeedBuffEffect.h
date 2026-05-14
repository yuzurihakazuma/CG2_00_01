#pragma once
#include "game/card/ICardEffect.h"



class SpeedBuffEffect : public ICardEffect {
public:

	// コンストラクタで倍率を受け取る
	SpeedBuffEffect(float multiplier) : multiplier_(multiplier) {}

	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPLayerCaster, Camera *camera)override;

	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss,  const Vector3 &bossPos, const LevelData &level)override;

	// 描画
	void Draw()override;

	bool IsFinished()const override { return isFinished_; }


private:

	float speedRatio_ = 1.5f;   // 移動速度の倍率

	float multiplier_ = 1.0f; // 速度倍率
	bool isPlayerCaster_ = true;
	bool isFinished_ = false;
	int timer_ = 0; // バフの経過時間を管理するタイマー

	int durationTimer_ = 300;   // バフの持続時間（例: 5秒間 = 60fps * 5）
	Vector3 currentPos_ = { 0,0,0 };

};

