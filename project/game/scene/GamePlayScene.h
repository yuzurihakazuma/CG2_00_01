#pragma once
#define NOMINMAX
// --- エンジン側のファイル ---
#include "Engine/Scene/IScene.h"
#include "Engine/Math/Matrix4x4.h"
#include "Engine/graphics/TextureManager.h"

#include "game/card/HandManager.h"
#include "game/card/CardPickupManager.h"
#include "game/card/FloorEffectManager.h"
#include "game/spawn/SpawnManager.h"
#include "game/Player/LevelUpBonusManager.h"

#include "Animation.h"
#include "InstancedGroup.h"
#include "engine/3d/animation/Animation.h"
#include "engine/3d/animation/CustomAnimation.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/3d/animation/CustomAnimation.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "Tutorial.h"


// --- 標準ライブラリ ---
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <random>

// 前方宣言
class DebugCamera;
class Camera;
class Sprite;
class Obj3d;
class DirectXCommon;
class Input;
class RenderTexture; 
class PostEffect;
class PlayerManager;
class LevelEditor;
class Enemy;
class CardUseSystem;
class Boss;
class Minimap;
class EnemyManager;
class BossManager;
class MapManager;
class FloorEffectManager;

// ゲームプレイシーン
class GamePlayScene : public IScene {
public:
	// 初期化
	void Initialize() override;
	// 終了
	void Finalize() override;
	// 更新
	void Update() override;

	void UpdatePostEffects(); 

	// 描画
	void Draw() override;

	// デバッグ用UIの描画
	void DrawDebugUI() override;

	GamePlayScene();

	~GamePlayScene();

private: // メンバ変数

	// カメラ
	std::unique_ptr<Camera> camera_ = nullptr;
	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Obj3d>> object3ds_;

	// スプライト
	std::vector<std::unique_ptr<Sprite>> sprites_;
	std::unique_ptr<Sprite> sprite_ = nullptr;

	// 交換フェイズ中のスプライト
	std::unique_ptr<Sprite> swapDarkOverlay_ = nullptr;
	std::unique_ptr<Sprite> swapUiSprite_ = nullptr;

	Vector2 spritePos_ = { 100.0f, 100.0f };

	// テクスチャデータ
	std::unordered_map<std::string, TextureData> textures_;

	// デプスステンシル
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;

	std::string bgmFile_ = "resources/BGMDon.mp3";

	// 描画先を切り替えるためのRenderTexture
	std::unique_ptr<PostEffect> postEffect_ = nullptr;

	// プレイヤー全体の管理
	std::unique_ptr<PlayerManager> playerManager_ = nullptr;

	std::unique_ptr<Obj3d> testObj_ = nullptr;

	// プレイヤー位置とスケールは他の処理でも使うのでまだ残す
	Vector3 playerPos_ = { 5.0f, 0.0f, 5.0f };
	Vector3 playerScale_ = { 1.0f, 1.0f, 1.0f };

	// マップエディタ
	std::unique_ptr<MapManager> mapManager_;

	bool isEditorActive_ = true;

	//手札管理とプレイヤーコスト
	HandManager handManager_;

	CardPickupManager cardPickupManager_;

	// カード
	std::unique_ptr<CardUseSystem> playerCardSystem_ = nullptr;

	// スポーンマネージャー
	SpawnManager spawnManager_;

	// レベルアップボーナスを管理する専用クラス
	LevelUpBonusManager levelUpBonusManager_;

	
	std::unique_ptr<EnemyManager> enemyManager_;

	std::unique_ptr<BossManager> bossManager_ = nullptr;

	int enemySpawnCount_ = 5;   // 出したい敵の数
	int enemySpawnMargin_ = 2;  // 壁から何マス離すか

	void ResetBattleDebug(); // デバッグ用バトルリセット

	Vector2 WorldToScreen(const Vector3& worldPos) const; // ワールド座標をスクリーン座標に変換する関数
	bool ProjectWorldToScreen(const Vector3& worldPos, Vector2& screenPos) const;
	void DrawCharacterHitboxesDebug() const;
	void DrawBossBeamHitboxesDebug() const;
	void DrawDebugAABB(const Vector3& center, const Vector3& halfSize, unsigned int color, float thickness) const;
	void DrawDebugCircleXZ(const Vector3& center, float radius, unsigned int color, float thickness) const;
	void DrawDebugOrientedRectXZ(const Vector3& center, float yaw, float halfWidth, float halfLength, unsigned int color, float thickness) const;

