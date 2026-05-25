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
	step_ = Step::MoveIntro;

	ClearGameplayObjects();
	BuildTutorialMap();

	PlacePlayerAt(room0_.CenterX(), room0_.CenterZ());
	SpawnTutorialCard();
	UpdateTexts();
}

void Tutorial::Update(Input* input) {
	if (!isActive_) {
		return;
	}

	if (step_ == Step::MoveIntro && context_.playerManager) {
		const Vector3 currentPos = context_.playerManager->GetPosition();
		const LevelData& level = context_.mapManager->GetLevelData();
		const int gridX = static_cast<int>(std::round(currentPos.x / level.tileSize));
		const int gridZ = static_cast<int>(std::round(currentPos.z / level.tileSize));
		if (IsInsideRect(gridX, gridZ, room1_)) {
			step_ = Step::PickCard;
			UpdateTexts();
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
			SpawnTutorialEnemy();
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
		OpenCorridor(corridor1_);
		step_ = Step::StatusIntro;
		isPauseStep_ = true;
		waitingForSpace_ = true;
		tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
		UpdateTexts();
		return;
	}

	if (step_ == Step::DefeatEnemy && enemySpawned_ && AreAllEnemiesDefeated()) {
		OpenCorridor(corridor2_);
		SpawnCardSwapTutorialCards();
		step_ = Step::CardSwapIntro;
		isPauseStep_ = true;
		waitingForSpace_ = true;
		tutorialAdvanceCooldown_ = kAdvanceCooldownFrames_;
		UpdateTexts();
		return;
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
	CarveRect(corridor0_, 0);

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

	const int cardX = room1_.right - 2;
	const int cardZ = room1_.CenterZ();

	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(cardX, cardZ, 0.01f),
		CardDatabase::GetCardData(2)
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

	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX - 2, centerZ, 0.01f),
		CardDatabase::GetCardData(3)
	);
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX, centerZ, 0.01f),
		CardDatabase::GetCardData(4)
	);
	context_.cardPickupManager->AddPickup(
		GetTileWorldPosition(centerX + 2, centerZ, 0.01f),
		CardDatabase::GetCardData(5)
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
	const int enemyX0 = cardX - 1;
	const int enemyZ0 = cardZ - 1;
	const int enemyX1 = cardX - 1;
	const int enemyZ1 = cardZ + 1;

	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX0, enemyZ0, 1.0f),
		context_.camera
	);
	context_.enemyManager->SpawnEnemyAt(
		GetTileWorldPosition(enemyX1, enemyZ1, 1.0f),
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
	text->SetPosition("TutorialTitle", 20.0f, 260.0f);
	text->SetPosition("TutorialBody", 20.0f, 305.0f);
	text->SetCentered("TutorialTitle", false);
	text->SetCentered("TutorialBody", false);

	// チュートリアル中だけ、操作説明と本編のクリア条件を右下に常時表示する
	text->SetPosition("TutorialGuide", 1450.0f, 750.0f);
	text->SetCentered("TutorialGuide", false);
	text->SetScale("TutorialGuide", 0.8f);
	text->SetColor("TutorialGuide", 1.0f, 1.0f, 1.0f, 0.9f);
	text->SetText(
		"TutorialGuide",
		"操作説明\n"
		"WASD:移動\n"
		"LShift:回避\n"
		"Space:カード使用\n"
		"矢印キー:カード選択\n\n"
		"クリア条件:10階層まで進みボスを倒す"
	);


	switch (step_) {
	case Step::MoveIntro:
		text->SetText("TutorialTitle", "TUTORIAL 1 / 8");
		text->SetText("TutorialBody", "右下に操作説明とクリア条件が表示されています。");
		break;

	case Step::PickCard:
		text->SetText("TutorialTitle", "TUTORIAL 2 / 8");
		text->SetText("TutorialBody", "部屋に置かれたカードを拾ってください。");
		break;

	case Step::StatusIntro:
		text->SetText("TutorialTitle", "TUTORIAL 3 / 8");
		text->SetText("TutorialBody", "上にHP、コスト、レベル、経験値が表示されます。\nカード使用でコストを消費します。SPACEで再開します。");
		break;

	case Step::CombatIntro:
		text->SetText("TutorialTitle", "TUTORIAL 4 / 8");
		text->SetText("TutorialBody", "矢印キーで選びSPACEで選択中のカードを使います。\nSPACEで再開します。");
		break;

	case Step::DefeatEnemy:
		text->SetText("TutorialTitle", "TUTORIAL 4 / 8");
		text->SetText("TutorialBody", "2つ目の部屋の敵を倒してください。\n倒すまで次の部屋には進めません。");
		break;

	case Step::CardSwapIntro:
		text->SetText("TutorialTitle", "TUTORIAL 5 / 8");
		text->SetText("TutorialBody", "持てるカードは3枚まで、レベルアップで増やすことができます。SPACEで再開します。");
		break;

	case Step::CardSwapPractice:
		text->SetText("TutorialTitle", "TUTORIAL 5 / 8");
		text->SetText("TutorialBody", "部屋に3枚のカードがあります。\n手札がいっぱいの状態で拾うと交換になります。");
		break;

	case Step::EnemyCardWatch:
		text->SetText("TutorialTitle", "TUTORIAL 6 / 8");
		text->SetText("TutorialBody", "");
		break;

	case Step::EnemyCardIntro:
		text->SetText("TutorialTitle", "TUTORIAL 6 / 8");
		text->SetText("TutorialBody", "敵もカードを拾い、カード攻撃をしてきます。\nSPACEで再開します。");
		break;

	case Step::EnemyCardBattle:
		text->SetText("TutorialTitle", "TUTORIAL 6 / 8");
		text->SetText("TutorialBody", "この部屋の敵を2体とも倒してください。");
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
}
