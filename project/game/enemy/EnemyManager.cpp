#include "game/enemy/EnemyManager.h"
#include "game/enemy/Boss.h"
#include "game/player/Player.h"
#include "game/spawn/SpawnManager.h"
#include "game/card/CardUseSystem.h" 
#include "game/card/CardPickupManager.h"
#include "game/card/CardDatabase.h"
#include "engine/utils/Level/LevelEditor.h" 
#include "engine/math/VectorMath.h"
#include "game/map/Minimap.h"
#include "game/map/MapManager.h"

#include <cmath>
#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <queue>
#include <random>
#include <vector>
#include "engine/particle/GPUParticleManager.h"

using namespace VectorMath;

namespace {
struct EnemyTypeWeight {
	Enemy::Type type;
	int weight;
};

Enemy::Type GetRandomEnemyTypeForFloor(int floor) {
	static const EnemyTypeWeight floor1[] = {
		{ Enemy::Type::Normal, 70 },
		{ Enemy::Type::Fast, 30 },
	};
	static const EnemyTypeWeight floor2[] = {
		{ Enemy::Type::Normal, 45 },
		{ Enemy::Type::Fast, 30 },
		{ Enemy::Type::Ranged, 25 },
	};
	static const EnemyTypeWeight floor3Plus[] = {
		{ Enemy::Type::Normal, 35 },
		{ Enemy::Type::Fast, 25 },
		{ Enemy::Type::Ranged, 25 },
		{ Enemy::Type::Heavy, 15 },
	};

	const EnemyTypeWeight* weights = floor1;
	int weightCount = static_cast<int>(std::size(floor1));
	if (floor >= 3) {
		weights = floor3Plus;
		weightCount = static_cast<int>(std::size(floor3Plus));
	}
	else if (floor == 2) {
		weights = floor2;
		weightCount = static_cast<int>(std::size(floor2));
	}

	int totalWeight = 0;
	for (int i = 0; i < weightCount; ++i) {
		totalWeight += weights[i].weight;
	}

	static std::random_device rd;
	static std::mt19937 mt(rd());
	std::uniform_int_distribution<int> dist(1, totalWeight);
	int roll = dist(mt);

	for (int i = 0; i < weightCount; ++i) {
		roll -= weights[i].weight;
		if (roll <= 0) {
			return weights[i].type;
		}
	}

	return Enemy::Type::Normal;
}

bool IsWalkableTile(const LevelData& level, int x, int z) {
	if (x < 0 || x >= level.width || z < 0 || z >= level.height) {
		return false;
	}

	int tile = level.tiles[z][x];
	return tile == 0 || tile == 3;
}

bool HasClearStraightPath(const LevelData& level, const Vector3& from, const Vector3& to) {
	if (level.tileSize <= 0.0f || level.tiles.empty()) {
		return false;
	}

	float dx = to.x - from.x;
	float dz = to.z - from.z;
	float distance = std::sqrt(dx * dx + dz * dz);
	int steps = (std::max)(1, static_cast<int>(std::ceil(distance / (level.tileSize * 0.25f))));

	for (int i = 0; i <= steps; ++i) {
		float t = static_cast<float>(i) / static_cast<float>(steps);
		float x = from.x + dx * t;
		float z = from.z + dz * t;
		int tileX = static_cast<int>(std::round(x / level.tileSize));
		int tileZ = static_cast<int>(std::round(z / level.tileSize));

		if (!IsWalkableTile(level, tileX, tileZ)) {
			return false;
		}
	}

	return true;
}

int TileIndex(const LevelData& level, int x, int z) {
	return z * level.width + x;
}

std::pair<int, int> WorldToTile(const LevelData& level, const Vector3& pos) {
	return {
		static_cast<int>(std::round(pos.x / level.tileSize)),
		static_cast<int>(std::round(pos.z / level.tileSize))
	};
}

Vector3 TileToWorld(const LevelData& level, int x, int z, float y) {
	return {
		static_cast<float>(x) * level.tileSize,
		y,
		static_cast<float>(z) * level.tileSize
	};
}

bool FindNextPathTarget(const LevelData& level, const Vector3& from, const Vector3& to, Vector3& outTarget) {
	if (level.tileSize <= 0.0f || level.width <= 0 || level.height <= 0 || level.tiles.empty()) {
		return false;
	}

	auto [startX, startZ] = WorldToTile(level, from);
	auto [goalX, goalZ] = WorldToTile(level, to);

	if (!IsWalkableTile(level, startX, startZ) || !IsWalkableTile(level, goalX, goalZ)) {
		return false;
	}

	const int tileCount = level.width * level.height;
	const int unreachable = (std::numeric_limits<int>::max)();
	std::vector<int> cost(tileCount, unreachable);
	std::vector<int> parent(tileCount, -1);
	std::priority_queue<
		std::pair<int, int>,
		std::vector<std::pair<int, int>>,
		std::greater<std::pair<int, int>>
	> open;

	int startIndex = TileIndex(level, startX, startZ);
	int goalIndex = TileIndex(level, goalX, goalZ);

	auto Heuristic = [&](int x, int z) {
		return (std::abs(goalX - x) + std::abs(goalZ - z)) * 10;
	};

	cost[startIndex] = 0;
	open.push({ Heuristic(startX, startZ), startIndex });

	const int dirs[4][2] = {
		{ 1, 0 },
		{ -1, 0 },
		{ 0, 1 },
		{ 0, -1 },
	};

	while (!open.empty()) {
		int currentIndex = open.top().second;
		open.pop();

		if (currentIndex == goalIndex) {
			break;
		}

		int currentX = currentIndex % level.width;
		int currentZ = currentIndex / level.width;

		for (const auto& dir : dirs) {
			int nextX = currentX + dir[0];
			int nextZ = currentZ + dir[1];
			if (!IsWalkableTile(level, nextX, nextZ)) {
				continue;
			}

			int nextIndex = TileIndex(level, nextX, nextZ);
			int nextCost = cost[currentIndex] + 10;
			if (nextCost >= cost[nextIndex]) {
				continue;
			}

			cost[nextIndex] = nextCost;
			parent[nextIndex] = currentIndex;
			open.push({ nextCost + Heuristic(nextX, nextZ), nextIndex });
		}
	}

	if (cost[goalIndex] == unreachable) {
		return false;
	}

	int nextIndex = goalIndex;
	while (parent[nextIndex] != -1 && parent[nextIndex] != startIndex) {
		nextIndex = parent[nextIndex];
	}

	if (nextIndex == startIndex) {
		outTarget = to;
		return true;
	}

	int nextX = nextIndex % level.width;
	int nextZ = nextIndex / level.width;
	outTarget = TileToWorld(level, nextX, nextZ, from.y);
	return true;
}

Vector4 GetEnemyDisplayColor(const Enemy& enemy) {
	Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	switch (enemy.GetCurrentAttackRangeType()) {
	case CardAttackRangeType::Melee:
		baseColor = { 1.0f, 0.45f, 0.35f, 1.0f };
		break;
	case CardAttackRangeType::Mid:
		baseColor = { 0.95f, 0.9f, 0.35f, 1.0f };
		break;
	case CardAttackRangeType::Long:
		baseColor = { 0.35f, 0.75f, 1.0f, 1.0f };
		break;
	default:
		break;
	}

	if (enemy.IsFrozen()) {
		Vector4 freezeColor = { 0.45f, 0.85f, 1.25f, 1.0f };
		return {
			baseColor.x * 0.35f + freezeColor.x * 0.65f,
			baseColor.y * 0.35f + freezeColor.y * 0.65f,
			baseColor.z * 0.35f + freezeColor.z * 0.65f,
			1.0f
		};
	}

	if (enemy.IsAttackDebuffed()) {
		Vector4 debuffColor = { 0.65f, 0.35f, 0.9f, 1.0f };
		return {
			baseColor.x * 0.45f + debuffColor.x * 0.55f,
			baseColor.y * 0.45f + debuffColor.y * 0.55f,
			baseColor.z * 0.45f + debuffColor.z * 0.55f,
			1.0f
		};
	}

	return baseColor;
}
}


