#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class ClawEffect : public ICardEffect{
public:

	// コンストラクタでダメージ(csvから読み込んだ値)を受け取る
	ClawEffect(int damage) : damage_(damage) {}

	// ImGuiで直接いじるためのデバッグ用ダメージ変数
	static int debugBaseDamage;

	// 初期化
	void Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss)override;

	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level)override;

	// 描画
	void Draw()override;

	bool IsFinished()const override { return isFinished_; }


private:

	std::unique_ptr<Obj3d> obj_ = nullptr;
	Vector3 pos_ = { 0.0f,0.0f,0.0f };
	Vector3 scale_ = { 20.0f, 20.0f, 20.0f };

	int damage_ = 10;  // ダメージ量
	int timer_ = 0;    // 演出の進行タイマー
	bool hasHit_ = false; // 現在の攻撃が当たったかどうか
	bool hasPlayedFirstSlashSE_ = false;
	bool hasPlayedSecondSlashSE_ = false;

	bool isPlayerCaster_ = true;
	bool isFinished_ = false;

	float casterYaw_ = 0.0f;

	Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };
	Camera* camera_ = nullptr;

};

