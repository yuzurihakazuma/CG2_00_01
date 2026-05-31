#pragma once
#include "game/card/ICardEffect.h"



class SpeedBuffEffect : public ICardEffect {
public:

	// 通常コンストラクタ
	SpeedBuffEffect(float multiplier) : multiplier_(multiplier) {}
	// 復元用コンストラクタ（階層移動後に残り時間を引き継ぐ）
	SpeedBuffEffect(float multiplier, int remainingTimer)
	    : multiplier_(multiplier), durationTimer_(remainingTimer) {}

	// 残り時間・倍率の取得（ResetBattleDebug で保存するため）
	int GetRemainingTimer() const { return durationTimer_; }
	float GetMultiplier() const { return multiplier_; }

	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPLayerCaster, Camera *camera, Boss* casterBoss)override;

	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level)override;

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

