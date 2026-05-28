#include "Minimap.h"
#include "Engine/2D/Sprite.h"
#include "Engine/Base/WindowProc.h"

#include <algorithm>
#include <cmath>

void Minimap::Initialize() {
	UpdateLayoutIfNeeded();

	backgroundSprite_.reset(Sprite::Create("resources/white1x1.png", mapLeftTop_).release());
	backgroundSprite_->SetAnchorPoint({ 0.0f, 0.0f });
	backgroundSprite_->SetSize(mapSize_);
	backgroundSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f }); // 背景を消す
	backgroundSprite_->Update();

	frameSprite_.reset(Sprite::Create("resources/white1x1.png", { mapLeftTop_.x - 2.0f, mapLeftTop_.y - 2.0f }).release());
	frameSprite_->SetAnchorPoint({ 0.0f, 0.0f });
	frameSprite_->SetSize({ mapSize_.x + 4.0f, mapSize_.y + 4.0f });
	frameSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f }); // 枠も消す
	frameSprite_->Update();

	playerSprite_.reset(Sprite::Create("resources/white1x1.png", mapLeftTop_).release());
	playerSprite_->SetAnchorPoint({ 0.5f, 0.5f });
	playerSprite_->SetSize({ 18.0f, 18.0f }); // 大きいマップでも見やすくする
	playerSprite_->SetColor({ 1.0f, 1.0f, 0.0f, 0.3f });
	playerSprite_->Update();
}

void Minimap::SetLevelData(const LevelData* levelData, bool keepDiscovery) {
	std::vector<bool> discoveredStates;

	// 同じ階層内の再構築時だけ開示状態を保持する
	if (keepDiscovery) {
		discoveredStates.reserve(chunks_.size());
		for (const Chunk& chunk : chunks_) {
			discoveredStates.push_back(chunk.discovered);
		}
	}

	// 新しいマップ情報でミニマップを作り直す
	levelData_ = levelData;
	UpdateLayoutIfNeeded();
	RebuildMapSprites();

	// 保持指定のときだけ開示状態を戻す
	if (keepDiscovery) {
		const size_t restoreCount = (std::min)(discoveredStates.size(), chunks_.size());
		for (size_t i = 0; i < restoreCount; ++i) {
			chunks_[i].discovered = discoveredStates[i];
		}
	}
}


void Minimap::SetPlayerPosition(const Vector3& worldPos) {
	playerWorldPos_ = worldPos;
}

void Minimap::SetEnemyPositions(const std::vector<Vector3>& worldPositions) {
	enemyWorldPositions_ = worldPositions;
	EnsureEnemySprites(enemyWorldPositions_.size());
}

void Minimap::SetCardPositions(const std::vector<Vector3>& worldPositions) {
	cardWorldPositions_ = worldPositions;
	EnsureCardSprites(cardWorldPositions_.size());
}

void Minimap::EnsureEnemySprites(size_t count) {
	while (enemySprites_.size() < count) {
		auto sprite = std::unique_ptr<Sprite>(Sprite::Create("resources/white1x1.png", mapLeftTop_));
		sprite->SetAnchorPoint({ 0.5f, 0.5f });
		sprite->SetSize({ 14.0f, 14.0f }); // 大マップ用に敵アイコンを見やすくする
		sprite->SetColor({ 1.0f, 0.35f, 0.35f, 0.3f }); // 少し透明にする
		sprite->Update();
		enemySprites_.push_back(std::move(sprite));
	}

	while (enemySprites_.size() > count) {
		enemySprites_.pop_back();
	}
}

void Minimap::EnsureCardSprites(size_t count) {
	while (cardSprites_.size() < count) {
		auto sprite = std::unique_ptr<Sprite>(Sprite::Create("resources/white1x1.png", mapLeftTop_));
		sprite->SetAnchorPoint({ 0.5f, 0.5f });
		sprite->SetSize({ 12.0f, 12.0f }); // 大マップ用にカードアイコンを見やすくする
		sprite->SetColor({ 0.2f, 1.0f, 0.4f, 0.3f }); // 少し透明にする
		sprite->Update();
		cardSprites_.push_back(std::move(sprite));
	}

	while (cardSprites_.size() > count) {
		cardSprites_.pop_back();
	}
}