void EnemyManager::Initialize() {
	// 敵関連のデータをすべて初期化
	enemies_.clear();
	enemyObjs_.clear();
	enemyDeadHandled_.clear();
	enemyCardSystems_.clear();


	castCardObjs_.clear();
	castCardObjs_[1] = std::unique_ptr<Obj3d>(Obj3d::Create("cardF"));
	castCardObjs_[2] = std::unique_ptr<Obj3d>(Obj3d::Create("cardFire"));
	castCardObjs_[7] = std::unique_ptr<Obj3d>(Obj3d::Create("CardFang"));
	castCardObjs_[10] = std::unique_ptr<Obj3d>(Obj3d::Create("CardClaw"));
}

void EnemyManager::Update(Player *player, CardPickupManager *cardPickupManager, MapManager* mapManager,Boss *boss, const Vector3 &targetPos) {
	if (!player || !cardPickupManager || !mapManager) return;

	const LevelData &level = mapManager->GetLevelData();

	// 敵が壁に触れているかを簡易的に判定する
	auto IsEnemyHitWall = [&](const Vector3& pos) -> bool {
		AABB enemyAABB;
		enemyAABB.min = { pos.x - 0.5f, pos.y - 0.5f, pos.z - 0.5f };
		enemyAABB.max = { pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f };

		int gridX = static_cast<int>(std::round(pos.x / level.tileSize));
		int gridZ = static_cast<int>(std::round(pos.z / level.tileSize));
		int startX = (std::max)(0, gridX - 1);
		int endX = (std::min)(level.width - 1, gridX + 1);
		int startZ = (std::max)(0, gridZ - 1);
		int endZ = (std::min)(level.height - 1, gridZ + 1);

		for (int z = startZ; z <= endZ; z++) {
			for (int x = startX; x <= endX; x++) {
				if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
					continue;
				}

				float worldX = x * level.tileSize;
				float worldZ = z * level.tileSize;
				AABB blockAABB;
				blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
				blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

				if (Collision::IsCollision(enemyAABB, blockAABB)) {
					return true;
				}
			}
		}

		return false;
	};

	// 敵全員分ループ
	for (size_t i = 0; i < enemies_.size(); ++i) {
		auto& enemy = enemies_[i];
		if (!enemy) {
			continue;
		}

		// 死亡時の処理を一度だけ行う
		if (enemy->IsDead()) {
			if (!enemyDeadHandled_[i]) {
				if (player) {
					player->AddExp(enemy->GetExpReward());
				}
				if (enemy->HasUsablePickupCard() && !enemy->IsBossRoomBehavior()) {
					cardPickupManager->AddPickup(enemy->GetPosition(), enemy->GetPickupCard());
					enemy->ClearPickupCard();
				}
				enemyDeadHandled_[i] = true;

				if (enemyCardSystems_[i]) {
					enemyCardSystems_[i]->Reset();
				}
			}
			continue;
		}

		// --- ① 移動前の座標を保存 ---
		Vector3 oldEnemyPos = enemy->GetPosition();

		// --- ② プレイヤーの位置を教える ---
		enemy->SetPlayerPosition(targetPos);
		enemy->SetBossRoomBehavior(mapManager->IsBossMap());
		Vector3 navigationTarget{};
		bool hasNavigationTarget = false;
		bool canSeePlayer = HasClearStraightPath(level, oldEnemyPos, targetPos);
		enemy->SetCanSeePlayer(canSeePlayer);
		if (!canSeePlayer) {
			hasNavigationTarget = FindNextPathTarget(level, oldEnemyPos, targetPos, navigationTarget);
			if (!hasNavigationTarget) {
				navigationTarget = oldEnemyPos;
				hasNavigationTarget = true;
			}
		}
		enemy->SetNavigationTarget(hasNavigationTarget, navigationTarget);

		// --- ③ 近くに落ちているカードを探す（引数のcardPickupManagerを使う） ---
		bool foundCard = false;
		Vector3 nearestCardPos{};
		float nearestCardDist = 99999.0f;

		for (auto &pickup : cardPickupManager->GetPickups()) {
			if (!pickup.isActive) continue;
			Vector3 diff = { pickup.position.x - oldEnemyPos.x, 0.0f, pickup.position.z - oldEnemyPos.z };
			float dist = Length(diff);
			if (dist < 8.0f &&
				dist < nearestCardDist &&
				HasClearStraightPath(level, oldEnemyPos, pickup.position)) {
				nearestCardDist = dist;
				nearestCardPos = pickup.position;
				foundCard = true;
			}
		}

		// --- ④ AI更新 ---
		enemy->SetCardTarget(foundCard, nearestCardPos);
		enemy->Update();

		// --- ⑤ 壁との衝突判定（引数のlevelを使う） ---
		Vector3 enemyPos = enemy->GetPosition();
		AABB enemyAABB;
		enemyAABB.min = { enemyPos.x - 0.5f, enemyPos.y - 0.5f, enemyPos.z - 0.5f };
		enemyAABB.max = { enemyPos.x + 0.5f, enemyPos.y + 0.5f, enemyPos.z + 0.5f };

		int enemyGridX = static_cast<int>(std::round(enemyPos.x / level.tileSize));
		int enemyGridZ = static_cast<int>(std::round(enemyPos.z / level.tileSize));
		int eStartX = (std::max)(0, enemyGridX - 1);
		int eEndX = (std::min)(level.width - 1, enemyGridX + 1);
		int eStartZ = (std::max)(0, enemyGridZ - 1);
		int eEndZ = (std::min)(level.height - 1, enemyGridZ + 1);

		bool isEnemyHit = false;
		for (int z = eStartZ; z <= eEndZ && !isEnemyHit; z++) {
			for (int x = eStartX; x <= eEndX; x++) {
				if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
					continue;
				}

				float worldX = x * level.tileSize;
				float worldZ = z * level.tileSize;
				AABB blockAABB;
				blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
				blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

				if (Collision::IsCollision(enemyAABB, blockAABB)) {
					// 壁押し戻しでは巡回基準まで巻き戻さない
					enemy->SetPositionOnly(oldEnemyPos);
					isEnemyHit = true;
					break;
				}
			}
		}

		// --- ⑥ 見た目（3Dモデル）に座標を反映 ---
		// 壁にぶつかって元の位置へ戻された時は、軸移動や横滑りで抜けられるかを試す
		if (isEnemyHit) {
			Vector3 resolvedPos = enemy->GetPosition();
			if (Length(resolvedPos - oldEnemyPos) <= 0.0001f) {
				Vector3 moveDelta = enemyPos - oldEnemyPos;
				Vector3 slideX = oldEnemyPos;
				Vector3 slideZ = oldEnemyPos;
				slideX.x += moveDelta.x;
				slideZ.z += moveDelta.z;

				bool moved = false;
				if (std::abs(moveDelta.x) > 0.0001f && !IsEnemyHitWall(slideX)) {
					enemy->SetPositionOnly(slideX);
					moved = true;
				}
				else if (std::abs(moveDelta.z) > 0.0001f && !IsEnemyHitWall(slideZ)) {
					enemy->SetPositionOnly(slideZ);
					moved = true;
				}
				else if (Length(moveDelta) > 0.0001f) {
					Vector3 moveDir = Normalize(moveDelta);
					Vector3 sideDirA = { moveDir.z, 0.0f, -moveDir.x };
					Vector3 sideDirB = { -moveDir.z, 0.0f, moveDir.x };
					float sideStep = (std::max)(0.35f, Length(moveDelta));
					Vector3 sidePosA = oldEnemyPos + sideDirA * sideStep;
					Vector3 sidePosB = oldEnemyPos + sideDirB * sideStep;

					if (!IsEnemyHitWall(sidePosA)) {
						enemy->SetPositionOnly(sidePosA);
						moved = true;
					}
					else if (!IsEnemyHitWall(sidePosB)) {
						enemy->SetPositionOnly(sidePosB);
						moved = true;
					}
				}

				if (!moved) {
					enemy->SetPositionOnly(oldEnemyPos);
				}
			}
		}

		if (enemyObjs_[i]) {
			enemyObjs_[i]->SetTranslation(enemy->GetPosition());
			enemyObjs_[i]->SetRotation(enemy->GetRotation());
			enemyObjs_[i]->SetScale(enemy->GetScale());
			enemyObjs_[i]->SetColor(GetEnemyDisplayColor(*enemy));
			enemyObjs_[i]->Update();
		}
		// 敵が「カードを使いたい！」と合図を出した時の処理
		if (enemy->GetCardUseRequest()) {
			bool usedPickupCard = enemy->HasUsablePickupCard() &&
				enemy->GetCurrentUseCard().id == enemy->GetPickupCard().id;

			if (enemyCardSystems_[i]) {
				enemyCardSystems_[i]->UseCard(
					enemy->GetCurrentUseCard(),
					enemy->GetPosition(),
					enemy->GetRotation().y,
					false // プレイヤーではないので false
				);
			}
			// 合図を出したらリセットする
			if (usedPickupCard) {
				enemy->ClearPickupCard();
			}
			enemy->ClearCardUseRequest();
		}

		// 敵の魔法を毎フレーム動かす（飛んでいる弾などの処理）
		if (enemyCardSystems_[i]) {
			// CardUseSystem が EnemyManager を要求するようになったので "this"（自分自身）を渡す！
			enemyCardSystems_[i]->Update(
				player,
				this,      // EnemyManager 自身を渡す
				nullptr,   // ボスのポインタ（雑魚同士やボスに当たらないなら nullptr でOK）
				player->GetPosition(),
				enemy->GetPosition(),
				{ 0.0f, 0.0f, 0.0f }, // bossPos
				mapManager->GetLevelData()
			);
		}

		// 敵がカードを拾う処理（復活！）
		if (!enemy->HasPickupCard()) {
			for (auto &pickup : cardPickupManager->GetPickups()) {
				if (!pickup.isActive) continue;

				Vector3 ePos = enemy->GetPosition();
				Vector3 diff = {
					ePos.x - pickup.position.x,
					ePos.y - pickup.position.y,
					ePos.z - pickup.position.z
				};

				// 距離が近ければ拾う
				if (Length(diff) < 2.0f) {
					Card pickedCard = pickup.card;
					// 敵が使えないカード、または攻撃カードじゃないなら、攻撃カードにすり替える
					if (!pickedCard.canEnemyUse || pickedCard.effectType != CardEffectType::Attack) {
						pickedCard = CardDatabase::GetRandomEnemyUsableCard();
					}
					enemy->SetPickupCard(pickedCard);
					pickup.isActive = false; // マップ上からカードを消す

					Vector3 effectPos = enemy->GetPosition();
					effectPos.y += 0.5f; // 体の中心あたり

					// ① 足元から上に激しく立ち昇る黄金の光柱（適度な量とサイズに）
					for (int i = 0; i < 40; i++) { // 数を40発に
						Vector3 auraPos = {
							effectPos.x + (rand() % 15 - 7) * 0.1f,
							effectPos.y - 0.5f,
							effectPos.z + (rand() % 15 - 7) * 0.1f
						};
						Vector3 auraVel = {
							(rand() % 11 - 5) * 0.01f,
							0.25f + (rand() % 10) * 0.05f,
							(rand() % 11 - 5) * 0.01f
						};
						Vector4 color = (rand() % 2 == 0) ? Vector4{ 1.0f, 0.8f, 0.2f, 1.0f } : Vector4{ 1.0f, 1.0f, 0.6f, 1.0f };

						// 🌟 サイズを 1.0f 〜 1.8f の丁度いい大きさに
						float scale = 1.0f + (rand() % 9) * 0.1f;

						GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.6f, scale, color);
					}

					// ② 横に広がる衝撃波のリング（1段構えでスッキリと）
					for (int i = 0; i < 24; i++) {
						float angle = (3.141592f * 2.0f / 24.0f) * i;
						float speed = 0.4f;
						Vector3 ringVel = { std::sinf(angle) * speed, 0.0f, std::cosf(angle) * speed };
						// 🌟 サイズ 1.5f の分かりやすいリング1つ
						GPUParticleManager::GetInstance()->Emit(effectPos, ringVel, 0.4f, 1.5f, { 1.0f, 0.9f, 0.5f, 1.0f });
					}

					// ③ 周囲に弾け飛ぶ黄金の火花（少量のアクセント）
					for (int i = 0; i < 20; i++) { // 20発に減らす
						Vector3 sparkVel = {
							(rand() % 21 - 10) * 0.1f,
							(rand() % 21 - 5) * 0.1f,
							(rand() % 21 - 10) * 0.1f
						};
						// サイズを 0.5f 〜 0.8f に
						float scale = 0.5f + (rand() % 4) * 0.1f;
						GPUParticleManager::GetInstance()->Emit(effectPos, sparkVel, 0.5f, scale, { 1.0f, 0.6f, 0.0f, 1.0f });
					}

					break; // 1度に拾うのは1枚だけ
				}
			}
		}
	}
	// =========================================================
	// ボスとの押し出し判定
	// =========================================================
	if (boss && !boss->IsDead()) {
		Vector3 bossPos = boss->GetPosition();
		float bossRadius = 3.0f; // ボスの大きさ（適宜調整してください）

		for (auto &enemy : enemies_) {
			if (enemy && !enemy->IsDead()) {
				Vector3 enemyPos = enemy->GetPosition();
				float enemyRadius = 1.0f;

				Vector3 diff = { enemyPos.x - bossPos.x, 0.0f, enemyPos.z - bossPos.z };
				float dist = Length(diff);
				float pushRange = bossRadius + enemyRadius;

				if (dist > 0.01f && dist < pushRange) {
					Vector3 pushDir = Normalize(diff);
					float pushAmount = pushRange - dist;
					enemyPos.x += pushDir.x * pushAmount;
					enemyPos.z += pushDir.z * pushAmount;
					// ボスとの押し分けでも巡回アンカーは維持する
					enemy->SetPositionOnly(enemyPos);
				}
			}
		}
	}
}


