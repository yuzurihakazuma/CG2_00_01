#include "Tutorial.h"

#include <cmath>

#include "engine/base/Input.h"
#include "engine/camera/Camera.h"
#include "engine/utils/Level/LevelData.h"
#include "engine/utils/TextManager.h"
#include "game/card/CardDatabase.h"
#include "game/card/CardPickupManager.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/EnemyManager.h"
#include "game/map/MapManager.h"
#include "game/map/Minimap.h"
#include "game/player/PlayerManager.h"
#include "game/card/HandManager.h"

void Tutorial::Initialize(const Context& context) {
	context_ = context;
}

void Tutorial::Start() {
	if (!context_.mapManager || !context_.playerManager || !context_.cardPickupManager) {
		return;
	}

	isActive_ = true;
	requestReturnToTitle_ = false;

	pickupSpawned_ = false;
	enemySpawned_ = false;
	swapPickupSpawned_ = false;
	enemyCardTutorialSpawned_ = false;
	stairsSpawned_ = false;
	isPauseStep_ = false;
	waitingForSpace_ = false;
	isTextSuppressed_ = false;
	consumedAdvanceInput_ = false;
	tutorialAdvanceCooldown_ = 0;
	stairsX_ = -1;
	stairsZ_ = -1;

	firstRoomStartPos_ = {};
	firstRoomMoveChecked_ = false;
	firstRoomDodgeChecked_ = false;
	firstRoomCardChecked_ = false;
	firstRoomPotionGranted_ = false; // チュートリアル開始時にポーション配布状態をリセットする

	combatPracticePendingCardId_ = -1;    // 練習用カードIDの記録を初期化する
	combatPracticeTrackedEnemyHp_ = -1;   // 敵HP監視値を初期化する
	combatPracticeTaskCompleted_ = false; // 練習タスク達成状態を初期化する

	combatPracticePendingCardTimer_ = 0; // 直前カードIDタイマーを初期化する

	combatPracticeAttackHitChecked_ = false; // 攻撃カード命中チェックを初期化する
	combatPracticeMagicHitChecked_ = false;  // 魔法カード命中チェックを初期化する

	step_ = Step::MoveChecklist;

	ClearGameplayObjects();
	BuildTutorialMap();

	PlacePlayerAt(room0_.CenterX(), room0_.CenterZ());
	if (context_.playerManager) {
		firstRoomStartPos_ = context_.playerManager->GetPosition();
	}
	SpawnTutorialCard();
	UpdateTexts();
}