void Minimap::RebuildMapSprites() {
	chunks_.clear();

	if (!levelData_ || levelData_->width <= 0 || levelData_->height <= 0) {
		return;
	}

	float tileSizeX = mapSize_.x / static_cast<float>(levelData_->width);
	float tileSizeY = mapSize_.y / static_cast<float>(levelData_->height);
	drawTileSize_ = (tileSizeX < tileSizeY) ? tileSizeX : tileSizeY;

	BuildChunks();
	BuildChunkSprites();
}
void Minimap::BuildChunks() {
	chunks_.clear();
	chunks_.resize(kChunkCountX * kChunkCountZ);

	const int innerLeft = 1;
	const int innerTop = 1;
	const int innerRight = levelData_->width - 2;
	const int innerBottom = levelData_->height - 2;

	const int usableWidth = innerRight - innerLeft + 1;
	const int usableHeight = innerBottom - innerTop + 1;

	const int cellWidth = usableWidth / kChunkCountX;
	const int cellHeight = usableHeight / kChunkCountZ;

	for (int cz = 0; cz < kChunkCountZ; ++cz) {
		for (int cx = 0; cx < kChunkCountX; ++cx) {

			const int index = cz * kChunkCountX + cx;
			Chunk& chunk = chunks_[index];

			int cellX = innerLeft + cx * cellWidth;
			int cellZ = innerTop + cz * cellHeight;

			int currentCellWidth =
				(cx == kChunkCountX - 1)
				? (innerRight - cellX + 1)
				: cellWidth;

			int currentCellHeight =
				(cz == kChunkCountZ - 1)
				? (innerBottom - cellZ + 1)
				: cellHeight;

			chunk.startX = cellX;
			chunk.endX = cellX + currentCellWidth - 1;

			chunk.startZ = cellZ;
			chunk.endZ = cellZ + currentCellHeight - 1;

			chunk.discovered = false;
		}
	}
}
void Minimap::BuildChunkSprites() {
	for (Chunk& chunk : chunks_) {

		// 壁は横方向の連続をまとめる
		for (int z = chunk.startZ; z <= chunk.endZ; ++z) {
			int x = chunk.startX;

			while (x <= chunk.endX) {
				if (levelData_->tiles[z][x] != 0 && levelData_->tiles[z][x] != 3) {
					++x;
					continue;
				}

				const int startX = x;
				while (x <= chunk.endX &&
					(levelData_->tiles[z][x] == 0 || levelData_->tiles[z][x] == 3)) {
					++x;
				}
				const int endX = x - 1;
				const int length = endX - startX + 1;

				Vector2 drawPos;
				drawPos.x = mapLeftTop_.x + startX * drawTileSize_;
				drawPos.y = mapLeftTop_.y + (levelData_->height - 1 - z) * drawTileSize_;

				auto sprite = std::unique_ptr<Sprite>(Sprite::Create("resources/white1x1.png", drawPos));
				sprite->SetAnchorPoint({ 0.0f, 0.0f });
				sprite->SetSize({ drawTileSize_ * length, drawTileSize_ });
				sprite->SetColor({ 0.45f, 0.85f, 1.0f, 0.1f }); // 道を薄い水色の半透明で表示する
				sprite->Update();

				chunk.wallSprites.push_back(std::move(sprite));
			}
		}

		const float outlineThickness = std::max(1.0f, drawTileSize_ * 0.10f);
		const Vector4 outlineColor = { 0.85f, 0.98f, 1.0f, 0.35f };

		auto isFloorTile = [this](int x, int z) {
			if (!levelData_ || x < 0 || x >= levelData_->width ||
				z < 0 || z >= levelData_->height) {
				return false;
			}
			return levelData_->tiles[z][x] == 0 || levelData_->tiles[z][x] == 3;
			};

		auto addOutline = [&chunk, outlineColor](Vector2 pos, Vector2 size) {
			auto sprite = std::unique_ptr<Sprite>(Sprite::Create("resources/white1x1.png", pos));
			sprite->SetAnchorPoint({ 0.0f, 0.0f });
			sprite->SetSize(size);
			sprite->SetColor(outlineColor);
			sprite->Update();
			chunk.outlineSprites.push_back(std::move(sprite));
			};

		for (int z = chunk.startZ; z <= chunk.endZ; ++z) {
			for (int x = chunk.startX; x <= chunk.endX; ++x) {
				if (!isFloorTile(x, z)) {
					continue;
				}

				Vector2 drawPos;
				drawPos.x = mapLeftTop_.x + x * drawTileSize_;
				drawPos.y = mapLeftTop_.y + (levelData_->height - 1 - z) * drawTileSize_;

				if (!isFloorTile(x, z + 1)) {
					addOutline(drawPos, { drawTileSize_, outlineThickness });
				}
				if (!isFloorTile(x, z - 1)) {
					addOutline(
						{ drawPos.x, drawPos.y + drawTileSize_ - outlineThickness },
						{ drawTileSize_, outlineThickness }
					);
				}
				if (!isFloorTile(x - 1, z)) {
					addOutline(drawPos, { outlineThickness, drawTileSize_ });
				}
				if (!isFloorTile(x + 1, z)) {
					addOutline(
						{ drawPos.x + drawTileSize_ - outlineThickness, drawPos.y },
						{ outlineThickness, drawTileSize_ }
					);
				}
			}
		}

		// 階段
		for (int z = chunk.startZ; z <= chunk.endZ; ++z) {
			for (int x = chunk.startX; x <= chunk.endX; ++x) {
				if (levelData_->tiles[z][x] != 3) {
					continue;
				}

				Vector2 drawPos;
				drawPos.x = mapLeftTop_.x + x * drawTileSize_;
				drawPos.y = mapLeftTop_.y + (levelData_->height - 1 - z) * drawTileSize_;

				const float borderThickness = std::max(2.0f, drawTileSize_ * 0.28f); // 枠線をさらに太くする

				// 枠全体を外側へ広げて、中央の空白を保ったまま大きく見せる
				const float outlineExpand = drawTileSize_ * 0.25f;

				// 階段マークの色
				const Vector4 stairsColor = { 0.0f, 1.0f, 0.8f, 0.3f };

				// 上
				{
					auto sprite = std::unique_ptr<Sprite>(Sprite::Create(
						"resources/white1x1.png",
						{ drawPos.x - outlineExpand, drawPos.y - outlineExpand }
					));
					sprite->SetAnchorPoint({ 0.0f, 0.0f });
					sprite->SetSize({ drawTileSize_ + outlineExpand * 2.0f, borderThickness });
					sprite->SetColor(stairsColor);
					sprite->Update();
					chunk.stairsSprites.push_back(std::move(sprite));
				}

				// 下
				{
					auto sprite = std::unique_ptr<Sprite>(Sprite::Create(
						"resources/white1x1.png",
						{ drawPos.x - outlineExpand, drawPos.y + drawTileSize_ + outlineExpand - borderThickness }
					));
					sprite->SetAnchorPoint({ 0.0f, 0.0f });
					sprite->SetSize({ drawTileSize_ + outlineExpand * 2.0f, borderThickness });
					sprite->SetColor(stairsColor);
					sprite->Update();
					chunk.stairsSprites.push_back(std::move(sprite));
				}

				// 左
				{
					auto sprite = std::unique_ptr<Sprite>(Sprite::Create(
						"resources/white1x1.png",
						{ drawPos.x - outlineExpand, drawPos.y - outlineExpand }
					));
					sprite->SetAnchorPoint({ 0.0f, 0.0f });
					sprite->SetSize({ borderThickness, drawTileSize_ + outlineExpand * 2.0f });
					sprite->SetColor(stairsColor);
					sprite->Update();
					chunk.stairsSprites.push_back(std::move(sprite));
				}

				// 右
				{
					auto sprite = std::unique_ptr<Sprite>(Sprite::Create(
						"resources/white1x1.png",
						{ drawPos.x + drawTileSize_ + outlineExpand - borderThickness, drawPos.y - outlineExpand }
					));
					sprite->SetAnchorPoint({ 0.0f, 0.0f });
					sprite->SetSize({ borderThickness, drawTileSize_ + outlineExpand * 2.0f });
					sprite->SetColor(stairsColor);
					sprite->Update();
					chunk.stairsSprites.push_back(std::move(sprite));
				}
			}
		}
	}
}

