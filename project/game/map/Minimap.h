#pragma once

#define NOMINMAX
#include <memory>
#include <vector>
#include "engine/math/VectorMath.h"
#include "engine/utils/Level/LevelData.h"

class Sprite;

class Minimap {
public:
	void Initialize();
	void SetLevelData(const LevelData* levelData, bool keepDiscovery = true);

	void SetPlayerPosition(const Vector3& worldPos);
	void SetEnemyPositions(const std::vector<Vector3>& worldPositions);
	void SetCardPositions(const std::vector<Vector3>& worldPositions);

	void Update();
	void Draw();

	void SetVisible(bool visible) { visible_ = visible; }

	void RevealAllMap();
	void ResetDiscoveryMap();

private:
	struct Chunk {
		int startX = 0;
		int endX = 0;
		int startZ = 0;
		int endZ = 0;
		bool discovered = false;

		std::vector<std::unique_ptr<Sprite>> wallSprites;
		std::vector<std::unique_ptr<Sprite>> stairsSprites;
	};

private:
	Vector2 WorldToMinimapPosition(const Vector3& worldPos) const;

	void EnsureEnemySprites(size_t count);
	void EnsureCardSprites(size_t count);

	void RebuildMapSprites();
	void BuildChunks();
	void BuildChunkSprites();
	void UpdateStaticSprites();
	void DiscoverCurrentChunk();
	int GetChunkIndexFromTile(int tileX, int tileZ) const;
	bool IsWorldPositionInDiscoveredChunk(const Vector3& worldPos) const;

private:
	const LevelData* levelData_ = nullptr;

	bool visible_ = true;

	Vector2 mapLeftTop_ = { 20.0f, 20.0f };
	Vector2 mapSize_ = { 220.0f, 220.0f };

	float drawTileSize_ = 8.0f;

	// 4 x 4 = 16区画
	static constexpr int kChunkCountX = 4;
	static constexpr int kChunkCountZ = 4;

	std::unique_ptr<Sprite> backgroundSprite_;
	std::unique_ptr<Sprite> frameSprite_;

	std::unique_ptr<Sprite> playerSprite_;
	std::vector<std::unique_ptr<Sprite>> enemySprites_;
	std::vector<std::unique_ptr<Sprite>> cardSprites_;

	Vector3 playerWorldPos_{};
	std::vector<Vector3> enemyWorldPositions_;
	std::vector<Vector3> cardWorldPositions_;

	// 旧: 全体一括保持
	// std::vector<std::unique_ptr<Sprite>> wallSprites_;
	// std::vector<std::unique_ptr<Sprite>> stairsSprites_;

	// 新: 区画単位保持
	std::vector<Chunk> chunks_;
};
