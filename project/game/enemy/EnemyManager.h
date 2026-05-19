#pragma once
#define NOMINMAX
#include "game/enemy/Enemy.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include <vector>
#include <memory>
#include <unordered_map>

class Player;
class Camera;
class SpawnManager;
class MapManager;
class CardUseSystem;
class CardPickupManager;
class Boss;
class Minimap;

class EnemyManager {
public:

	// 初期化
	void Initialize();

	// 更新
	void Update(Player *player, CardPickupManager *cardPickupManager, MapManager* mapManager,Boss *boss, const Vector3 &targetPos);

	// 描画
	void Draw(Camera *camera, Minimap *minimap = nullptr);

	// ボスの雑魚敵召喚
	void SpawnBossMinions(int spawnCount, const Vector3 &summonCenter, Camera *camera);

	// 通常の敵をランダムに配置する
	void SpawnEnemiesRandom(int enemyCount, int margin, SpawnManager *spawnManager, MapManager* mapManager, const Vector3 &playerPos, Camera *camera);

	// 全ての敵を消す
	void Clear();

	// 当たり判定のチェック
	void CheckCollisions(Player* player, MapManager* mapManager);

	// 敵のリストを取得
	const std::vector<std::unique_ptr<Enemy>> &GetEnemies() const { return enemies_; }

	// 指定したワールド座標に敵をスポーンさせる
	void SpawnEnemyAt(const Vector3& worldPos, Camera* camera, int floor = 1);

private:
	struct EnemyVisual {
		std::unique_ptr<Obj3d> obj;
		std::unique_ptr<SkinnedObj3d> skinned;
		std::unique_ptr<Obj3d> cardRing;
		std::unique_ptr<Obj3d> cardRingFill;
	};

	EnemyVisual CreateEnemyVisual(Enemy::Type type, Camera* camera) const;
	void UpdateEnemyVisual(EnemyVisual& visual, const Enemy& enemy, const Vector3& previousPosition) const;
	void DrawEnemyVisual(const EnemyVisual& visual) const;

	// 敵のリスト
	std::vector<std::unique_ptr<Enemy>> enemies_;
	std::vector<EnemyVisual> enemyVisuals_;
	std::vector<bool> enemyDeadHandled_;


	// 魔法システムのリストもこちらで持つ
	std::vector<std::unique_ptr<CardUseSystem>> enemyCardSystems_;

	// カードIDとモデルを紐づける
	std::unordered_map<int, std::unique_ptr<Obj3d>> castCardObjs_;
};

