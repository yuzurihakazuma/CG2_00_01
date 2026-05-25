#pragma once
#include "game/card/ICardEffect.h"
#include "engine/camera/Camera.h"

class FireballEffect : public ICardEffect{
public:

	// コンストラクタでCSVのダメージを受け取る
	FireballEffect(int damage) : damage_(damage) {}

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
	std::unique_ptr<Obj3d> obj_ = nullptr;    // 表示用オブジェクト
	Vector3 pos_ = { 0.0f, 0.0f, 0.0f };      // 位置
	Vector3 velocity_ = { 0.0f, 0.0f, 0.0f }; // 移動速度
	Vector3 scale_ = { 0.8f, 0.8f, 0.8f };    // 大きさ

	Camera* camera_ = nullptr;                // スクリーンUV変換用
	bool isPlayerCaster_ = true;              // 使用者がプレイヤーか
	bool isFinished_ = false;                 // 終了したか

	float rotAngle_ = 0.0f;                   // 炎のカードエフェクト
	int sparkTimer_ = 0;                    // 火の粉のタイマー

	// ダメージを保存しておく変数
	int damage_ = 0;

};
