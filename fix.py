import sys

file_path = "project/game/scene/GamePlayScene.cpp"
with open(file_path, "r", encoding="utf-8-sig") as f:
    content = f.read()

broken_str = """\
	// ダンジョン生成 + プレイヤー再配置 + 敵/カード再生成 + ボス再配置
	cardUseFlashTimer_ = 0;
	cardUseFlashPersistent_ = false;
	cardUseFlashDissolving_ = false;
	cardUseFlashDissolveEnabled_ = true;

	if (playerCardSystem_) {
		playerCardSystem_->CancelCasting();
	}
	if (playerManager_) {
		playerManager_->ResetTransientActionState();
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}
}"""

fixed_str = """\
	// ダンジョン生成 + プレイヤー再配置 + 敵/カード再生成 + ボス再配置
	RegenerateDungeonAndRespawnPlayer(5);
	if (playerManager_) {
		playerManager_->ResetTransientActionState();
	}

	// 再配置後のプレイヤー位置とスケールを取り直す
	if (playerManager_) {
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}

	if (minimap_ && mapManager_) {
		minimap_->SetLevelData(&mapManager_->GetLevelData());
	}

	// 交換モードも戻しておく
	isCardSwapMode_ = false;
	pendingCard_ = Card{};

	// レベルボーナスのリセット
	int currentLevel = playerManager_ ? playerManager_->GetLevel() : 1;
	levelUpBonusManager_.Reset(currentLevel);

	isBossDeathCinematicPlaying_ = false;
	bossDeathCinematicPlayed_ = false;
	bossDeathCinematicTimer_ = 0;
}

void GamePlayScene::ResetFloorTransitionActionState() {
	if (isCardReady_) {
		EndMagicCast(false);
	}
	isCardReady_ = false;
	isMagicCastPausedForSwap_ = false;
	cardReadyTimer_ = 0;
	readiedCard_ = Card{};
	readiedCardIndex_ = -1;
	handManager_.SetCastDisplay(-1, 0, 0, -1);
	TextManager::GetInstance()->SetText("ReadyCardT", "");

	ResetFireballPredictionAttack();
	cardUseFlashObj_.reset();
	cardUseFlashTimer_ = 0;
	cardUseFlashPersistent_ = false;
	cardUseFlashDissolving_ = false;
	cardUseFlashDissolveEnabled_ = true;

	if (playerCardSystem_) {
		playerCardSystem_->CancelCasting();
	}
	if (playerManager_) {
		playerManager_->ResetTransientActionState();
		playerPos_ = playerManager_->GetPosition();
		playerScale_ = playerManager_->GetScale();
	}
}"""

if broken_str in content:
    content = content.replace(broken_str, fixed_str)
    with open(file_path, "w", encoding="utf-8-sig") as f:
        f.write(content)
    print("Fixed successfully!")
else:
    print("Broken string not found!")
    