void Minimap::UpdateLayoutIfNeeded() {
	WindowProc* windowProc = WindowProc::GetInstance();
	const Vector2 screenSize = {
		static_cast<float>(windowProc->GetClientWidth()),
		static_cast<float>(windowProc->GetClientHeight())
	};

	if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) {
		return;
	}
	if (std::fabs(screenSize.x - currentScreenSize_.x) < 0.5f &&
		std::fabs(screenSize.y - currentScreenSize_.y) < 0.5f) {
		return;
	}

	std::vector<bool> discoveredStates;
	discoveredStates.reserve(chunks_.size());
	for (const Chunk& chunk : chunks_) {
		discoveredStates.push_back(chunk.discovered);
	}

	currentScreenSize_ = screenSize;

	constexpr float kBaseRightMargin = 70.0f;
	constexpr float kBaseTop = 200.0f;
	constexpr float kBaseMapSize = 550.0f;
	constexpr float kControlGuideBottomMargin = 10.0f;
	constexpr float kControlGuideHeight = 104.0f + 73.0f + 59.0f + 59.0f;
	constexpr float kControlGuideGap = 24.0f;

	mapSize_ = { kBaseMapSize, kBaseMapSize };
	const float maxMapTop = screenSize.y - kControlGuideBottomMargin - kControlGuideHeight - kControlGuideGap - mapSize_.y;
	mapLeftTop_ = {
		screenSize.x - kBaseRightMargin - mapSize_.x,
		std::max(16.0f, std::min(kBaseTop, maxMapTop))
	};

	if (backgroundSprite_) {
		backgroundSprite_->SetPosition(mapLeftTop_);
		backgroundSprite_->SetSize(mapSize_);
	}
	if (frameSprite_) {
		frameSprite_->SetPosition({ mapLeftTop_.x - 2.0f, mapLeftTop_.y - 2.0f });
		frameSprite_->SetSize({ mapSize_.x + 4.0f, mapSize_.y + 4.0f });
	}

	if (levelData_) {
		RebuildMapSprites();

		const size_t restoreCount = (std::min)(discoveredStates.size(), chunks_.size());
		for (size_t i = 0; i < restoreCount; ++i) {
			chunks_[i].discovered = discoveredStates[i];
		}
	}

	UpdateStaticSprites();
}

