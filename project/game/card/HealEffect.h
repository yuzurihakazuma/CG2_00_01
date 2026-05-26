#pragma once
#include "game/card/ICardEffect.h"

class HealEffect : public ICardEffect{
public:

	// コンストラクタで回復量を受け取る
	HealEffect(int healAmount) : healAmount_(healAmount) {}

	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss)override;

	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level)override;

	// 描画
	void Draw()override;

	bool IsFinished() const override { return isFinished_; }

private:
	int healAmount_ = 5; // 回復量
	bool isPlayerCaster_ = true;
	bool isFinished_ = false;

	bool hasHealed_ = false;              // 回復処理を一度だけ行うためのフラグ

	int effectTimer_ = 0;                 // エフェクトの経過フレーム
	const int kEffectDuration = 20;       // エフェクトを出し続ける長さ（20フレーム ＝ 約0.3秒）
	Vector3 currentPos_ = { 0.0f, 0.0f, 0.0f }; // 毎フレーム更新する発生位置
	Camera* camera_ = nullptr;

};

