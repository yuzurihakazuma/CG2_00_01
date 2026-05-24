#pragma once
#include "game/card/ICardEffect.h"

class FistEffect : public ICardEffect{
public:

	FistEffect(int damage) : damage_(damage){}

	// 効果開始
	void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) override;

	// 毎フレーム更新
	void Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss,
		const Vector3& bossPos, const LevelData& level) override;

	// 描画
	void Draw() override;

	// 効果終了フラグ
	bool IsFinished() const override{ return isFinished_; }

private:
	std::unique_ptr<Obj3d> obj_ = nullptr;   // 表示用オブジェクト

	Vector3 pos_ = { 0.0f, 0.0f, 0.0f };     // 現在の位置
	Vector3 startPos_ = { 0.0f, 0.0f, 0.0f };// 突き出しの開始位置
	float casterYaw_ = 0.0f;                 // 殴る向き

	Vector3 velocity_ = { 0.0f, 0.0f, 0.0f };
	Vector3 scale_ = { 1.0f, 1.0f, 1.0f };   // モデルの形を崩さないように1.0に

	bool isPlayerCaster_ = true;             // 使用者がプレイヤーか
	bool isFinished_ = false;                // 終了したか
	bool hasHit_ = false;                    // 命中済みフラグ
	int punchTimer_ = 0;                     // 効果時間

	int damage_ = 0;                         // ダメージを保存しておく変数
};
