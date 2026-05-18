#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class ShieldEffect : public ICardEffect {
public:
	
	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss)override;

	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level)override;

	// 描画
	void Draw()override;

	bool IsFinished()const override { return isFinished_; }

private:
	std::unique_ptr<Obj3d> obj_ = nullptr;
	Vector3 scale_ = { 1.5f,1.5f,1.5f }; // シールドの大きさ
	bool  isFirstFrame_ = false;
	bool isPlayerCaster_ = true;
	bool isFinished_ = false;

	float rotAngle_ = 0.0f;
};

