#pragma once
#include "engine/math/VectorMath.h"
#include "game/map/MapManager.h"

// 前方宣言
class  Camera;
class Player;
class EnemyManager;
class Boss;

// 全てのカード効果のベースとなるクラス
class ICardEffect {
public:
	virtual ~ICardEffect() = default;

	// カード効果の初期化処理
	virtual void Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss) = 0;

	// 毎フレームの更新処理
	virtual void Update(Player *player, EnemyManager *enemyManager, Boss *boss, const Vector3 &bossPos, const LevelData &level) = 0;

	// 描画処理
	virtual void Draw() = 0;

	// このカードの演出が終わったかどうか（終わったらシステムから削除するため）
	virtual bool IsFinished() const = 0;
};