void EnemyManager::Draw(Camera* camera, Minimap* minimap) {
	std::vector<Vector3> enemyPositions;

	for (size_t i = 0; i < enemies_.size(); ++i) {
		if (enemies_[i] && !enemies_[i]->IsDead()) {

			// 点滅中は表示するフレームだけ描画
			if (enemyObjs_[i] && enemies_[i]->IsVisible()) {
				enemyObjs_[i]->Draw();
			}

			enemyPositions.push_back(enemies_[i]->GetPosition());
		}
	}

	for (auto& cardSystem : enemyCardSystems_) {
		if (cardSystem) {
			cardSystem->Draw();
		}
	}

	// 詠唱（カード準備）中の、頭上での回転演出
	for (const auto &enemy : enemies_) {
		if (enemy && !enemy->IsDead() && enemy->IsCasting()) {
			Vector3 ePos = enemy->GetPosition();

			// 敵が今使おうとしているカードのIDを取得する
			int currentCardId = enemy->GetCurrentUseCard().id;

			// 辞書の中にそのIDのモデルが存在すれば描画処理を行う
			if (castCardObjs_.count(currentCardId) && castCardObjs_[currentCardId]) {
				// 使うモデルを取り出す
				auto &cardObj = castCardObjs_[currentCardId];

				// --- 演出パラメータの設定 ---
				float height = 3.5f;   // 敵の頭からの高さ
				float timer = static_cast<float>(enemy->GetCastTimer());
				float rotateSpeed = 0.1f; // 回転する速さ

				// 1. 座標の設定
				cardObj->SetCamera(camera);
				cardObj->SetTranslation({
					ePos.x,
					ePos.y + height,
					ePos.z
					});

				// ==========================================
				// ★ ① 表面を描画する
				// ==========================================
				cardObj->SetRotation({
					0.0f,
					timer * rotateSpeed, // Y軸で回転
					0.0f
					});
				cardObj->Update();
				cardObj->Draw();

				// ==========================================
				// ★ ② 裏面を描画する（180度反転させて上書き）
				// ==========================================
				// 180度はラジアン（円周率）で 3.14159f です
				cardObj->SetRotation({
					0.0f,
					(timer * rotateSpeed) + 3.14159f, // Y軸に180度足して裏返す！
					0.0f
					});
				cardObj->Update();
				cardObj->Draw();

				//// 2. 回転の設定
				//cardObj->SetRotation({
				//	0.0f,                // X軸
				//	timer * rotateSpeed, // Y軸で回転
				//	0.0f                 // Z軸
				//	});

				//cardObj->Update();
				//cardObj->Draw();
			}
		}
	}

	if (minimap) {
		minimap->SetEnemyPositions(enemyPositions);
	}
}