void Tutorial::Update(Input* input) {
	if (!isActive_) {
		return;
	}
	// 直前に使ったカードIDは、一定時間を過ぎたら無効にする
	if (combatPracticePendingCardTimer_ > 0) {
		combatPracticePendingCardTimer_--;
		if (combatPracticePendingCardTimer_ <= 0) {
			combatPracticePendingCardId_ = -1;
			combatPracticePendingCardTimer_ = 0;
		}
	}
	if (step_ == Step::MoveChecklist) {
		UpdateFirstRoomChecks(input);

		if (IsFirstRoomChecklistComplete()) {
			// 最初のタスク完了後、1秒だけその場で見せる
			step_ = Step::FirstRoomTaskCompleteWait;
			isPauseStep_ = false;
			waitingForSpace_ = false;
			tutorialAdvanceCooldown_ = 60; // 60フレーム = 約1秒
			UpdateTexts();
			return;
		}
	}
	if (step_ == Step::FirstRoomTaskCompleteWait) {
		// 1秒待ったら次の説明へ進む
		if (tutorialAdvanceCooldown_ <= 0) {
			step_ = Step::FirstRoomStatusIntro;
			isPauseStep_ = true;
			waitingForSpace_ = true;
			tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
			UpdateTexts();
			return;
		}
	}
	if (tutorialAdvanceCooldown_ > 0) {
		tutorialAdvanceCooldown_--;
	}

	if (waitingForSpace_) {
		if (tutorialAdvanceCooldown_ > 0) {
			return;
		}

		// SPACE に加えて A ボタンでもチュートリアルを進められるようにする
		if (!input || !(input->Triggerkey(DIK_SPACE) || input->TriggerJoystickButton(XINPUT_GAMEPAD_A))) {
			return;
		}

		consumedAdvanceInput_ = true;
		waitingForSpace_ = false;

		switch (step_) {

		case Step::StatusIntro:
			// 攻撃カード / 魔法カードの説明の次は、敵タスク説明に進む
			step_ = Step::CombatIntro;
			isPauseStep_ = true;
			waitingForSpace_ = true;
			UpdateTexts();
			return;

		case Step::CombatIntro:
			step_ = Step::DefeatEnemy;
			isPauseStep_ = false;
			UpdateTexts();
			return;

		case Step::FirstRoomStatusIntro:
			// 上部UI説明の次は、実際にカードを選んで使う練習へ進む
			step_ = Step::FirstRoomCardControlIntro;
			isPauseStep_ = false;      // このステップではゲームを止めない
			waitingForSpace_ = false;  // SPACEで説明を閉じず、カード使用に使わせる
			tutorialAdvanceCooldown_ = 0;

			// 練習用にポーションを1枚だけ手札へ追加する
			if (!firstRoomPotionGranted_ && context_.handManager) {
				context_.handManager->AddCard(CardDatabase::GetCardData(3));
				firstRoomPotionGranted_ = true;
			}

			UpdateTexts();
			return;

		case Step::FirstRoomCardControlIntro:
			// このステップはポーション使用で進めるので、ここでは何もしない
			return;

		case Step::CardSwapIntro:
			step_ = Step::CardSwapPractice;
			isPauseStep_ = false;
			UpdateTexts();
			return;

		case Step::EnemyCardIntro:
			step_ = Step::EnemyCardBattle;
			isPauseStep_ = false;
			UpdateTexts();
			return;

		case Step::LevelUpIntro:
			OpenCorridor(corridor4_);
			SpawnTutorialStairs();
			step_ = Step::ReachStairs;
			isPauseStep_ = false;
			UpdateTexts();
			return;

		default:
			break;
		}
	}

	if (step_ == Step::PickCard && pickupSpawned_ && AreAllPickupsCollected()) {
		// 部屋のカードを拾ったら、次の説明へ進める
		OpenCorridor(corridor1_);
		step_ = Step::StatusIntro;
		isPauseStep_ = true;
		waitingForSpace_ = true;
		tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
		UpdateTexts();
		return;
	}

	if (step_ == Step::DefeatEnemy && context_.playerManager && !enemySpawned_) {
		const Vector3 currentPos = context_.playerManager->GetPosition();
		const LevelData& level = context_.mapManager->GetLevelData();
		const int gridX = static_cast<int>(std::round(currentPos.x / level.tileSize));
		const int gridZ = static_cast<int>(std::round(currentPos.z / level.tileSize));

		if (IsInsideRect(gridX, gridZ, room2_)) {
			SpawnTutorialEnemy();
			combatPracticePendingCardId_ = -1;
			combatPracticePendingCardTimer_ = 0; // 直前カードタイマーもリセットする
			combatPracticeTaskCompleted_ = false;
			combatPracticeAttackHitChecked_ = false;
			combatPracticeMagicHitChecked_ = false;

			// 練習用の敵は倒れないようにHPを大きくしておく
			combatPracticeTrackedEnemyHp_ = 100000;
			if (context_.enemyManager) {
				const auto& enemies = context_.enemyManager->GetEnemies();
				if (!enemies.empty() && enemies[0]) {
					enemies[0]->SetHP(100000); // 練習用なので高HPにする
				}
			}
		}
	}

	if (step_ == Step::DefeatEnemy && context_.enemyManager) {
		// 練習用の敵はずっと止めておく
		for (const auto& enemy : context_.enemyManager->GetEnemies()) {
			if (enemy && !enemy->IsDead()) {
				enemy->Freeze(2);
			}
		}

		// 敵HPを見て、正しいカードで当てられたかを判定する
		UpdateCombatPracticeHitCheck();
	}

	if (step_ == Step::DefeatEnemy && combatPracticeTaskCompleted_) {
		// 正しいカードで当てられたら、練習用の敵を消す
		context_.enemyManager->Clear();

		// 練習用の状態をリセットする
		enemySpawned_ = false;
		combatPracticePendingCardId_ = -1;
		combatPracticeTrackedEnemyHp_ = -1;
		combatPracticeTaskCompleted_ = false;
		requestCombatPracticeCleanup_ = true; // 手札から練習用2枚を消す依頼を出す

		// 次の部屋への通路を開ける
		OpenCorridor(corridor2_);

		// 次の案内へ進む
		step_ = Step::GoToSwapRoom;
		isPauseStep_ = false;
		waitingForSpace_ = false;
		UpdateTexts();
		return;
	}

	if (step_ == Step::GoToSwapRoom && context_.playerManager) {
		const Vector3 currentPos = context_.playerManager->GetPosition();
		const LevelData& level = context_.mapManager->GetLevelData();
		const int gridX = static_cast<int>(std::round(currentPos.x / level.tileSize));
		const int gridZ = static_cast<int>(std::round(currentPos.z / level.tileSize));
		
		// 次の部屋に入った瞬間にカード交換チュートリアルへ進める
		if (IsInsideRect(gridX, gridZ, room3_)) {
			SpawnCardSwapTutorialCards();
			step_ = Step::CardSwapIntro;
			isPauseStep_ = true;
			waitingForSpace_ = true;
			tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
			UpdateTexts();
			return;
		}
	}

	if (step_ == Step::CardSwapPractice && swapPickupSpawned_ && AreAllPickupsCollected()) {
		OpenCorridor(corridor3_);
		step_ = Step::EnemyCardWatch;
		isPauseStep_ = false;
		waitingForSpace_ = false;
		UpdateTexts();
		return;
	}

	if (step_ == Step::EnemyCardWatch && context_.playerManager && !enemyCardTutorialSpawned_) {
		const Vector3 currentPos = context_.playerManager->GetPosition();
		const LevelData& level = context_.mapManager->GetLevelData();
		const int gridX = static_cast<int>(std::round(currentPos.x / level.tileSize));
		const int gridZ = static_cast<int>(std::round(currentPos.z / level.tileSize));
		if (IsInsideRect(gridX, gridZ, room4_)) {
			SpawnEnemyCardTutorial();
		}
	}

	if (step_ == Step::EnemyCardWatch && enemyCardTutorialSpawned_ && HasEnemyPickedTutorialCard()) {
		step_ = Step::EnemyCardIntro;
		isPauseStep_ = true;
		waitingForSpace_ = true;
		tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
		UpdateTexts();
		return;
	}

	if (step_ == Step::EnemyCardBattle && enemyCardTutorialSpawned_ && AreAllEnemiesDefeated()) {
		step_ = Step::LevelUpIntro;
		isPauseStep_ = true;
		waitingForSpace_ = true;
		tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
		UpdateTexts();
	}
}

