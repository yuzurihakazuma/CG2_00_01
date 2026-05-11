#include "MapManager.h"

#include <cmath>
#include "../../engine/utils/Level/LevelManager.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/3d/model/ModelManager.h"
#include "game/enemy/EnemyManager.h"
#include "game/enemy/BossManager.h"
#include "game/map/Minimap.h"
#include "game/player/PlayerManager.h"
#include "game/card/CardPickupManager.h"
#include "game/spawn/SpawnManager.h"

// デフォルト生成
MapManager::MapManager() = default;

// デフォルト破棄
MapManager::~MapManager() = default;

// 初期化
void MapManager::Initialize() {
    // 初期階層と階段タイル位置
    int currentFloor_ = 1;
    std::pair<int, int> stairsTile_ = { -1, -1 };

    // サイズ初期化
    editWidth_ = levelData_.width;
    editHeight_ = levelData_.height;

    // マップ生成器の初期化
    mapGenerator_ = std::make_unique<MapGenerator>();
    mapGenerator_->Initialize();

    // 初期マップ設定
    currentMapFile_ = "resources/map/map01.json";
    mapType_ = 0;
    saveFileName_ = "map01.json";

    // 床インスタンスグループ
    floorGroup_ = std::make_unique<InstancedGroup>();
    floorGroup_->Initialize("ground", 10000);

    // 壁インスタンスグループ
    wallGroup_ = std::make_unique<InstancedGroup>();
    wallGroup_->Initialize("wall", 10000);

    // 階段インスタンスグループ
    stairsGroup_ = std::make_unique<InstancedGroup>();
    stairsGroup_->Initialize("stairs", 10000);

    // マップ読み込み
    LoadAndCreateMap(currentMapFile_);
}


// マップ読み込み＋生成
void MapManager::LoadAndCreateMap(const std::string& fileName) {

    // 読み込み
    levelData_ = LevelManager::GetInstance()->Load(fileName);

    // サイズ同期
    editWidth_ = levelData_.width;
    editHeight_ = levelData_.height;

    // オブジェクト再構築
    RebuildMapObjects();

    // 変更フラグ
    mapChanged_ = true;

    // 現在ファイル更新
    currentMapFile_ = fileName;
}

// 配列サイズ調整
void MapManager::ResizeObjectGrids() {

    // 行数確保
    floorObjects_.resize(levelData_.height);
    wallObjects_.resize(levelData_.height);

    // 列数確保
    for (int z = 0; z < levelData_.height; ++z) {
        floorObjects_[z].resize(levelData_.width);
        wallObjects_[z].resize(levelData_.width);
    }
}

// マップ再構築
void MapManager::RebuildMapObjects() {

    ResizeObjectGrids();

    for (int z = 0; z < levelData_.height; ++z) {
        for (int x = 0; x < levelData_.width; ++x) {
            UpdateTileObject(x, z);
        }
    }
}


// 更新処理
void MapManager::Update(const Vector3& playerPos) {

    if (floorGroup_) floorGroup_->PreUpdate();
    if (wallGroup_) wallGroup_->PreUpdate();
    if (stairsGroup_) stairsGroup_->PreUpdate();

    for (int z = 0; z < levelData_.height; ++z) {
        for (int x = 0; x < levelData_.width; ++x) {

            if (floorObjects_[z][x]) {
                floorObjects_[z][x]->Update();
                floorGroup_->AddObject(floorObjects_[z][x].get());
            }

            if (wallObjects_[z][x]) {
                wallObjects_[z][x]->Update();

                int tile = levelData_.tiles[z][x];
                if (tile == 1 || tile == 2) {
                    wallGroup_->AddObject(wallObjects_[z][x].get());
                } else if (tile == 3) {
                    stairsGroup_->AddObject(wallObjects_[z][x].get());
                }
            }
        }
    }
}

// 描画
void MapManager::Draw(const Vector3& playerPos) {

    // 床描画
    if (floorGroup_) floorGroup_->Draw(camera_);

    // 壁描画
    if (wallGroup_) wallGroup_->Draw(camera_);

    // 階段描画
    if (stairsGroup_) stairsGroup_->Draw(camera_);
}