void EnemyManager::SpawnBossMinions(int spawnCount, const Vector3 &summonCenter, Camera *camera) {
	// 現在「生きている雑魚敵」の数を数える
	int aliveCount = 0;
	for (const auto &enemy : enemies_) {
		if (enemy && !enemy->IsDead()) {
			aliveCount++;
		}
	}

	// 上限の設定
	int maxMinions = 3; // 出したい上限の数
	if (aliveCount >= maxMinions) {
		return; // すでに上限まで生きているなら、召喚をキャンセル！
	}

	// 今回実際に召喚する数を計算（要求数か、上限までの空き枠の少ない方）
	int actualSpawnCount = std::min(spawnCount, maxMinions - aliveCount);

	// 召喚数が0なら何もしない
	if (actualSpawnCount <= 0) {
		return;
	}

	// 4. 実際に召喚する処理
	float angleStep = 3.14159f * 2.0f / actualSpawnCount;
	for (int i = 0; i < actualSpawnCount; ++i) {
		float angle = angleStep * i;
		float radius = 4.0f;
		Vector3 spawnPos = {
			summonCenter.x + std::sin(angle) * radius,
			summonCenter.y,
			summonCenter.z + std::cos(angle) * radius
		};

		// ① 足元から這い上がるような、赤黒い禍々しいオーラ
		for (int j = 0; j < 40; j++) {
			Vector3 auraPos = {
				spawnPos.x + (rand() % 21 - 10) * 0.08f,
				spawnPos.y + 0.1f + (rand() % 10) * 0.05f, // 足元から
				spawnPos.z + (rand() % 21 - 10) * 0.08f
			};
			Vector3 auraVel = {
				(rand() % 11 - 5) * 0.015f,
				0.15f + (rand() % 10) * 0.03f, // 上に向かってフワァッと噴き出す
				(rand() % 11 - 5) * 0.015f
			};

			// 赤黒い闇の魔法カラー
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 0.8f, 0.0f, 0.0f, 0.8f } : Vector4{ 0.4f, 0.0f, 0.6f, 0.8f };
			float scale = 0.8f + (rand() % 6) * 0.1f;

			// 寿命を少し長くして（0.8秒）余韻を残す
			GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.8f, scale, color);
		}

		// ② 出現時のちょっとした衝撃波（紫色の波紋）
		for (int j = 0; j < 20; j++) {
			float ringAngle = (3.141592f * 2.0f / 20.0f) * j;
			float speed = 0.25f;
			Vector3 ringVel = { std::sinf(ringAngle) * speed, 0.0f, std::cosf(ringAngle) * speed };
			GPUParticleManager::GetInstance()->Emit(spawnPos, ringVel, 0.4f, 1.2f, { 0.6f, 0.0f, 0.9f, 1.0f });
		}

		// 敵の本体を生成
		auto newEnemy = std::make_unique<Enemy>();
		newEnemy->Initialize();
		newEnemy->SetType(Enemy::Type::Normal);
		newEnemy->SetPosition(spawnPos);
		newEnemy->SetBossRoomBehavior(true);

		// 敵の見た目（3Dモデル）を生成
		auto enemyObj = std::unique_ptr<Obj3d>(Obj3d::Create("enemy"));
		if (enemyObj) {
			enemyObj->SetCamera(camera);
			enemyObj->SetTranslation(spawnPos);
			enemyObj->SetScale(newEnemy->GetScale());
			enemyObj->Update();
		}

		// 敵の魔法システムを生成
		auto enemyCardSystem = std::make_unique<CardUseSystem>();
		enemyCardSystem->Initialize(camera);

		// リストに追加
		enemies_.push_back(std::move(newEnemy));
		enemyObjs_.push_back(std::move(enemyObj));
		enemyCardSystems_.push_back(std::move(enemyCardSystem));
		enemyDeadHandled_.push_back(false);
	}
}