void Tutorial::Finalize() {
	ClearTexts();
	isActive_ = false;
	requestReturnToTitle_ = false;
	isPauseStep_ = false;
	waitingForSpace_ = false;
	isTextSuppressed_ = false;
	consumedAdvanceInput_ = false;
	tutorialAdvanceCooldown_ = 0;
}

bool Tutorial::ConsumeReturnToTitleRequest() {
	const bool requested = requestReturnToTitle_;
	requestReturnToTitle_ = false;
	return requested;
}

bool Tutorial::ConsumeAdvanceInputRequest() {
	const bool requested = consumedAdvanceInput_;
	consumedAdvanceInput_ = false;
	return requested;
}

void Tutorial::SetTextSuppressed(bool suppressed) {
	if (isTextSuppressed_ == suppressed) {
		return;
	}

	isTextSuppressed_ = suppressed;
	if (suppressed) {
		ClearTexts();
	} else {
		UpdateTexts();
	}
}

void Tutorial::CheckPlayerGoal(const Vector3& playerWorldPos) {
	if (!isActive_ || step_ != Step::ReachStairs || !stairsSpawned_ || !context_.mapManager) {
		return;
	}

	const LevelData& level = context_.mapManager->GetLevelData();
	const int gridX = static_cast<int>(std::round(playerWorldPos.x / level.tileSize));
	const int gridZ = static_cast<int>(std::round(playerWorldPos.z / level.tileSize));

	if (gridX == stairsX_ && gridZ == stairsZ_) {
		requestReturnToTitle_ = true;
	}
}

void Tutorial::ClearGameplayObjects() {
	if (context_.enemyManager) {
		context_.enemyManager->Clear();
	}

	if (context_.cardPickupManager) {
		context_.cardPickupManager->Initialize(context_.camera);
	}
}

void Tutorial::BuildTutorialMap() {
	LevelData& level = const_cast<LevelData&>(context_.mapManager->GetLevelData());

	for (int z = 0; z < level.height; ++z) {
		for (int x = 0; x < level.width; ++x) {
			level.tiles[z][x] = 1;
		}
	}

	CarveRect(room1_, 0);
	CarveRect(room2_, 0);
	CarveRect(room3_, 0);
	CarveRect(room4_, 0);
	CarveRect(room5_, 0);
	CarveRect(room0_, 0);

	context_.mapManager->SetCurrentFloor(1);
	context_.mapManager->SetStairsTile({ -1, -1 });
	context_.mapManager->RebuildMapObjects();

	if (context_.minimap) {
		context_.minimap->SetLevelData(&context_.mapManager->GetLevelData());
	}
}