// デバッグUI
void MapManager::DrawDebugUI() {
#ifdef USE_IMGUI
    // エディタ有効時
    if (isEditorActive) {

        // 部屋作成用
        static int roomX = 1;
        static int roomZ = 1;
        static int roomWidth = 5;
        static int roomHeight = 5;

        ImGui::Begin("マップエディタ");
        ImGui::Text("マップ切り替え");

        // 通常マップ切り替え
        if (ImGui::RadioButton("通常マップ(map01.json)", mapType_ == 0)) {
            mapType_ = 0;
            saveFileName_ = "map01.json";
            LoadAndCreateMap("resources/map/map01.json");
        }
        ImGui::SameLine();

        // ボスマップ切り替え
        if (ImGui::RadioButton("ボス部屋(boss.json)", mapType_ == 1)) {
            mapType_ = 1;
            saveFileName_ = "boss.json";
            LoadAndCreateMap("resources/map/boss.json");
        }

        ImGui::Separator();

        // 保存名入力
        char buffer[256];
        strcpy_s(buffer, saveFileName_.c_str());
        if (ImGui::InputText("保存ファイル名", buffer, sizeof(buffer))) {
            saveFileName_ = buffer;
        }

        // 保存パス
        std::string fullPath = "resources/map/" + saveFileName_;

        // 保存
        if (ImGui::Button("マップを保存")) {
            LevelManager::GetInstance()->Save(fullPath, levelData_);
        }
        ImGui::SameLine();

        // 読み込み
        if (ImGui::Button("マップを読み込む")) {
            LoadAndCreateMap(fullPath);
        }
        ImGui::SameLine();

        // クリア
        if (ImGui::Button("マップをクリア")) {
            FillAllTiles(0);
        }
        ImGui::SameLine();

        // 全部壁
        if (ImGui::Button("全部壁(1)で埋める")) {
            FillAllTiles(1);
        }

        // 全部床
        if (ImGui::Button("全部壁(0)で埋める")) {
            FillAllTiles(0);
        }

        ImGui::Separator();

        // サイズ入力
        ImGui::InputInt("幅", &editWidth_);
        ImGui::InputInt("高さ", &editHeight_);

        // 最小補正
        if (editWidth_ < 1) { editWidth_ = 1; }
        if (editHeight_ < 1) { editHeight_ = 1; }

        // サイズ反映
        if (ImGui::Button("サイズを反映")) {
            levelData_.width = editWidth_;
            levelData_.height = editHeight_;

            levelData_.tiles.resize(levelData_.height);
            for (auto& row : levelData_.tiles) {
                row.resize(levelData_.width, 0);
            }

            RebuildMapObjects();
        }

        ImGui::Separator();

        ImGui::Text("部屋作成");

        // 部屋入力
        ImGui::InputInt("部屋開始X", &roomX);
        ImGui::InputInt("部屋開始Z", &roomZ);
        ImGui::InputInt("部屋幅", &roomWidth);
        ImGui::InputInt("部屋高さ", &roomHeight);

        // 最小補正
        if (roomWidth < 1) { roomWidth = 1; }
        if (roomHeight < 1) { roomHeight = 1; }

        // 部屋作成
        if (ImGui::Button("部屋を作る")) {
            CreateRoom(roomX, roomZ, roomWidth, roomHeight);
        }
        ImGui::SameLine();

        // 中央部屋作成
        if (ImGui::Button("中央に部屋を作る")) {
            int autoRoomWidth = 6;
            int autoRoomHeight = 6;

            int startX = (levelData_.width - autoRoomWidth) / 2;
            int startZ = (levelData_.height - autoRoomHeight) / 2;

            CreateRoom(startX, startZ, autoRoomWidth, autoRoomHeight);
        }

        // ランダム部屋数
        static int randomRoomCount = 8;

        ImGui::Separator();
        ImGui::Text("ランダムダンジョン生成");

        // 部屋数入力
        ImGui::InputInt("部屋数", &randomRoomCount);
        if (randomRoomCount < 8) { randomRoomCount = 8; }
        if (randomRoomCount > 10) { randomRoomCount = 10; }

        // 部屋だけ生成
        if (ImGui::Button("部屋だけ生成")) {
            GenerateRandomDungeon(randomRoomCount);
        }

        ImGui::SameLine();

        // 部屋＋通路生成
        if (ImGui::Button("部屋+通路生成")) {
            GenerateRandomDungeon(randomRoomCount);
        }

        ImGui::Separator();

        ImGui::Text("配置タイル");

        // 配置タイル選択
        ImGui::RadioButton("床(0)", &selectedTile_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("壁(1)", &selectedTile_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("階段(3)", &selectedTile_, 3);

        ImGui::Separator();

        // 行数補正
        if ((int)levelData_.tiles.size() != levelData_.height) {
            levelData_.tiles.resize(levelData_.height);
        }

        // 列数補正
        for (auto& row : levelData_.tiles) {
            if ((int)row.size() != levelData_.width) {
                row.resize(levelData_.width, 0);
            }
        }

        ImGui::Text("グリッド編集");

        // グリッド表示
        for (int viewZ = 0; viewZ < levelData_.height; ++viewZ) {
            int dataZ = levelData_.height - 1 - viewZ;

            for (int x = 0; x < levelData_.width; ++x) {

                int tile = levelData_.tiles[dataZ][x];

                // 床色
                if (tile == 0) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 1.0f, 1.0f));
                }
                // 壁色
                else if (tile == 1) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
                // 階段色
                else if (tile == 2) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.2f, 1.0f));
                }
                else if (tile == 3) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 1.0f, 1.0f, 1.0f));
                }

                // ボタン名
                std::string label =
                    std::to_string(tile) +
                    "##" + std::to_string(dataZ) + "_" + std::to_string(x);

                // タイル変更
                if (ImGui::Button(label.c_str(), ImVec2(24, 24))) {
                    if (levelData_.tiles[dataZ][x] != selectedTile_) {
                        levelData_.tiles[dataZ][x] = selectedTile_;
                        UpdateTileObject(x, dataZ);
                    }
                }

                ImGui::PopStyleColor();

                // 横並び
                if (x < levelData_.width - 1) {
                    ImGui::SameLine();
                }
            }
        }

        ImGui::End();
    }