void EnemyManager::SpawnEnemiesRandom(int enemyCount, int margin, SpawnManager *spawnManager, MapManager* mapManager, const Vector3 &playerPos, Camera *camera) {
	// ポインタが有効かチェック
	if (!spawnManager || !mapManager || !spawnManager->HasLevelData()) {
		return;
	}

	const LevelData &level = mapManager->GetLevelData();

	// spawnManager-> に変更
	std::vector<std::pair<int, int>> candidates = spawnManager->FindEnemySpawnCandidates(margin);

	if (candidates.empty()) {
		return;
	}

	// プレイヤーの現在位置をタイル座標に変換
	int playerTileX = static_cast<int>(std::round(playerPos.x / level.tileSize));
	int playerTileZ = static_cast<int>(std::round(playerPos.z / level.tileSize));

	std::vector<std::pair<int, int>> filtered;
	for (const auto &c : candidates) {
		int x = c.first;
		int z = c.second;

		// 階段本体 + 周囲1マス禁止（※IsNearStairsTileの処理は、必要ならEnemyManagerに持ってくるか、SpawnManagerに移動させると良いです。今回は一旦コメントアウトか簡易的な判定にします）
		// if (IsNearStairsTile(x, z)) { continue; } 

		// マップの範囲内かチェック
		if (x >= 0 && x < level.width && z >= 0 && z < level.height) {
			if (level.tiles[z][x] != 0) {
				continue;
			}
		}

		// プレイヤーから近すぎるマスを除外
		int dx = x - playerTileX;
		int dz = z - playerTileZ;
		float distanceToPlayer = std::sqrt(static_cast<float>(dx * dx + dz * dz));

		if (distanceToPlayer < 5.0f) {
			continue;
		}

		filtered.push_back(c);
	}

	if (filtered.empty()) {
		return;
	}

	std::random_device rd;
	std::mt19937 mt(rd());
	std::shuffle(filtered.begin(), filtered.end(), mt);

	const int kMaxEnemies = 5;

	// 生きている敵の数を数える
	int aliveCount = 0;
	for (const auto& enemy : enemies_) {
		if (enemy && !enemy->IsDead()) {
			aliveCount++;
		}
	}

	int availableSpace = kMaxEnemies - aliveCount;

	if (availableSpace <= 0) {
		return;
	}

	int actualSpawnCount = (std::min)(enemyCount, static_cast<int>(filtered.size()));
	actualSpawnCount = (std::min)(actualSpawnCount, availableSpace);

	for (int i = 0; i < actualSpawnCount; ++i) {
		int tileX = filtered[i].first;
		int tileZ = filtered[i].second;

		
		// spawnManager-> に変更
		Vector3 worldPos = spawnManager->TileToWorldPosition(tileX, tileZ,0.0f);

		auto enemy = std::make_unique<Enemy>();
		enemy->Initialize();
		enemy->SetType(GetRandomEnemyTypeForFloor(mapManager->GetCurrentFloor()));
		enemy->SetPosition(worldPos);

		auto enemyObj = std::unique_ptr<Obj3d>(Obj3d::Create("enemy"));
		if (enemyObj) {
			enemyObj->SetCamera(camera); // camera_.get() から camera に変更
			enemyObj->SetTranslation(worldPos);
			enemyObj->SetScale(enemy->GetScale());
			enemyObj->Update();
		}

		auto enemyCardSystem = std::make_unique<CardUseSystem>();
		enemyCardSystem->Initialize(camera); // 同上

		enemies_.push_back(std::move(enemy));
		enemyObjs_.push_back(std::move(enemyObj));
		enemyDeadHandled_.push_back(false);
		enemyCardSystems_.push_back(std::move(enemyCardSystem));
	}
}