	//UI専用カメラ
	std::unique_ptr<Camera> uiCamera_ = nullptr;

	// カード交換モード用変数
	bool isCardSwapMode_ = false; // 交換モード中かどうか
	Card pendingCard_;            // 拾おうとしている（保留中の）カード
	struct CardPickup *pendingPickup_ = nullptr; // 拾おうとしているフィールドのアイテム
	int swapSelectionIndex_ = 0;                 // 捨てる手札を選ぶカーソル位置

	//カード交換モードの処理
	void UpdateCardSwapMode(Input *input);

	//カード使用の処理
	void UpdateCardUse(Input *input);
	void SyncReadiedCardIndex();
	void PauseMagicCastForSwap();
	void ResumeMagicCastAfterSwap();
	void EndMagicCast(bool consumeReadiedCard);
	void StartCardUseFlash(const Card& card, bool persistent = false, bool dissolveEnabled = true);
	void UpdateCardUseFlash();
	void DrawCardUseFlash();
	void UpdatePlayerStatusGaugeUI();
	void DrawPlayerStatusGaugeUI();
	void UpdateTimedSpawns();
	bool SpawnTimedCardPickup();
	int CountActiveCardPickups() const;
	int CountAliveEnemies() const;

	// ポーズ画面の更新
	void UpdatePause(Input* input);

	// ポーズ画面の描画
	void DrawPauseUI();
	void UpdateBossIntroLetterbox();
	void DrawBossIntroLetterbox();

	float dissolveThreshold_ = 0.0f; // ディゾルブエフェクトの進行度（0.0で通常、1.0で完全に消える）
	
	
	// カードスポーンの処理
	void SpawnCardsRandom(int cardCount, int margin);

	// カードスポーン関連
	int cardSpawnCount_ = 5;
	int cardSpawnMargin_ = 1;
	int timedEnemySpawnTimer_ = 600;
	int timedCardSpawnTimer_ = 600;
	const int timedSpawnIntervalFrames_ = 600;
	const int normalTimedEnemyMax_ = 6;
	const int normalTimedCardMax_ = 6;
	const int bossTimedCardMax_ = 8;

	// ダンジョン再生成とプレイヤー再スポーンの処理
	void RegenerateDungeonAndRespawnPlayer(int roomCount);
	void RespawnPlayerInRoom();

	// ボス再配置
	void RespawnBossInRoom();

	// 敵とカードのクリア
	void ClearEnemiesAndCards();

	// 階層管理
	int currentFloor_ = 1;
	// 次の階層へ進む処理
	void AdvanceFloor();
	Animation testAnimation_;

	// 階段タイルの位置（タイル座標）
	std::pair<int, int> stairsTile_ = { -1, -1 };
	bool IsNearStairsTile(int x, int z) const;

	// カードの説明文の後ろに敷く背景画像テクスチャ
	uint32_t descBgTexture_ = 0;
	std::unique_ptr<Sprite> descBgSprite_ = nullptr;

	// ステータス無限モード(デバッグ用)
	bool isInfiniteMode_ = false;
	bool showCharacterHitboxes_ = false;
	// --- フェード演出用の状態 ---
	enum class TransitionState {
		None,       // 通常時
		FadeOut,    // 暗くなっている途中
		BlackHold,  // 真っ黒のまま階層表示
		FadeIn      // 明るくなっている途中
	};

	// private メンバ変数に追加
	TransitionState transitionState_ = TransitionState::None;
	float fadeAlpha_ = 0.0f;           // 0.0f(透明) ～ 1.0f(真っ黒)
	const float kFadeSpeed = 0.02f;   // フェードの速さ（調整してください）
	bool isFloorTransitionTextVisible_ = false;
	int floorTransitionDisplayFloor_ = 1;
	int floorTransitionHoldTimer_ = 0;
	bool shouldAdvanceFloorOnBlack_ = false;

	// 画面全体を覆う黒スプライト
	std::unique_ptr<Sprite> fadeSprite_ = nullptr; // または std::unique_ptr<Sprite> fadeSprite_;

