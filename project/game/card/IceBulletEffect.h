#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class IceBulletEffect : public ICardEffect {
public:
	// 効果開始
	void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

	// 毎フレーム更新
	void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;

	// 描画
	void Draw() override;

	// 終了判定
	bool IsFinished() const override { return isFinished_; }

private:
	std::unique_ptr<Obj3d> indicatorObj_ = nullptr; // 範囲インジケーター（危険表示）

	Vector3 pos_ = { 0.0f, 0.0f, 0.0f };      // 魔法の中心位置

	bool isPlayerCaster_ = true;
	bool isFinished_ = false;

	int delayTimer_ = 0;      //  発動までの準備時間（予兆・クールタイム）
	int timer_ = 0;           // 吹雪が降る時間
	int startTimer_ = 0;      // 開始時のタイマー（フェードアウト用）

	float range_ = 4.0f;      // 吹雪が降る円の半径
	int damage_ = 1;          //  ダメージを 1 に変更
};