void EnemyManager::Clear() {
	enemies_.clear();
	enemyObjs_.clear();
	enemyCardSystems_.clear(); 
	enemyDeadHandled_.clear();
}

void EnemyManager::CheckCollisions(Player* player, MapManager* mapManager) {
	if (!player || player->IsDead() || !mapManager) {
		return;
	}

	const LevelData& level = mapManager->GetLevelData();

	const float playerRadius = 0.6f;
	const float enemyRadius = 0.6f;

	// 指定位置の敵が壁に当たるか
	auto IsEnemyHitWall = [&](const Vector3& pos) -> bool {
		AABB enemyAABB;
		enemyAABB.min = { pos.x - 0.5f, pos.y - 0.5f, pos.z - 0.5f };
		enemyAABB.max = { pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f };

		int gridX = static_cast<int>(std::round(pos.x / level.tileSize));
		int gridZ = static_cast<int>(std::round(pos.z / level.tileSize));
		int startX = (std::max)(0, gridX - 1);
		int endX = (std::min)(level.width - 1, gridX + 1);
		int startZ = (std::max)(0, gridZ - 1);
		int endZ = (std::min)(level.height - 1, gridZ + 1);

		for (int z = startZ; z <= endZ; z++) {
			for (int x = startX; x <= endX; x++) {
				if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
					continue;
				}

				float worldX = x * level.tileSize;
				float worldZ = z * level.tileSize;

				AABB blockAABB;
				blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
				blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

				if (Collision::IsCollision(enemyAABB, blockAABB)) {
					return true;
				}
			}
		}

		return false;
		};

	// 指定位置のプレイヤーが壁に当たるか
	auto IsPlayerHitWall = [&](const Vector3& pos) -> bool {
		AABB playerAABB;
		playerAABB.min = { pos.x - 0.5f, pos.y - 0.5f, pos.z - 0.5f };
		playerAABB.max = { pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f };

		int gridX = static_cast<int>(std::round(pos.x / level.tileSize));
		int gridZ = static_cast<int>(std::round(pos.z / level.tileSize));
		int startX = (std::max)(0, gridX - 1);
		int endX = (std::min)(level.width - 1, gridX + 1);
		int startZ = (std::max)(0, gridZ - 1);
		int endZ = (std::min)(level.height - 1, gridZ + 1);

		for (int z = startZ; z <= endZ; z++) {
			for (int x = startX; x <= endX; x++) {
				if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
					continue;
				}

				float worldX = x * level.tileSize;
				float worldZ = z * level.tileSize;

				AABB blockAABB;
				blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
				blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

				if (Collision::IsCollision(playerAABB, blockAABB)) {
					return true;
				}
			}
		}

		return false;
		};

	Vector3 playerPos = player->GetPosition();

	// プレイヤーと敵の押し出し
	for (auto& enemy : enemies_) {
		if (!enemy || enemy->IsDead()) {
			continue;
		}

		Vector3 enemyPos = enemy->GetPosition();

		Vector3 diff = {
			enemyPos.x - playerPos.x,
			0.0f,
			enemyPos.z - playerPos.z
		};

		float dist = Length(diff);
		float pushRange = playerRadius + enemyRadius;

		if (dist > 0.001f && dist < pushRange) {
			Vector3 pushDir = Normalize(diff);
			float pushAmount = pushRange - dist;

			Vector3 newPlayerPos = playerPos;
			Vector3 newEnemyPos = enemyPos;

			newPlayerPos.x -= pushDir.x * (pushAmount * 0.5f);
			newPlayerPos.z -= pushDir.z * (pushAmount * 0.5f);

			newEnemyPos.x += pushDir.x * (pushAmount * 0.5f);
			newEnemyPos.z += pushDir.z * (pushAmount * 0.5f);

			bool playerHitWall = IsPlayerHitWall(newPlayerPos);
			bool enemyHitWall = IsEnemyHitWall(newEnemyPos);

			// 両方とも壁に当たらない時だけ反映
			if (!playerHitWall && !enemyHitWall) {
				playerPos = newPlayerPos;
				enemy->SetPositionOnly(newEnemyPos);
			}
			// プレイヤーだけ安全ならプレイヤーだけ動かす
			else if (!playerHitWall && enemyHitWall) {
				playerPos = newPlayerPos;
			}
			// 敵だけ安全なら敵だけ動かす
			else if (playerHitWall && !enemyHitWall) {
				enemy->SetPositionOnly(newEnemyPos);
			}
		}
	}

	player->SetPosition(playerPos);

	// 敵同士の押し出し
	for (size_t i = 0; i < enemies_.size(); ++i) {
		if (!enemies_[i] || enemies_[i]->IsDead()) {
			continue;
		}

		for (size_t j = i + 1; j < enemies_.size(); ++j) {
			if (!enemies_[j] || enemies_[j]->IsDead()) {
				continue;
			}

			Vector3 posA = enemies_[i]->GetPosition();
			Vector3 posB = enemies_[j]->GetPosition();

			Vector3 diff = {
				posB.x - posA.x,
				0.0f,
				posB.z - posA.z
			};

			float dist = Length(diff);
			float pushRange = enemyRadius + enemyRadius;

			if (dist > 0.001f && dist < pushRange) {
				Vector3 pushDir = Normalize(diff);
				float pushAmount = pushRange - dist;

				Vector3 newPosA = posA;
				Vector3 newPosB = posB;

				newPosA.x -= pushDir.x * (pushAmount * 0.5f);
				newPosA.z -= pushDir.z * (pushAmount * 0.5f);

				newPosB.x += pushDir.x * (pushAmount * 0.5f);
				newPosB.z += pushDir.z * (pushAmount * 0.5f);

				bool aHitWall = IsEnemyHitWall(newPosA);
				bool bHitWall = IsEnemyHitWall(newPosB);

				// 両方とも安全な時だけ両方反映
				if (!aHitWall && !bHitWall) {
					enemies_[i]->SetPositionOnly(newPosA);
					enemies_[j]->SetPositionOnly(newPosB);
				}
				// Aだけ安全ならAだけ動かす
				else if (!aHitWall && bHitWall) {
					enemies_[i]->SetPositionOnly(newPosA);
				}
				// Bだけ安全ならBだけ動かす
				else if (aHitWall && !bHitWall) {
					enemies_[j]->SetPositionOnly(newPosB);
				}
			}
		}
	}
}

// 指定したワールド座標に敵をスポーンさせる
void EnemyManager::SpawnEnemyAt(const Vector3& worldPos, Camera* camera, int floor) {
	auto enemy = std::make_unique<Enemy>();
	enemy->Initialize();
	enemy->SetType(GetRandomEnemyTypeForFloor(floor));
	enemy->SetPosition(worldPos);

	// 敵の見た目（3Dモデル）を生成
	auto enemyObj = std::unique_ptr<Obj3d>(Obj3d::Create("enemy"));
	if (enemyObj) {
		enemyObj->SetCamera(camera);
		enemyObj->SetTranslation(worldPos);
		enemyObj->SetScale(enemy->GetScale());
		enemyObj->Update();
	}

	// 敵の魔法システムを生成
	auto enemyCardSystem = std::make_unique<CardUseSystem>();
	enemyCardSystem->Initialize(camera);

	// リストに追加
	enemies_.push_back(std::move(enemy));
	enemyObjs_.push_back(std::move(enemyObj));
	enemyDeadHandled_.push_back(false);
	enemyCardSystems_.push_back(std::move(enemyCardSystem));
}