#endif
}

// 全タイル埋める
void MapManager::FillAllTiles(int tileType) {
    if (!mapGenerator_) {
        return;
    }

    mapGenerator_->FillAllTiles(levelData_, tileType);
    RebuildMapObjects();
}

// 部屋生成
void MapManager::CreateRoom(int startX, int startZ, int roomWidth, int roomHeight) {
    if (!mapGenerator_) {
        return;
    }

    mapGenerator_->CreateRoom(levelData_, startX, startZ, roomWidth, roomHeight);
    RebuildMapObjects();
}

// ランダムダンジョン生成
void MapManager::GenerateRandomDungeon(int roomCount) {
    if (!mapGenerator_) {
        return;
    }

    if (roomCount < 8) {
        roomCount = 8;
    }
    if (roomCount > 10) {
        roomCount = 10;
    }

    mapGenerator_->GenerateRandomDungeon(levelData_, roomCount);
    RebuildMapObjects();
}

// プレイヤー初期位置取得
Vector3 MapManager::GetRandomPlayerSpawnPosition(float y) {
    if (!mapGenerator_) {
        return { 0.0f, levelData_.baseY + y, 0.0f };
    }

    return mapGenerator_->GetRandomPlayerSpawnPosition(levelData_, y);
}

// マップ中心座標取得
Vector3 MapManager::GetMapCenterPosition(float y) const {
    const float tileSize = levelData_.tileSize;

    Vector3 pos;
    pos.x = ((float)levelData_.width - 1.0f) * tileSize * 0.5f;
    pos.y = levelData_.baseY + y;
    pos.z = ((float)levelData_.height - 1.0f) * tileSize * 0.5f;

    return pos;
}

