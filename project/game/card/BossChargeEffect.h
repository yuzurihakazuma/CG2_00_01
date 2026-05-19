#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/collision/Collision.h" // 突進先の壁判定に使う
#include <memory>

// ボス用の突進攻撃エフェクト
// effect 側で発動元ボス本人を前進させる
class BossChargeEffect : public ICardEffect {
public:
	// コンストラクタでCSVのeffectValueを受け取る
	BossChargeEffect(int damage) : damage_(damage) {}

	// 初期化
	// 分裂ボス時に、どの個体が突進したかを保持する
	void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

	// 更新
	void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level) override;

	// 描画
	void Draw() override;

	// 終了判定
	bool IsFinished() const override { return isFinished_; }

private:
	// 発動元のボス本人
	// 分裂ボス時に左右どちらを前進させるかを失わないようにする
	Boss* casterBoss_ = nullptr;

	// 見た目用オブジェクト
	std::unique_ptr<Obj3d> obj_ = nullptr;

	// 現在位置
	Vector3 pos_ = { 0.0f, 0.0f, 0.0f };

	// 突進方向
	Vector3 direction_ = { 0.0f, 0.0f, 0.0f };

	// 見た目サイズ
	Vector3 scale_ = { 2.8f, 1.4f, 3.8f };

	// ダメージ量
	int damage_ = 0;

	// 残りフレーム
	int timer_ = 0;

	// 突進時間
	int duration_ = 18;

	// 1回でも当たったか
	bool hasHit_ = false;

	// 終了したか
	bool isFinished_ = false;

	// 突進速度
	float speed_ = 0.55f;

	// 命中半径
	float hitRadius_ = 2.6f;
};