void Tutorial::CarveRect(const Rect& rect, int tile) {
	for (int z = rect.top; z <= rect.bottom; ++z) {
		for (int x = rect.left; x <= rect.right; ++x) {
			SetTile(x, z, tile);
		}
	}
}

void Tutorial::OpenCorridor(const Rect& rect) {
	CarveRect(rect, 0);
	context_.mapManager->RebuildMapObjects();

	// 通路開放後にミニマップの壁表示も作り直す
	if (context_.minimap) {
		context_.minimap->SetLevelData(&context_.mapManager->GetLevelData());
	}
}


void Tutorial::SetTile(int x, int z, int tile) {
	LevelData& level = const_cast<LevelData&>(context_.mapManager->GetLevelData());

	if (x < 0 || x >= level.width || z < 0 || z >= level.height) {
		return;
	}

	level.tiles[z][x] = tile;
}

Vector3 Tutorial::GetTileWorldPosition(int tileX, int tileZ, float yOffset) const {
	const LevelData& level = context_.mapManager->GetLevelData();

	return {
		tileX * level.tileSize,
		context_.mapManager->GetFloorSurfaceY(yOffset),
		tileZ * level.tileSize
	};
}

void Tutorial::PlacePlayerAt(int tileX, int tileZ) {
	if (!context_.playerManager) {
		return;
	}

	const Vector3 playerPos = GetTileWorldPosition(tileX, tileZ, 1.5f);
	context_.playerManager->SetPosition(playerPos);

	if (context_.camera) {
		context_.camera->SetTranslation({
			playerPos.x,
			playerPos.y + 15.0f,
			playerPos.z - 15.0f
		});
		context_.camera->SetRotation({ 0.9f, 0.0f, 0.0f });
		context_.camera->Update();
	}
}

void Tutorial::SpawnTutorialCard() {
	context_.cardPickupManager->Initialize(context_.camera);

	const int centerX = room1_.CenterX();
	const int centerZ = room1_.CenterZ();

	// PickCard の部屋にファイヤーボールを置く
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX - 2, centerZ, 0.01f),
		CardDatabase::GetCardData(2) // ファイヤーボール
	);

	// PickCard の部屋にけりを置く
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX + 2, centerZ, 0.01f),
		CardDatabase::GetCardData(13) // けり
	);

	pickupSpawned_ = true;
}

void Tutorial::SpawnTutorialEnemy() {
	if (!context_.enemyManager) {
		return;
	}

	context_.enemyManager->Clear();

	const int enemyX = room2_.CenterX();
	const int enemyZ = room2_.CenterZ();

	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX, enemyZ, 1.0f),
		context_.camera
	);

	enemySpawned_ = true;
}

void Tutorial::SpawnCardSwapTutorialCards() {
	if (!context_.cardPickupManager) {
		return;
	}

	const int centerX = room3_.CenterX();
	const int centerZ = room3_.CenterZ();

	// 交換チュートリアル用に4枚のカードを横並びで配置する
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX - 3, centerZ, 0.01f),
		CardDatabase::GetCardData(3) // ポーション
	);
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX - 1, centerZ, 0.01f),
		CardDatabase::GetCardData(14) // 剣
	);
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX + 1, centerZ, 0.01f),
		CardDatabase::GetCardData(5) // シールド
	);
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX + 3, centerZ, 0.01f),
		CardDatabase::GetCardData(4) // スピードアップ
	);

	swapPickupSpawned_ = true;
}

void Tutorial::SpawnEnemyCardTutorial() {
	if (!context_.enemyManager || !context_.cardPickupManager) {
		return;
	}

	context_.enemyManager->Clear();

	const int cardX = room4_.right - 2;
	const int cardZ = room4_.CenterZ();

	// カードまで少し歩かせて、拾う流れを見えやすくする
	const int enemyX0 = cardX - 3;
	const int enemyZ0 = cardZ - 2;

	const int enemyX1 = cardX - 3;
	const int enemyZ1 = cardZ;

	const int enemyX2 = cardX - 3;
	const int enemyZ2 = cardZ + 2;

	// カードの手前側に3体並べて出す
	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX0, enemyZ0, 1.0f),
		context_.camera
	);
	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX1, enemyZ1, 1.0f),
		context_.camera
	);
	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX2, enemyZ2, 1.0f),
		context_.camera
	);

	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(cardX, cardZ, 0.01f),
		CardDatabase::GetRandomEnemyUsableCard()
	);

	enemyCardTutorialSpawned_ = true;
}