float MapManager::GetFloorSurfaceY(float yOffset) const {
    // 床メッシュは baseY + 1.0f に置いているので、
    // キャラやドロップも同じ基準でそろえる。
    return levelData_.baseY + 1.0f + yOffset;
}

Vector3 MapManager::GetMapCenterFloorPosition(float yOffset) const {
    Vector3 pos = GetMapCenterPosition();
    pos.y = GetFloorSurfaceY(yOffset);
    return pos;
}

// マップ変更消費
bool MapManager::ConsumeMapChanged() {
    if (mapChanged_) {
        mapChanged_ = false;
        return true;
    }
    return false;
}

// 通常マップ切り替え
void MapManager::ChangeToNormalMap() {
    mapType_ = 0;
    currentMapFile_ = "resources/map/map01.json";
    saveFileName_ = "map01.json";
    LoadAndCreateMap(currentMapFile_);
}

// ボスマップ切り替え
void MapManager::ChangeToBossMap() {
    mapType_ = 1;
    currentMapFile_ = "resources/map/boss.json";
    saveFileName_ = "boss.json";
    LoadAndCreateMap(currentMapFile_);
}

// タイル→ワールド座標変換
Vector3 MapManager::GetTileWorldPosition(int x, int z, float y) const {
    if (!mapGenerator_) {
        Vector3 pos;
        pos.x = x * levelData_.tileSize;
        pos.y = levelData_.baseY + y;
        pos.z = z * levelData_.tileSize;
        return pos;
    }

    return mapGenerator_->GetTileWorldPosition(levelData_, x, z, y);
}

// 階段ランダム配置
void MapManager::PlaceStairsTileRandom(const Vector3& avoidWorldPos, float avoidDistance) {
    if (!mapGenerator_) {
        return;
    }

    mapGenerator_->PlaceStairsTileRandom(levelData_, avoidWorldPos, avoidDistance);
    RebuildMapObjects();
}

// タイルオブジェクト更新
void MapManager::UpdateTileObject(int x, int z) {
    if (z < 0 || z >= levelData_.height || x < 0 || x >= levelData_.width) return;

    Model* floorModel = ModelManager::GetInstance()->FindModel("ground");
    Model* wallModel = ModelManager::GetInstance()->FindModel("wall");
    Model* stairsModel = ModelManager::GetInstance()->FindModel("stairs");
    if (floorModel == nullptr || wallModel == nullptr || stairsModel == nullptr) return;

    const float tileSize = levelData_.tileSize;
    const int tile = levelData_.tiles[z][x];

    if (tile == 0 || tile == 3) {
        if (tile == 0) {
            wallObjects_[z][x].reset();
        }

        if (!floorObjects_[z][x]) {
            floorObjects_[z][x] = std::make_unique<Obj3d>();
            floorObjects_[z][x]->Initialize(floorModel);
            floorObjects_[z][x]->SetCamera(camera_);
        }

        floorObjects_[z][x]->SetTranslation({ x * tileSize, levelData_.baseY , z * tileSize });

        floorObjects_[z][x]->SetRotation({ -1.57f, 0.0f, 0.0f });

        floorObjects_[z][x]->SetScale({ 1.0f, 1.0f, 1.0f });
        if (tile == 0) {
            return;
        }
    }

    if (tile == 1 || tile == 2 || tile == 3) {
        if (tile != 3) {
            floorObjects_[z][x].reset();
        }

        if (!wallObjects_[z][x]) {
            wallObjects_[z][x] = std::make_unique<Obj3d>();
            wallObjects_[z][x]->Initialize(tile == 3 ? stairsModel : wallModel);
            wallObjects_[z][x]->SetCamera(camera_);
        }
        wallObjects_[z][x]->SetModel(tile == 3 ? stairsModel : wallModel);

        if (tile == 1 || tile == 2) {
            wallObjects_[z][x]->SetTranslation({ x * tileSize, levelData_.baseY + tileSize, z * tileSize });
            wallObjects_[z][x]->SetScale({ 1.0f, 1.0f, 1.0f });
            wallObjects_[z][x]->SetRotation({ 0.0f, 0.0f, 0.0f });
        } else {
            wallObjects_[z][x]->SetTranslation({ x * tileSize, levelData_.baseY + 2.0f, z * tileSize });
            wallObjects_[z][x]->SetScale({ 1.5f,  1.5f,  1.5f });
            wallObjects_[z][x]->SetRotation({ 0.0f,0.0f, 0.0f });
        }
        return;
    }

    floorObjects_[z][x].reset();
    wallObjects_[z][x].reset();
}