void Minimap::UpdateStaticSprites() {
	if (backgroundSprite_) {
		backgroundSprite_->Update();
	}
	if (frameSprite_) {
		frameSprite_->Update();
	}

	for (Chunk& chunk : chunks_) {
		for (const auto& sprite : chunk.wallSprites) {
			if (sprite) {
				sprite->Update();
			}
		}
		for (const auto& sprite : chunk.outlineSprites) {
			if (sprite) {
				sprite->Update();
			}
		}
		for (const auto& sprite : chunk.stairsSprites) {
			if (sprite) {
				sprite->Update();
			}
		}
	}
}

int Minimap::GetChunkIndexFromTile(int tileX, int tileZ) const {
	if (!levelData_ || chunks_.empty()) {
		return -1;
	}

	tileX = std::clamp(tileX, 0, levelData_->width - 1);
	tileZ = std::clamp(tileZ, 0, levelData_->height - 1);

	for (int i = 0; i < static_cast<int>(chunks_.size()); ++i) {
		const Chunk& chunk = chunks_[i];

		if (tileX >= chunk.startX && tileX <= chunk.endX &&
			tileZ >= chunk.startZ && tileZ <= chunk.endZ) {
			return i;
		}
	}

	return -1;
}

void Minimap::DiscoverCurrentChunk() {
	if (!levelData_ || chunks_.empty()) {
		return;
	}

	const int tileX = static_cast<int>(std::floor(playerWorldPos_.x / levelData_->tileSize));
	const int tileZ = static_cast<int>(std::floor(playerWorldPos_.z / levelData_->tileSize));

	const int chunkIndex = GetChunkIndexFromTile(tileX, tileZ);
	if (chunkIndex >= 0) {
		chunks_[chunkIndex].discovered = true;
	}
}