void Tutorial::SpawnTutorialStairs() {
	if (stairsSpawned_) {
		return;
	}

	stairsX_ = room5_.CenterX();
	stairsZ_ = room5_.CenterZ();

	SetTile(stairsX_, stairsZ_, 3);
	context_.mapManager->SetStairsTile({ stairsX_, stairsZ_ });
	context_.mapManager->RebuildMapObjects();

	if (context_.minimap) {
		context_.minimap->SetLevelData(&context_.mapManager->GetLevelData());
	}

	stairsSpawned_ = true;
}

bool Tutorial::IsInsideRect(int x, int z, const Rect& rect) const {
	return x >= rect.left && x <= rect.right && z >= rect.top && z <= rect.bottom;
}

bool Tutorial::AreAllPickupsCollected() const {
	if (!context_.cardPickupManager) {
		return false;
	}

	for (const auto& pickup : context_.cardPickupManager->GetPickups()) {
		if (pickup.isActive) {
			return false;
		}
	}
	return true;
}

bool Tutorial::AreAllEnemiesDefeated() const {
	if (!context_.enemyManager) {
		return false;
	}

	const auto& enemies = context_.enemyManager->GetEnemies();
	if (enemies.empty()) {
		return false;
	}

	for (const auto& enemy : enemies) {
		if (enemy && !enemy->IsDead()) {
			return false;
		}
	}
	return true;
}

bool Tutorial::HasEnemyPickedTutorialCard() const {
	if (!context_.enemyManager) {
		return false;
	}

	for (const auto& enemy : context_.enemyManager->GetEnemies()) {
		if (enemy && !enemy->IsDead() && enemy->HasPickupCard()) {
			return true;
		}
	}

	return false;
}