	// プレイヤーステータスの背景
	std::unique_ptr<Sprite> playerStatusBgSprite_ = nullptr;
	std::unique_ptr<Sprite> handCountBgSprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeShadowSprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeFrameSprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeBackSprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeDelaySprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeFillSprite_ = nullptr;
	std::unique_ptr<Sprite> playerHpGaugeGlossSprite_ = nullptr;
	std::unique_ptr<Sprite> playerCostGaugeShadowSprite_ = nullptr;
	std::unique_ptr<Sprite> playerCostGaugeFrameSprite_ = nullptr;
	std::unique_ptr<Sprite> playerCostGaugeBackSprite_ = nullptr;
	std::unique_ptr<Sprite> playerCostGaugeFillSprite_ = nullptr;
	std::unique_ptr<Sprite> playerCostGaugeGlossSprite_ = nullptr;
	float displayedHpRatio_ = -1.0f;
	float hpDamageTrailRatio_ = -1.0f;

	// コスト不足メッセージ表示用
	int costLackMessageTimer_ = 0;


	// ブロックの一括描画用グループ
	std::unique_ptr<InstancedGroup> blockGroup_ = nullptr;
	std::vector<std::unique_ptr<Obj3d>> blocks_;

	std::unique_ptr<Minimap> minimap_;

	bool isCardReady_ = false;       // 現在カードを構えているか
	Card readiedCard_{};             // 構えているカードの情報
	int cardReadyTimer_ = 0;         // 構えていられる残り時間
	int readiedCardIndex_ = -1;
	static constexpr int kMagicCastDuration = 120;
	bool isMagicCastPausedForSwap_ = false;
	int magicRepeatCooldownTimer_ = 0;
	static constexpr int kMagicRepeatCooldownDuration = 24;
	int fistCooldownTimer_ = 0;
	const int fistCooldownDuration_ = 60;
	std::unique_ptr<Obj3d> cardUseFlashObj_ = nullptr;
	int cardUseFlashTimer_ = 0;
	bool cardUseFlashPersistent_ = false;
	bool cardUseFlashDissolving_ = false;
	bool cardUseFlashDissolveEnabled_ = true;
	static constexpr int kCardUseFlashDuration = 18;

	// ポーズ画面関連
	bool isPaused_ = false;                  // ポーズ中かどうか
	int pauseSelection_ = 0;                 // 0: Resume  1: Title

	std::unique_ptr<Sprite> pauseBgSprite_ = nullptr; // ポーズ中の半透明背景
	std::unique_ptr<Sprite> bossIntroTopBar_ = nullptr;
	std::unique_ptr<Sprite> bossIntroBottomBar_ = nullptr;
	bool wasBossIntroPlaying_ = false;
	int bossIntroLetterboxFadeTimer_ = 0;
	const int bossIntroLetterboxFadeDuration_ = 30;
	bool isBossDeathCinematicPlaying_ = false;
	bool bossDeathCinematicPlayed_ = false;
	int bossDeathCinematicTimer_ = 0;
	const int bossDeathCinematicDuration_ = 150;
	Vector3 bossDeathCinematicFocus_{ 0.0f, 0.0f, 0.0f };

	std::unique_ptr<SkinnedObj3d> skinnedObj_ = nullptr;

	// アニメーション再生用（編集なし・読み込みのみ）
	CustomAnimationTrack skinnedAnimTrack_;
	float skinnedAnimTime_ = 0.0f;

	// イントロ演出用の変数
	Vector3 introPlayerPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 introBossPos_{ 0.0f, 0.0f, 0.0f };
	
	// GPUパーティクルエミッター
	GPUParticleEmitter emitter_;

	// チュートリアル
	std::unique_ptr<Tutorial> tutorial_ = nullptr;

	// チュートリアル開始のリクエストを出すための静的関数とフラグ
	static bool pendingTutorialStart_;
	static bool ConsumeTutorialStartRequest();

	FloorEffectManager floorEffectManager_; // クラスを持たせる
public:
	// チュートリアル開始のリクエストを出すための静的関数
	static void RequestTutorialStart(bool enable);

	
	// ポストエフェクト用ステート追跡
	int  bossDeathFlashTimer_ = 0;
	int  enrageFlashTimer_ = 0;
	bool wasBossDead_ = false;
	bool wasEnraged_ = false;

};