bool Minimap::IsWorldPositionInDiscoveredChunk(const Vector3& worldPos) const {
	if (!levelData_ || chunks_.empty()) {
		return false;
	}

	const int tileX = static_cast<int>(std::floor(worldPos.x / levelData_->tileSize));
	const int tileZ = static_cast<int>(std::floor(worldPos.z / levelData_->tileSize));

	const int chunkIndex = GetChunkIndexFromTile(tileX, tileZ);
	if (chunkIndex < 0) {
		return false;
	}

	return chunks_[chunkIndex].discovered;
}

Vector2 Minimap::WorldToMinimapPosition(const Vector3& worldPos) const {
	Vector2 result = mapLeftTop_;

	if (!levelData_) {
		return result;
	}

	const float tileX = worldPos.x / levelData_->tileSize;
	const float tileZ = worldPos.z / levelData_->tileSize;

	result.x = mapLeftTop_.x + (tileX + 0.5f) * drawTileSize_;
	result.y = mapLeftTop_.y + ((levelData_->height - 1 - tileZ) + 0.5f) * drawTileSize_;

	return result;
}

void Minimap::Update() {
	UpdateLayoutIfNeeded();

	if (!visible_) {
		return;
	}
	

	// 今いる区画を探索済みにする
	DiscoverCurrentChunk();

	if (playerSprite_) {
		playerSprite_->SetPosition(WorldToMinimapPosition(playerWorldPos_));
		playerSprite_->Update();
	}

	for (size_t i = 0; i < enemySprites_.size(); ++i) {
		if (i < enemyWorldPositions_.size() &&
			IsWorldPositionInDiscoveredChunk(enemyWorldPositions_[i])) {
			enemySprites_[i]->SetPosition(WorldToMinimapPosition(enemyWorldPositions_[i]));
		} else {
			enemySprites_[i]->SetPosition({ -1000.0f, -1000.0f });
		}
		enemySprites_[i]->Update();
	}

	for (size_t i = 0; i < cardSprites_.size(); ++i) {
		if (i < cardWorldPositions_.size() &&
			IsWorldPositionInDiscoveredChunk(cardWorldPositions_[i])) {
			cardSprites_[i]->SetPosition(WorldToMinimapPosition(cardWorldPositions_[i]));
		} else {
			cardSprites_[i]->SetPosition({ -1000.0f, -1000.0f });
		}
		cardSprites_[i]->Update();
	}
}

void Minimap::Draw() {
	if (!visible_) {
		return;
	}

	if (frameSprite_) {
		frameSprite_->Draw();
	}
	if (backgroundSprite_) {
		backgroundSprite_->Draw();
	}

	// 探索済み区画だけ描画
	for (const Chunk& chunk : chunks_) {
		if (!chunk.discovered) {
			continue;
		}

		for (const auto& sprite : chunk.wallSprites) {
			sprite->Draw();
		}
		for (const auto& sprite : chunk.outlineSprites) {
			sprite->Draw();
		}
		for (const auto& sprite : chunk.stairsSprites) {
			sprite->Draw();
		}
	}

	for (size_t i = 0; i < enemyWorldPositions_.size() && i < enemySprites_.size(); ++i) {
		if (IsWorldPositionInDiscoveredChunk(enemyWorldPositions_[i])) {
			enemySprites_[i]->Draw();
		}
	}

	for (size_t i = 0; i < cardWorldPositions_.size() && i < cardSprites_.size(); ++i) {
		if (IsWorldPositionInDiscoveredChunk(cardWorldPositions_[i])) {
			cardSprites_[i]->Draw();
		}
	}

	if (playerSprite_) {
		playerSprite_->Draw();
	}
}

void Minimap::RevealAllMap() {
	for (Chunk& chunk : chunks_) {
		chunk.discovered = true;
	}
}

void Minimap::ResetDiscoveryMap() {
	for (Chunk& chunk : chunks_) {
		chunk.discovered = false;
	}
}
