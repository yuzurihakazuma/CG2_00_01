#pragma once
#include "game/card/ICardEffect.h"
#include "engine/3d/obj/Obj3d.h"
#include <memory>

class BossClawEffect : public ICardEffect {
public:

	// コンストラクタでダメージ(CSVのeffectValue)を受け取る
	BossClawEffect(int damage) : damage_(damage) {}

	// 初期化
// 分裂ボス時に、どの個体が発動した攻撃かを受け取れる形にしておく
	void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;


	// 更新
	void Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) override;

	// 描画
	void Draw() override;

	bool IsFinished() const override { return isFinished_; }

private:

	// エフェクトの状態（フェーズ）
	enum class Phase {
		Horizontal, // 横
		Vertical    // 縦
	};

	std::unique_ptr<Obj3d> obj_ = nullptr;
	Vector3 pos_ = { 0.0f, 0.0f, 0.0f };
	Vector3 scale_ = { 1.5f,0.2f,1.5f }; // プレイヤーの爪と同じサイズ

	int damage_ = 20;     // ダメージ量
	int timer_ = 0;       // 演出の進行タイマー
	bool hasHit_ = false; // 現在の攻撃が当たったかどうか
	bool isFinished_ = false;

	float casterYaw_ = 0.0f;
	Vector3 casterPos_ = { 0.0f, 0.0f, 0.0f };

	// この攻撃を発動したボス本人
// 今回の爪攻撃では未使用だが、全ボス攻撃クラスで受け口を統一するため保持する
	Boss* casterBoss_ = nullptr;

};