void Tutorial::UpdateTexts() const {
	if (isTextSuppressed_) {
		ClearTexts();
		return;
	}

	TextManager* text = TextManager::GetInstance();
	text->SetPosition("TutorialTitle", 20.0f, 140.0f);      // タイトルをさらに上に移動
	text->SetPosition("TutorialBody", 20.0f, 185.0f);       // 本文をさらに上に移動
	text->SetCentered("TutorialTitle", false);
	text->SetCentered("TutorialBody", false);

	// 右下ガイドを少し上に移動
	text->SetPosition("TutorialGuide", 1450.0f, 620.0f);
	text->SetCentered("TutorialGuide", false);
	text->SetScale("TutorialGuide", 0.8f);
	text->SetColor("TutorialGuide", 1.0f, 1.0f, 1.0f, 0.9f);

	// 通常チェック項目を上に移動
	text->SetPosition("TutorialCheckMove", 20.0f, 300.0f);
	text->SetPosition("TutorialCheckDodge", 20.0f, 335.0f);
	text->SetPosition("TutorialCheckCard", 20.0f, 370.0f);

	text->SetCentered("TutorialCheckMove", false);
	text->SetCentered("TutorialCheckDodge", false);
	text->SetCentered("TutorialCheckCard", false);

	text->SetScale("TutorialCheckMove", 0.8f);
	text->SetScale("TutorialCheckDodge", 0.8f);
	text->SetScale("TutorialCheckCard", 0.8f);

	// けり / ファイヤーボール確認用は少し下にして本文とかぶらないようにする
	text->SetPosition("TutorialCheckAttack", 20.0f, 355.0f);
	text->SetPosition("TutorialCheckMagic", 20.0f, 390.0f);

	text->SetCentered("TutorialCheckAttack", false);
	text->SetCentered("TutorialCheckMagic", false);

	text->SetScale("TutorialCheckAttack", 0.8f);
	text->SetScale("TutorialCheckMagic", 0.8f);

	// 毎フレーム最初に空文字を入れて、TextManager の初期値 "New Text" を出さない
	text->SetText("TutorialCheckAttack", "");
	text->SetText("TutorialCheckMagic", "");
	switch (step_) {
	case Step::MoveIntro:
	text->SetText("TutorialTitle", "TUTORIAL 1 / 8");

	text->SetText("TutorialBody", "右下に操作説明とクリア条件が表示されています。\nSPACE or A を押してください。");
	text->SetText("TutorialCheckMove", "");
	text->SetText("TutorialCheckDodge", "");
	text->SetText("TutorialCheckCard", "");
	break;

	case Step::MoveChecklist:
		text->SetText("TutorialTitle", "TUTORIAL 1 / 8");
		text->SetText("TutorialBody", "右下に操作説明とクリア条件が表示されています。\n右下の操作説明を見ながら、3つの操作を試してください。");

		text->SetText(
			"TutorialCheckMove",
			firstRoomMoveChecked_ ? "[OK] 移動する" : "[ ] 移動する"
		);
		text->SetText(
			"TutorialCheckDodge",
			firstRoomDodgeChecked_ ? "[OK] 回避する" : "[ ] 回避する"
		);
		text->SetText(
			"TutorialCheckCard",
			firstRoomCardChecked_ ? "[OK] 殴りカードを使う" : "[ ] 殴りカードを使う"
		);



		text->SetColor("TutorialCheckMove", firstRoomMoveChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomMoveChecked_ ? 0.3f : 1.0f, 1.0f);
		text->SetColor("TutorialCheckDodge", firstRoomDodgeChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomDodgeChecked_ ? 0.3f : 1.0f, 1.0f);
		text->SetColor("TutorialCheckCard", firstRoomCardChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomCardChecked_ ? 0.3f : 1.0f, 1.0f);
		break;
	case Step::FirstRoomTaskCompleteWait:
		text->SetText("TutorialTitle", "TUTORIAL 1 / 8");

		text->SetText(
			"TutorialCheckMove",
			firstRoomMoveChecked_ ? "[OK] 移動する" : "[ ] 移動する"
		);
		text->SetText(
			"TutorialCheckDodge",
			firstRoomDodgeChecked_ ? "[OK] 回避する" : "[ ] 回避する"
		);
		text->SetText(
			"TutorialCheckCard",
			firstRoomCardChecked_ ? "[OK] 殴りカードを使う" : "[ ] 殴りカードを使う"
		);

		text->SetColor("TutorialCheckMove", firstRoomMoveChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomMoveChecked_ ? 0.3f : 1.0f, 1.0f);
		text->SetColor("TutorialCheckDodge", firstRoomDodgeChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomDodgeChecked_ ? 0.3f : 1.0f, 1.0f);
		text->SetColor("TutorialCheckCard", firstRoomCardChecked_ ? 0.3f : 1.0f, 1.0f, firstRoomCardChecked_ ? 0.3f : 1.0f, 1.0f);

		break;
	case Step::FirstRoomStatusIntro:
		text->SetText("TutorialTitle", "TUTORIAL 2 / 8");
		// 最初の部屋のタスク完了後に、上部UIを説明する
		text->SetText("TutorialBody", "上にHP、コスト、レベル、経験値が表示されます。\nカード使用でコストを消費します。SPACEで再開します。");
		text->SetText("TutorialCheckMove", "");
		text->SetText("TutorialCheckDodge", "");
		text->SetText("TutorialCheckCard", "");
		break;

	case Step::FirstRoomCardControlIntro:
		text->SetText("TutorialTitle", "TUTORIAL 2 / 8");
		// 実際にカードを切り替えてポーションを使わせる説明にする
		text->SetText("TutorialBody", "矢印キーで選びSPACEで選択中のカードを使います。\nポーションを付与したので使ってみてください");
		text->SetText("TutorialCheckMove", "");
		text->SetText("TutorialCheckDodge", "");
		text->SetText("TutorialCheckCard", "");
		break;
	case Step::PickCard:
		text->SetText("TutorialTitle", "TUTORIAL 3 / 8");
		// 説明が終わったら、次の部屋で2枚のカードを拾わせる
		text->SetText("TutorialBody", "次の部屋にある2枚のカードを拾ってください。");
		text->SetText("TutorialCheckMove", "");
		text->SetText("TutorialCheckDodge", "");
		text->SetText("TutorialCheckCard", "");
		break;

	case Step::StatusIntro:
		text->SetText("TutorialTitle", "TUTORIAL 3 / 8");
		// 2枚拾ったあとに攻撃カードと魔法カードの違いを説明する
		text->SetText("TutorialBody", "けりは攻撃カード、ファイヤーボールは魔法カードです。\n攻撃カードは近くの敵に使い、魔法カードは離れた敵にも使えます。\nカードに種類が書いてあります。\n'攻'が攻撃'魔'が魔法です。\nSPACEで次へ進みます。");
		text->SetText("TutorialCheckMove", "");
		text->SetText("TutorialCheckDodge", "");
		text->SetText("TutorialCheckCard", "");
		break;

	case Step::CombatIntro:
		text->SetText("TutorialTitle", "TUTORIAL 4 / 8");
		// 練習用なので、けりとファイヤーボールは何回でも使えることを説明する
		text->SetText("TutorialBody", "次の部屋の敵に攻撃カードと魔法カードで攻撃し当ててみてください。\nこの部屋では練習なので何度もカードが使えます\nSPACEで始めます。");
		break;
	case Step::DefeatEnemy:
		text->SetText("TutorialTitle", "TUTORIAL 4 / 8");
		// 攻撃カードと魔法カードの両方を当てる練習タスクを表示する
		text->SetText("TutorialBody", "動かない敵に、けりとファイヤーボールをそれぞれ当ててください。\nLCTRLで向き固定することで狙いやすくなります\nこの練習では、けりとファイヤーボールは何回でも使えます。");

		text->SetText(
			"TutorialCheckAttack",
			combatPracticeAttackHitChecked_ ? "[OK] けりを当てる" : "[ ] けりを当てる"
		);
		text->SetText(
			"TutorialCheckMagic",
			combatPracticeMagicHitChecked_ ? "[OK] ファイヤーボールを当てる" : "[ ] ファイヤーボールを当てる"
		);

		text->SetColor("TutorialCheckAttack", combatPracticeAttackHitChecked_ ? 0.3f : 1.0f, 1.0f, combatPracticeAttackHitChecked_ ? 0.3f : 1.0f, 1.0f);
		text->SetColor("TutorialCheckMagic", combatPracticeMagicHitChecked_ ? 0.3f : 1.0f, 1.0f, combatPracticeMagicHitChecked_ ? 0.3f : 1.0f, 1.0f);

		text->SetText("TutorialCheckMove", "");
		text->SetText("TutorialCheckDodge", "");
		text->SetText("TutorialCheckCard", "");
		break;

	case Step::GoToSwapRoom:
		text->SetText("TutorialTitle", "TUTORIAL 4 / 8");
		// 練習後は次の部屋へ進ませる
		text->SetText("TutorialBody", "次の部屋に進んでください。");
		break;

	case Step::CardSwapIntro:
		text->SetText("TutorialTitle", "TUTORIAL 5 / 8");
		text->SetText("TutorialBody", "持てるカードは4枚まで、レベルアップで増やすことができます。SPACEで再開します。");
		break;

	case Step::CardSwapPractice:
		text->SetText("TutorialTitle", "TUTORIAL 5 / 8");
		text->SetText("TutorialBody", "部屋に4枚のカードがあります。\n手札がいっぱいの状態で拾うと交換になります。");
		break;

	case Step::EnemyCardWatch:
		text->SetText("TutorialTitle", "TUTORIAL 5 / 8");
		text->SetText("TutorialBody", "");
		break;

	case Step::EnemyCardIntro:
		text->SetText("TutorialTitle", "TUTORIAL 6 / 8");
		text->SetText("TutorialBody", "敵もカードを拾い、カード攻撃をしてきます。\nSPACEで再開します。");
		break;

	case Step::EnemyCardBattle:
		text->SetText("TutorialTitle", "TUTORIAL 6 / 8");
		text->SetText("TutorialBody", "この部屋の敵を3体とも倒してください。");
		break;

	case Step::LevelUpIntro:
		text->SetText("TutorialTitle", "TUTORIAL 7 / 8");
		text->SetText("TutorialBody", "敵を倒して経験値がたまるとレベルアップします。\nレベルアップではボーナスを選べます。");
		break;

	case Step::ReachStairs:
		text->SetText("TutorialTitle", "TUTORIAL 8 / 8");
		text->SetText("TutorialBody", "階段をのぼると\nチュートリアルを終えて\n本編を開始します。");
		break;
	}
}