// 階段配置＋座標取得
std::pair<int, int> MapManager::PlaceStairsTileRandomAndGetTile(const Vector3& avoidWorldPos, float avoidDistance) {
    if (!mapGenerator_) {
        return { -1, -1 };
    }

    auto result = mapGenerator_->PlaceStairsTileRandomAndGetTile(levelData_, avoidWorldPos, avoidDistance);
    RebuildMapObjects();
    return result;
}

// ノイズテクスチャ設定
void MapManager::SetNoiseTexture(uint32_t index) {
    noiseTextureIndex_ = index;
    if (floorGroup_) floorGroup_->SetNoiseTexture(index);
    if (wallGroup_) wallGroup_->SetNoiseTexture(index);
    if (stairsGroup_) stairsGroup_->SetNoiseTexture(index);
}

// シングルトンインスタンスの取得
MapManager* MapManager::GetInstance(){
    static MapManager instance;
    return &instance;
}

// 階段タイル近いか
bool MapManager::IsNearStairsTile(int x, int z) const {
    if (stairsTile_.first < 0 || stairsTile_.second < 0) {
        return false;
    }

    int dx = x - stairsTile_.first;
    int dz = z - stairsTile_.second;

    return dx == 0 && dz == 0;
}

// 階段配置＋座標取得
void MapManager::AdvanceFloor(
    EnemyManager* enemyManager,
    BossManager* bossManager,
    Minimap* minimap,
    const std::function<void()>& resetBattleDebug
) {
    if (enemyManager) {
        enemyManager->Clear();
    }

    AdvanceFloorCount();

    const bool isBossFloor = (GetCurrentFloor() % 5 == 0);

    if (isBossFloor) {
        ChangeToBossMap();
    } else {
        ChangeToNormalMap();
    }

    if (bossManager) {
        if (isBossFloor) {
            bossManager->StartBossIntro();
        } else {
            bossManager->EndBossIntro();
            bossManager->SetBossCardRainTimer(0);
        }
    }

    if (resetBattleDebug) {
        resetBattleDebug();
    }

    if (minimap) {
        minimap->SetLevelData(&GetLevelData());
    }
}

void MapManager::RespawnPlayerInRoom(
    PlayerManager* playerManager,
    Camera* camera,
    Vector3& playerPos,
    Vector3& playerScale
) {
    if (playerManager) {
        playerManager->RespawnInRoom(this, camera);
        playerPos = playerManager->GetPosition();
        playerScale = playerManager->GetScale();
    }
}

void MapManager::RespawnBossInRoom(BossManager* bossManager) {
    if (bossManager) {
        bossManager->RespawnInRoom(this);
    }
}

void MapManager::ClearEnemiesAndCards(
    EnemyManager* enemyManager,
    CardPickupManager* cardPickupManager,
    Camera* camera
) {
    if (enemyManager) {
        enemyManager->Clear();
    }

    if (cardPickupManager) {
        cardPickupManager->Initialize(camera);
    }
}

