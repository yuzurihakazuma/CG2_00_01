#pragma once // 多重インクルード防止

#define NOMINMAX // min,maxマクロ対策

#include <vector>
#include <memory>
#include <string>
#include <random>
#include "engine/utils/Level/LevelData.h"
#include "../../engine/utils/Level/LevelManager.h"
#include "engine/math/VectorMath.h"
#include "game/map/MapGenerator.h"
#include "InstancedGroup.h"

#include <functional>

class Obj3d;  // 3Dオブジェクト前方宣言
class Camera; // カメラ前方宣言
class BossManager;
class EnemyManager;
class Minimap;
class PlayerManager;
class SpawnManager;
class CardPickupManager;
class Minimap;

// マップエディタ専用クラス
class MapManager {
public:

    // シングルトンインスタンスの取得
    static MapManager* GetInstance();



    // コンストラクタ
    MapManager();

    // デストラクタ
    ~MapManager();

    // 初期化
    void Initialize();

    // 更新
    void Update(const Vector3& playerPos);

    // 描画
    void Draw(const Vector3& playerPos);

    // マップ読込＋生成
    void LoadAndCreateMap(const std::string& fileName);

    // マップオブジェクト再構築
    void RebuildMapObjects();

    // カメラ設定
    void SetCamera(const Camera* camera) { camera_ = camera; }

    // デバッグUI描画
    void DrawDebugUI();

    // マップデータ取得
    const LevelData& GetLevelData() const { return levelData_; }

    // タイル更新
    void UpdateTileObject(int x, int z);

    // 配列サイズ調整
    void ResizeObjectGrids();

    // 全タイル埋める
    void FillAllTiles(int tileType);

    // 部屋生成
    void CreateRoom(int startX, int startZ, int roomWidth, int roomHeight);

    // ランダムダンジョン生成
    void GenerateRandomDungeon(int roomCount);

    // プレイヤー初期位置取得
    Vector3 GetRandomPlayerSpawnPosition(float y = 0.0f);

    // 通常マップへ変更
    void ChangeToNormalMap();

    // ボスマップへ変更
    void ChangeToBossMap();

    // 階段ランダム配置
    void PlaceStairsTileRandom(const Vector3& avoidWorldPos, float avoidDistance = 6.0f);

    // タイル→ワールド座標変換
    Vector3 GetTileWorldPosition(int x, int z, float y = 0.0f) const;

    // 階段配置＋座標取得
    std::pair<int, int> PlaceStairsTileRandomAndGetTile(const Vector3& avoidWorldPos, float avoidDistance = 6.0f);

	// 現在の階層数取得・設定・進める
    int GetCurrentFloor() const { return currentFloor_; }
    void SetCurrentFloor(int floor) { currentFloor_ = floor; }
    void AdvanceFloorCount() { ++currentFloor_; }
	// 階段タイル位置取得・設定・近いか
    void SetStairsTile(const std::pair<int, int>& tile) { stairsTile_ = tile; }
    const std::pair<int, int>& GetStairsTile() const { return stairsTile_; }
    bool IsNearStairsTile(int x, int z) const;
	// 次の階層へ進む処理
    void AdvanceFloor(
        EnemyManager* enemyManager,
        BossManager* bossManager,
        Minimap* minimap,
        const std::function<void()>& resetBattleDebug
    );

    void RespawnPlayerInRoom(PlayerManager* playerManager, Camera* camera, Vector3& playerPos, Vector3& playerScale);

    void RespawnBossInRoom(BossManager* bossManager);

    void ClearEnemiesAndCards(EnemyManager* enemyManager, CardPickupManager* cardPickupManager, Camera* camera);

    void SpawnCardsRandom(int cardCount, int margin, SpawnManager* spawnManager, CardPickupManager* cardPickupManager, EnemyManager* enemyManager, Camera* camera);
    std::vector<std::pair<int, int>> GetSpawnTilesInRoom(
        const DungeonGenerator::Room& room,
        int margin,
        const Vector3* avoidWorldPos = nullptr,
        float avoidDistance = 0.0f
    ) const;


public:
    // ボスマップ判定
    bool IsBossMap() const { return mapType_ == 1; }

    // 現在のマップファイル取得
    const std::string& GetCurrentMapFile() const { return currentMapFile_; }

    // マップ変更状態を消費
    bool ConsumeMapChanged();

    // マップ中心座標取得
    Vector3 GetMapCenterPosition(float y = 0.0f) const;
    float GetFloorSurfaceY(float yOffset = 0.0f) const;
    Vector3 GetMapCenterFloorPosition(float yOffset = 0.0f) const;

    // ノイズテクスチャ設定
    void SetNoiseTexture(uint32_t index);
	// ダンジョン再生成とプレイヤー再スポーンの処理
    void RegenerateDungeonAndRespawnPlayer(
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
    );
    void UpdateMinimap(
        Minimap* minimap,
        const Vector3& playerPos,
        const CardPickupManager* cardPickupManager
    );
private:
    // 使用カメラ
    const Camera* camera_ = nullptr;

    // マップ本体
    LevelData levelData_;

    // 1マスごとの床オブジェクト
    std::vector<std::vector<std::unique_ptr<Obj3d>>> floorObjects_;

    // 1マスごとの壁オブジェクト
    std::vector<std::vector<std::unique_ptr<Obj3d>>> wallObjects_;

    // 保存ファイル名
    std::string saveFileName_ = "map01.json";

    // エディタ有効状態
    bool isEditorActive = true;

    // 選択中タイル
    int selectedTile_ = 0; // 0=床, 1=壁

    // 編集幅
    int editWidth_ = 10;

    // 編集高さ
    int editHeight_ = 10;

    // マップ生成クラス
    std::unique_ptr<MapGenerator> mapGenerator_;

    // 現在のマップファイル
    std::string currentMapFile_ = "resources/map/map01.json";

    // マップ種類
    int mapType_ = 0; // 0 = map01.json, 1 = boss.json

    // マップ変更フラグ
    bool mapChanged_ = false;

    // 床インスタンス描画
    std::unique_ptr<InstancedGroup> floorGroup_;

    // 壁インスタンス描画
    std::unique_ptr<InstancedGroup> wallGroup_;

    // 階段インスタンス描画
    std::unique_ptr<InstancedGroup> stairsGroup_;

    // ノイズテクスチャ番号
    uint32_t noiseTextureIndex_ = 0;

	// 現在の階層数
    int currentFloor_ = 1;
	// 階段タイル位置
    std::pair<int, int> stairsTile_ = { -1, -1 };

};