void Tutorial::ClearTexts() const {
	TextManager::GetInstance()->SetText("TutorialTitle", "");
	TextManager::GetInstance()->SetText("TutorialBody", "");
	TextManager::GetInstance()->SetText("TutorialCheckMove", "");
	TextManager::GetInstance()->SetText("TutorialCheckDodge", "");
	TextManager::GetInstance()->SetText("TutorialCheckCard", "");
	TextManager::GetInstance()->SetText("TutorialCheckAttack", "");
	TextManager::GetInstance()->SetText("TutorialCheckMagic", "");
}

bool Tutorial::IsFirstRoomChecklistComplete() const {
	return firstRoomMoveChecked_ &&
		firstRoomDodgeChecked_ &&
		firstRoomCardChecked_;
}

void Tutorial::NotifyFirstRoomCardUsed(int cardId) {
	if (step_ == Step::MoveChecklist) {
		// 最初のチェックリストでは殴りカードを使えたかだけを見る
		if (cardId == 1 && !firstRoomCardChecked_) {
			firstRoomCardChecked_ = true;
			UpdateTexts();
		}
		return;
	}

	if (step_ == Step::FirstRoomCardControlIntro) {
		// このステップはポーションを使った時だけ次へ進める
		if (cardId != 3) {
			return; // 殴りカードを使っても進ませない
		}

		OpenCorridor(corridor0_);
		step_ = Step::PickCard;
		isPauseStep_ = false;
		waitingForSpace_ = false;
		UpdateTexts();
	}
}