void MapManager::SpawnCardsRandom(
    int cardCount,
    int margin,
    SpawnManager* spawnManager,
    CardPickupManager* cardPickupManager,
    EnemyManager* enemyManager,
    Camera* camera
) {
    if (!spawnManager || !cardPickupManager) {
        return;
    }

    if (!spawnManager->HasLevelData()) {
        return;
    }

    cardPickupManager->Initialize(camera);

    const LevelData& level = GetLevelData();

    std::vector<std::pair<int, int>> candidates =
        spawnManager->FindCardSpawnCandidates(margin);

    if (candidates.empty()) {
        return;
    }

    std::vector<std::pair<int, int>> enemyTiles;
    if (enemyManager) {
        const auto& enemies = enemyManager->GetEnemies();
        for (const auto& enemy : enemies) {
            if (!enemy || enemy->IsDead()) {
                continue;
            }

            Vector3 pos = enemy->GetPosition();
            int tileX = static_cast<int>(std::round(pos.x / level.tileSize));
            int tileZ = static_cast<int>(std::round(pos.z / level.tileSize));
            enemyTiles.push_back({ tileX, tileZ });
        }
    }

    std::vector<std::pair<int, int>> filtered;
    for (const auto& c : candidates) {
        int x = c.first;
        int z = c.second;

        if (IsNearStairsTile(x, z)) {
            continue;
        }

        if (x >= 0 && x < level.width && z >= 0 && z < level.height) {
            if (level.tiles[z][x] == 3) {
                continue;
            }
        }

        bool overlapsEnemy = false;
        for (const auto& e : enemyTiles) {
            if (e.first == x && e.second == z) {
                overlapsEnemy = true;
                break;
            }
        }
        if (overlapsEnemy) {
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

    int spawnCount = std::min(cardCount, static_cast<int>(filtered.size()));

    for (int i = 0; i < spawnCount; ++i) {
        int tileX = filtered[i].first;
        int tileZ = filtered[i].second;

        Vector3 worldPos = spawnManager->TileToWorldPosition(tileX, tileZ, 0.0f);
        worldPos.y = -0.99f;

        Card dropCard = CardDatabase::GetRandomPlayerCard();
        cardPickupManager->AddPickup(worldPos, dropCard);
    }
}

void MapManager::RegenerateDungeonAndRespawnPlayer(
    int roomCount,
    PlayerManager* playerManager,
    EnemyManager* enemyManager,
    BossManager* bossManager,
    SpawnManager* spawnManager,
    CardPickupManager* cardPickupManager,
    Camera* camera,
    Vector3& playerPos,
    Vector3& playerScale,
    int enemySpawnCount,
    int enemySpawnMargin,
    int cardSpawnCount,
    int cardSpawnMargin
) {
    SetStairsTile({ -1, -1 });

    if (!IsBossMap()) {
        GenerateRandomDungeon(roomCount);
    }

    RespawnPlayerInRoom(playerManager, camera, playerPos, playerScale);

    if (!IsBossMap()) {
        SetStairsTile(PlaceStairsTileRandomAndGetTile(playerPos, 6.0f));
    }

    if (spawnManager) {
        spawnManager->SetLevelData(&GetLevelData());
    }

    ClearEnemiesAndCards(enemyManager, cardPickupManager, camera);

    if (!IsBossMap() && enemyManager && spawnManager) {
        if (!IsBossMap() && enemyManager && spawnManager) {
            const auto& rooms = mapGenerator_->GetGeneratedRooms();

            std::random_device rd;
            std::mt19937 mt(rd());

            for (const auto& room : rooms) {
                // 敵はプレイヤーの近くに湧かないように現在位置を渡す
                auto roomTilesForEnemy = GetSpawnTilesInRoom(
                    room,
                    enemySpawnMargin,
                    &playerPos,
                    20.0f
                );

                // カードは今まで通りでOK
                auto roomTilesForCard = GetSpawnTilesInRoom(room, cardSpawnMargin);


                if (room.enemySpawnMax > 0 && room.enemySpawnPercent > 0 && !roomTilesForEnemy.empty()) {
                    std::shuffle(roomTilesForEnemy.begin(), roomTilesForEnemy.end(), mt);

                    int maxTryCount = std::min(room.enemySpawnMax, static_cast<int>(roomTilesForEnemy.size()));
                    std::uniform_int_distribution<int> percentDist(1, 100);

                    int tileIndex = 0;
                    for (int i = 0; i < maxTryCount; ++i) {
                        if (percentDist(mt) > room.enemySpawnPercent) {
                            continue;
                        }

                        int tileX = roomTilesForEnemy[tileIndex].first;
                        int tileZ = roomTilesForEnemy[tileIndex].second;
                        tileIndex++;

                        Vector3 worldPos = spawnManager->TileToWorldPosition(tileX, tileZ, 0.0f);
                        enemyManager->SpawnEnemyAt(worldPos, camera, currentFloor_);

                        if (tileIndex >= maxTryCount) {
                            break;
                        }
                    }
                }


                if (room.cardSpawnMax > 0 && room.cardSpawnPercent > 0 && !roomTilesForCard.empty()) {
                    std::shuffle(roomTilesForCard.begin(), roomTilesForCard.end(), mt);

                    int maxTryCount = std::min(room.cardSpawnMax, static_cast<int>(roomTilesForCard.size()));
                    std::uniform_int_distribution<int> percentDist(1, 100);

                    int tileIndex = 0;
                    for (int i = 0; i < maxTryCount; ++i) {
                        if (percentDist(mt) > room.cardSpawnPercent) {
                            continue;
                        }

                        int tileX = roomTilesForCard[tileIndex].first;
                        int tileZ = roomTilesForCard[tileIndex].second;
                        tileIndex++;

                        Vector3 worldPos = spawnManager->TileToWorldPosition(tileX, tileZ, 0.0f);
                        worldPos.y = -0.99f;

                        Card dropCard = CardDatabase::GetRandomPlayerCard();
                        cardPickupManager->AddPickup(worldPos, dropCard);

                        if (tileIndex >= maxTryCount) {
                            break;
                        }
                    }
                }

                
            }
        }

    }

    RespawnBossInRoom(bossManager);
}

void MapManager::UpdateMinimap(
    Minimap* minimap,
    const Vector3& playerPos,
    const CardPickupManager* cardPickupManager
) {
    if (!minimap) {
        return;
    }

    minimap->SetPlayerPosition(playerPos);

    std::vector<Vector3> cardPositions;
    if (cardPickupManager) {
        for (const auto& pickup : cardPickupManager->GetPickups()) {
            if (!pickup.isActive) {
                continue;
            }
            cardPositions.push_back(pickup.position);
        }
    }

    minimap->SetCardPositions(cardPositions);
    minimap->Update();
}

std::vector<std::pair<int, int>> MapManager::GetSpawnTilesInRoom(
    const DungeonGenerator::Room& room,
    int margin,
    const Vector3* avoidWorldPos,
    float avoidDistance
) const {
    std::vector<std::pair<int, int>> result;

    const LevelData& level = GetLevelData();

    for (int z = room.z + 1; z < room.z + room.height - 1; ++z) {
        for (int x = room.x + 1; x < room.x + room.width - 1; ++x) {
            if (x < 0 || x >= level.width || z < 0 || z >= level.height) {
                continue;
            }

            if (level.tiles[z][x] != 0) {
                continue;
            }

            bool ok = true;
            for (int dz = -margin; dz <= margin && ok; ++dz) {
                for (int dx = -margin; dx <= margin; ++dx) {
                    int cx = x + dx;
                    int cz = z + dz;

                    if (cx < 0 || cx >= level.width || cz < 0 || cz >= level.height) {
                        ok = false;
                        break;
                    }

                    if (level.tiles[cz][cx] != 0) {
                        ok = false;
                        break;
                    }
                }
            }

            if (!ok) {
                continue;
            }

            if (IsNearStairsTile(x, z)) {
                continue;
            }

            // プレイヤー付近の敵スポーン候補を除外する
            if (avoidWorldPos && avoidDistance > 0.0f) {
                Vector3 worldPos = GetTileWorldPosition(x, z, 0.0f);
                Vector3 diff = {
                    worldPos.x - avoidWorldPos->x,
                    0.0f,
                    worldPos.z - avoidWorldPos->z
                };

                if (VectorMath::Length(diff) < avoidDistance) {
                    continue;
                }

            }

            result.push_back({ x, z });
        }
    }

    return result;
}
