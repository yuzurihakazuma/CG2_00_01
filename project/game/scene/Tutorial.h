// Tutorial.h
#pragma once

#include <utility>
#include "engine/math/VectorMath.h"

class MapManager;
class PlayerManager;
class EnemyManager;
class Enemy;
class CardPickupManager;
class Camera;
class Minimap;
class Input;
class HandManager;

class Tutorial {
public:
	struct Context {
		MapManager* mapManager = nullptr;
		PlayerManager* playerManager = nullptr;
		EnemyManager* enemyManager = nullptr;
		CardPickupManager* cardPickupManager = nullptr;
		Camera* camera = nullptr;
		Minimap* minimap = nullptr;
		HandManager* handManager = nullptr; // FirstRoomCardControlIntro で手札を直接操作する
	};

	void Initialize(const Context& context);
	void Start();
	void Update(Input* input);
	void Finalize();
	void SetTextSuppressed(bool suppressed);

	bool IsActive() const { return isActive_; }
	bool IsNormalFloorTransitionBlocked() const { return isActive_; }
	bool IsGameplayPausedByTutorial() const { return isPauseStep_; }
	bool ConsumeReturnToTitleRequest();
	bool ConsumeAdvanceInputRequest();

	bool ShouldKeepPracticeCards() const; // 練習用のけりとファイヤーボールを消費しない区間かどうか

	void CheckPlayerGoal(const Vector3& playerWorldPos);
	void NotifyFirstRoomCardUsed(int cardId); // 最初の部屋のカード使用を監視する
	bool IsReusableCombatPracticeStep() const; // 練習用カードを無限使用にするステップかどうか
	void NotifyCombatPracticeCardUsed(int cardId); // 練習用に、直前に使ったカードIDを記録する
	void UpdateCombatPracticeHitCheck(); // 練習用の敵が正しいカードで攻撃されたかを判定する
	bool ConsumeCombatPracticeClearRequest(); // 練習完了後にカード削除を依頼する
private:
	struct Rect {
		int left;
		int top;
		int right;
		int bottom;

		int CenterX() const { return (left + right) / 2; }
		int CenterZ() const { return (top + bottom) / 2; }
	};

	enum class Step {
		MoveIntro,
		MoveChecklist,
		FirstRoomTaskCompleteWait,
		FirstRoomStatusIntro,
		FirstRoomCardControlIntro,
		PickCard,
		StatusIntro,
		CombatIntro,
		DefeatEnemy,
		GoToSwapRoom,
		CardSwapIntro,
		CardSwapPractice,
		EnemyCardWatch,
		EnemyCardIntro,
		EnemyCardBattle,
		LevelUpIntro,
		ReachStairs
	};

private:
	void ClearGameplayObjects();
	void BuildTutorialMap();
	void CarveRect(const Rect& rect, int tile);
	void OpenCorridor(const Rect& rect);
	void SetTile(int x, int z, int tile);

	Vector3 GetTileWorldPosition(int tileX, int tileZ, float yOffset) const;
	void PlacePlayerAt(int tileX, int tileZ);

	void SpawnTutorialCard();
	void SpawnTutorialEnemy();
	void SpawnCardSwapTutorialCards();
	void SpawnEnemyCardTutorial();
	void SpawnTutorialStairs();

	bool IsInsideRect(int x, int z, const Rect& rect) const;
	bool AreAllPickupsCollected() const;
	bool AreAllEnemiesDefeated() const;
	bool HasEnemyPickedTutorialCard() const;

	void UpdateTexts() const;
	void ClearTexts() const;

	void UpdateFirstRoomChecks(Input* input);
	bool IsFirstRoomChecklistComplete() const;
	bool IsCombatPracticeCompleted() const; // 攻撃カードと魔法カードの両方を当てたか
private:
	Context context_{};
	bool isActive_ = false;
	bool requestReturnToTitle_ = false;

	bool pickupSpawned_ = false;
	bool enemySpawned_ = false;
	bool swapPickupSpawned_ = false;
	bool enemyCardTutorialSpawned_ = false;
	bool stairsSpawned_ = false;
	bool isPauseStep_ = false;
	bool waitingForSpace_ = false;
	bool isTextSuppressed_ = false;
	bool consumedAdvanceInput_ = false;
	int tutorialAdvanceCooldown_ = 0;
	Step step_ = Step::MoveIntro;
	int stairsX_ = -1;
	int stairsZ_ = -1;

	// 本編の small_square の床 10x10 に合わせて正方形部屋を固定配置する
	const Rect room0_{ 2, 2, 11, 11 };
	const Rect room1_{ 2, 14, 11, 23 };
	const Rect room2_{ 14, 14, 23, 23 };
	const Rect room3_{ 26, 14, 35, 23 };
	const Rect room4_{ 26, 26, 35, 35 };
	const Rect room5_{ 14, 26, 23, 35 };

	// 部屋同士をつなぐ通路も新しい床サイズに合わせて配置する
	const Rect corridor0_{ 6, 12, 7, 13 };
	const Rect corridor1_{ 12, 18, 13, 19 };
	const Rect corridor2_{ 24, 18, 25, 19 };
	const Rect corridor3_{ 30, 24, 31, 25 };
	const Rect corridor4_{ 24, 30, 25, 31 };
	static constexpr int kAdvanceCooldownFrames_ = 60;

	Vector3 firstRoomStartPos_{};
	bool firstRoomMoveChecked_ = false;
	bool firstRoomDodgeChecked_ = false;
	bool firstRoomCardChecked_ = false;

	bool shouldPauseOnIntro_ = false;

	int combatPracticePendingCardId_ = -1;   // 直前に使った練習用カードIDを覚える
	int combatPracticeTrackedEnemyHp_ = -1;  // 練習用の敵HPを監視する
	bool combatPracticeTaskCompleted_ = false; // 正しいカードで敵に当てたか

	bool requestCombatPracticeCleanup_ = false; // 練習完了後に手札整理を依頼する

	bool combatPracticeAttackHitChecked_ = false; // 攻撃カードを当てたか
	bool combatPracticeMagicHitChecked_ = false;  // 魔法カードを当てたか
	int combatPracticePendingCardTimer_ = 0; // 直前カードIDの有効時間

	bool firstRoomPotionGranted_ = false; // ポーションを二重追加しないためのフラグ

};