void Tutorial::UpdateFirstRoomChecks(Input* input) {
	if (step_ != Step::MoveChecklist || !context_.playerManager || !context_.mapManager) {
		return;
	}

	const Vector3 currentPos = context_.playerManager->GetPosition();
	const float dx = currentPos.x - firstRoomStartPos_.x;
	const float dz = currentPos.z - firstRoomStartPos_.z;
	const float movedSq = (dx * dx) + (dz * dz);

	const float moveThreshold = context_.mapManager->GetLevelData().tileSize * 1.5f;
	const float moveThresholdSq = moveThreshold * moveThreshold;

	if (!firstRoomMoveChecked_ && movedSq >= moveThresholdSq) {
		firstRoomMoveChecked_ = true;
		UpdateTexts();
	}

	if (
		!firstRoomDodgeChecked_ &&
		input &&
		(
			input->Triggerkey(DIK_LSHIFT) ||
			input->TriggerJoystickButton(XINPUT_GAMEPAD_B)
			)
		) {
		firstRoomDodgeChecked_ = true;
		UpdateTexts();
	}
}

bool Tutorial::IsReusableCombatPracticeStep() const {
	// カード命中練習中だけ、けりとファイヤーボールを使い捨てにしない
	return isActive_ && step_ == Step::DefeatEnemy;
}

void Tutorial::NotifyCombatPracticeCardUsed(int cardId) {
	// 練習用の敵タスク中だけ、直前に使ったカードIDを記録する
	if (step_ != Step::DefeatEnemy) {
		return;
	}

	// 練習対象はファイヤーボール(2)とけり(13)だけにする
	if (cardId == 2 || cardId == 13) {
		combatPracticePendingCardId_ = cardId;
		// 命中判定は短時間だけ有効にする
		combatPracticePendingCardTimer_ = 180;
	}
}

void Tutorial::UpdateCombatPracticeHitCheck() {
	// 練習用の敵タスク中だけ、敵への命中を監視する
	if (step_ != Step::DefeatEnemy || !context_.enemyManager || !enemySpawned_) {
		return;
	}

	const auto& enemies = context_.enemyManager->GetEnemies();
	if (enemies.empty() || !enemies[0]) {
		return;
	}

	Enemy* enemy = enemies[0].get();

	// 敵に何かが当たっているフレームで、直前に使ったカードIDを見てチェックを付ける
	if (enemy->IsHit() && combatPracticePendingCardTimer_ > 0) {
		bool changed = false; // OK表示を更新する必要があるかを覚える

		if (combatPracticePendingCardId_ == 13 && !combatPracticeAttackHitChecked_) {
			// けりは攻撃カード扱い
			combatPracticeAttackHitChecked_ = true;
			changed = true;
		}

		if (combatPracticePendingCardId_ == 2 && !combatPracticeMagicHitChecked_) {
			// ファイヤーボールは魔法カード扱い
			combatPracticeMagicHitChecked_ = true;
			changed = true;
		}

		// 練習中は敵を倒させず、命中確認後にHPを元へ戻す
		if (combatPracticeTrackedEnemyHp_ > 0) {
			enemy->SetHP(combatPracticeTrackedEnemyHp_);
		}

		// 命中を確認したので、直前カード記録を消す
		combatPracticePendingCardId_ = -1;
		combatPracticePendingCardTimer_ = 0; // 命中したので有効時間も終了する

		// チェック状態が変わった瞬間に表示を更新する
		if (changed) {
			UpdateTexts();
		}
	}

	// 攻撃カードと魔法カードの両方が当たったら完了
	if (IsCombatPracticeCompleted()) {
		combatPracticeTaskCompleted_ = true;
	}
}
bool Tutorial::ConsumeCombatPracticeClearRequest() {
	// 練習完了後のカード整理依頼を1回だけ渡す
	const bool requested = requestCombatPracticeCleanup_;
	requestCombatPracticeCleanup_ = false;
	return requested;
}

bool Tutorial::IsCombatPracticeCompleted() const {
	// 攻撃カードと魔法カードの両方を当てたら完了
	return combatPracticeAttackHitChecked_ && combatPracticeMagicHitChecked_;
}