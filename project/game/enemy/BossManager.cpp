#define NOMINMAX
#include <algorithm>
#include <cmath>

#include "BossManager.h"
#include "game/enemy/Boss.h"
#include "game/card/CardUseSystem.h"
#include "engine/3D/Obj/Obj3d.h"
#include "engine/2D/Sprite.h"
#include "game/map/MapManager.h"
#include "engine/camera/Camera.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/card/CardPickupManager.h"
#include "game/card/CardDatabase.h"
#include "engine/collision/Collision.h"
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleManager.h"

using namespace VectorMath;

void BossManager::Initialize(Camera* camera) {
    boss_ = std::make_unique<Boss>();
    boss_->Initialize();
    boss_->SetScale({ 2.0f, 2.0f, 2.0f });

    bossObj_ = std::unique_ptr<Obj3d>(Obj3d::Create("boss"));
    if (bossObj_) {
        bossObj_->SetCamera(camera);
        bossObj_->SetScale(boss_->GetScale());
    }

	for (int i = 0; i < 2; ++i) {
		// 分裂ボス本体を用意しておく
		splitBosses_[i] = std::make_unique<Boss>();
		splitBosses_[i]->Initialize();
		splitBosses_[i]->SetScale(splitBossScale_);

		// 分裂ボスの見た目オブジェクトも2つ用意する
		splitBossObjs_[i] = std::unique_ptr<Obj3d>(Obj3d::Create("boss"));
		if (splitBossObjs_[i]) {
			splitBossObjs_[i]->SetCamera(camera);
			splitBossObjs_[i]->SetScale(splitBossScale_);
			splitBossObjs_[i]->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
			splitBossObjs_[i]->Update();
		}
	}


    beamWarningObj_ = std::unique_ptr<Obj3d>(Obj3d::Create("sphere"));
    if (beamWarningObj_) {
        beamWarningObj_->SetCamera(camera);
        beamWarningObj_->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
        beamWarningObj_->SetScale({ 0.45f, 0.04f, 16.0f });

        Model* model = beamWarningObj_->GetModel();
        if (model) {
            model->SetTexture("resources/white1x1.png");

            Model::Material* material = model->GetMaterial();
            if (material) {
                material->color = { 1.0f, 0.05f, 0.05f, 0.32f };
                material->emissive = 2.2f;
            }
        }

        beamWarningObj_->Update();
    }

    bossCardSystem_ = std::make_unique<CardUseSystem>();
    bossCardSystem_->Initialize(camera);

    // ボスHPバー背景
    bossHpBackSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
    bossHpBackSprite_->SetSize({ 160.0f, 16.0f });
    bossHpBackSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.75f });

    // ボスHPバー本体
    bossHpFillSprite_ = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
    bossHpFillSprite_->SetSize({ 152.0f, 10.0f });
    bossHpFillSprite_->SetColor({ 0.2f, 1.0f, 0.2f, 1.0f });

    bossDeadHandled_ = false;
    bossIntroCameraState_ = IntroCameraState::None;
    bossIntroTimer_ = 0;
    isBossIntroPlaying_ = false;

    bossCardRainTimer_ = 0;
    isBossCardRainEnabled_ = true;
}

void BossManager::Finalize() {
    bossCardSystem_.reset();
    beamWarningObj_.reset();
    bossHpBackSprite_.reset();
    bossHpFillSprite_.reset();
    bossObj_.reset();
    boss_.reset();
	for (int i = 0; i < 2; ++i) {
		splitBossObjs_[i].reset();
		splitBosses_[i].reset();
	}

}

void BossManager::Reset() {
    if (boss_) {
        boss_->Initialize();
        boss_->SetScale({ 2.0f, 2.0f, 2.0f });
    }

    if (bossCardSystem_) {
        bossCardSystem_->Reset();
    }

    bossDeadHandled_ = false;
    EndBossIntro();
}

void BossManager::RespawnInRoom(MapManager* mapManager) {
    if (!mapManager || !boss_) {
        return;
    }

    // 通常マップではボスを出さない
    if (!mapManager->IsBossMap()) {
        boss_->Initialize();
        bossDeadHandled_ = false;

        // 画面外に逃がしておく
        boss_->SetPosition({ 9999.0f, -9999.0f, 9999.0f });

        if (bossObj_) {
            bossObj_->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
            bossObj_->Update();
        }
        return;
    }

	// ボス部屋の中央位置
	Vector3 spawnPos = mapManager->GetMapCenterFloorPosition(2.0f);

	// 10階層だけ分裂ボスにする
	const bool isSplitBossFloor = (mapManager->GetCurrentFloor() == 10);
	bossType_ = isSplitBossFloor ? BossType::Split : BossType::Normal;

	// 分裂演出の開始位置と最終配置を記録しておく
	splitBossCenterPosition_ = spawnPos;
	splitBossTargetPositions_[0] = spawnPos;
	splitBossTargetPositions_[0].x -= splitBossOffset_;
	splitBossTargetPositions_[1] = spawnPos;
	splitBossTargetPositions_[1].x += splitBossOffset_;

	bossDeadHandled_ = false;

	if (bossType_ == BossType::Split) {
		// 通常ボスは画面外へ逃がしておく
		boss_->Initialize();
		boss_->SetPosition({ 9999.0f, -9999.0f, 9999.0f });

		if (bossObj_) {
			bossObj_->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
			bossObj_->Update();
		}

		for (int i = 0; i < 2; ++i) {
			if (!splitBosses_[i]) {
				continue;
			}

			splitBosses_[i]->Initialize();

			// 最初は中央に重ねて出して、カットイン中に左右へ開く
			Vector3 splitSpawnPos = splitBossTargetPositions_[i];
			splitBosses_[i]->SetSpawnPosition(splitSpawnPos);


			// 今のボス60HPを半分にして30HPずつにする
			splitBosses_[i]->SetMaxHP(30);
			splitBosses_[i]->SetHP(30);
			// 10階の分裂ボスだけ専用AIを使う
			splitBosses_[i]->SetSplitBehaviorEnabled(true);

			if (i == 0) {
				// 左側は遠距離型
				splitBosses_[i]->SetCombatRole(Boss::CombatRole::Ranged);
			} else {
				// 右側は近距離型
				splitBosses_[i]->SetCombatRole(Boss::CombatRole::Melee);
			}

			splitBosses_[i]->SetForceMeleeMode(false);


			if (splitBossObjs_[i]) {
				splitBossObjs_[i]->SetTranslation(splitBosses_[i]->GetPosition());
				splitBossObjs_[i]->SetRotation(splitBosses_[i]->GetRotation());
				splitBossObjs_[i]->SetScale(splitBosses_[i]->GetScale());
				splitBossObjs_[i]->Update();
			}
		}

		return;
	}

	// 5階層など通常ボスは今まで通り
	boss_->Initialize();
	boss_->SetSpawnPosition(spawnPos);
	boss_->SetScale({ 2.0f, 2.0f, 2.0f });

	if (bossObj_) {
		bossObj_->SetTranslation(boss_->GetPosition());
		bossObj_->SetRotation(boss_->GetRotation());
		bossObj_->SetScale(boss_->GetScale());
		bossObj_->Update();
	}

	// 分裂ボス側は画面外へ逃がしておく
	for (int i = 0; i < 2; ++i) {
		if (splitBossObjs_[i]) {
			splitBossObjs_[i]->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
			splitBossObjs_[i]->Update();
		}
	}

}

void BossManager::StartBossIntro() {
	isBossIntroPlaying_ = true;
	bossIntroCameraState_ = IntroCameraState::SkyLook;
	bossIntroTimer_ = 50;
	bossCardRainTimer_ = bossCardRainInterval_;
}


void BossManager::EndBossIntro() {
    isBossIntroPlaying_ = false;
    bossIntroCameraState_ = IntroCameraState::None;
    bossIntroTimer_ = 0;
}

bool BossManager::ShouldTriggerGameClear(MapManager* mapManager) const {
	// 無効な値ならクリアしない
	if (!mapManager || !boss_) {
		return false;
	}

	// ボスマップでなければクリアしない
	if (!mapManager->IsBossMap()) {
		return false;
	}

	// 10階の分裂ボスは2体とも倒していて、死亡処理も終わっている時だけクリア
	if (bossType_ == BossType::Split) {
		bool leftDead = !splitBosses_[0] || splitBosses_[0]->IsDead();
		bool rightDead = !splitBosses_[1] || splitBosses_[1]->IsDead();
		return leftDead && rightDead && bossDeadHandled_;
	}

	// 5階など通常ボスは今まで通り
	return boss_->IsDead() && bossDeadHandled_;
}


void BossManager::UpdateBeamWarning(MapManager* mapManager) {
	if (!beamWarningObj_) {
		return;
	}

	Boss* warningBoss = nullptr;

	if (bossType_ == BossType::Split) {
		// 分裂ボス側でビーム詠唱中の個体を探す
		for (int i = 0; i < 2; ++i) {
			if (!splitBosses_[i] || splitBosses_[i]->IsDead()) {
				continue;
			}

			if (!splitBosses_[i]->IsAppearing() &&
				splitBosses_[i]->IsCasting() &&
				splitBosses_[i]->GetSelectedCard().id == 104) {
				warningBoss = splitBosses_[i].get();
				break;
			}
		}
	} else {
		if (boss_ &&
			!boss_->IsDead() &&
			!boss_->IsAppearing() &&
			boss_->IsCasting() &&
			boss_->GetSelectedCard().id == 104) {
			warningBoss = boss_.get();
		}
	}

	const bool shouldShow =
		warningBoss &&
		mapManager &&
		mapManager->IsBossMap();

	if (!shouldShow) {
		beamWarningObj_->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
		beamWarningObj_->Update();
		return;
	}

	const Vector3& bossPos = warningBoss->GetPosition();
	float bossYaw = warningBoss->GetRotation().y;
	Vector3 forward = {
		std::sinf(bossYaw),
		0.0f,
		std::cosf(bossYaw)
	};

	const float warningLength = 16.0f;
	Vector3 warningPos = {
		bossPos.x + forward.x * (warningLength * 0.90f),
		mapManager->GetFloorSurfaceY(0.08f),
		bossPos.z + forward.z * (warningLength * 0.90f)
	};

	beamWarningObj_->SetTranslation(warningPos);
	beamWarningObj_->SetRotation({ 0.0f, bossYaw, 0.0f });
	beamWarningObj_->SetScale({ 0.45f, 0.04f, warningLength });
	beamWarningObj_->Update();
}

void BossManager::Update(
	Player* player,
	EnemyManager* enemyManager,
	CardPickupManager* cardPickupManager,
	MapManager* mapManager,
	Camera* camera,
	const Vector3& playerPos,
	const Vector3& targetPos
) {
	// 必要なポインタが無ければ処理しない
	if (!boss_ || !mapManager || !cardPickupManager) {
		return;
	}

	// =========================================================
	// ボス本体の更新
	// =========================================================
	if (bossType_ != BossType::Split && !boss_->IsDead() && mapManager->IsBossMap()) {

		// 衝突で戻すために更新前の位置を保存
		Vector3 oldBossPos = boss_->GetPosition();

		// プレイヤー位置を教えてAI更新
		boss_->SetPlayerPosition(targetPos);
		// 登場演出中は、Appearの間だけ更新する
		if (!isBossIntroPlaying_ || boss_->IsAppearing()) {
			boss_->Update();
		}

		Vector3 bossPos = boss_->GetPosition();

		// ボスと壁の当たり判定
		AABB bossAABB;
		bossAABB.min = { bossPos.x - 1.0f, bossPos.y - 1.0f, bossPos.z - 1.0f };
		bossAABB.max = { bossPos.x + 1.0f, bossPos.y + 1.0f, bossPos.z + 1.0f };

		const LevelData& level = mapManager->GetLevelData();

		int bossGridX = static_cast<int>(std::round(bossPos.x / level.tileSize));
		int bossGridZ = static_cast<int>(std::round(bossPos.z / level.tileSize));

		int bStartX = std::max(0, bossGridX - 1);
		int bEndX = std::min(level.width - 1, bossGridX + 1);
		int bStartZ = std::max(0, bossGridZ - 1);
		int bEndZ = std::min(level.height - 1, bossGridZ + 1);

		bool isBossHit = false;

		for (int z = bStartZ; z <= bEndZ && !isBossHit; z++) {
			for (int x = bStartX; x <= bEndX; x++) {
				if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
					continue;
				}

				float worldX = x * level.tileSize;
				float worldZ = z * level.tileSize;

				AABB blockAABB;
				blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
				blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

				// 壁に当たったら元の位置に戻す
				if (Collision::IsCollision(bossAABB, blockAABB)) {
					boss_->SetPosition(oldBossPos);
					isBossHit = true;
					break;
				}
			}
		}

		// 見た目に反映
		if (bossObj_) {
			bossObj_->SetTranslation(boss_->GetPosition());
			bossObj_->SetRotation(boss_->GetRotation());
			bossObj_->SetScale(boss_->GetScale());
			bossObj_->Update();
		}
	}

	if (mapManager->IsBossMap()) {
		if (bossType_ == BossType::Split) {

			bool leftAlive = splitBosses_[0] && !splitBosses_[0]->IsDead();
			bool rightAlive = splitBosses_[1] && !splitBosses_[1]->IsDead();
			bool oneDefeated = (leftAlive != rightAlive);

			for (int i = 0; i < 2; ++i) {
				if (!splitBosses_[i] || splitBosses_[i]->IsDead()) {
					continue;
				}

				// 片方が倒れたら残った側も近距離型にする
				splitBosses_[i]->SetForceMeleeMode(oneDefeated);

				// 相方の位置を渡して、重ならないようにする
				int partnerIndex = (i == 0) ? 1 : 0;
				if (splitBosses_[partnerIndex] && !splitBosses_[partnerIndex]->IsDead()) {
					splitBosses_[i]->SetPartnerPosition(splitBosses_[partnerIndex]->GetPosition());
				} else {
					splitBosses_[i]->ClearPartnerPosition();
				}
			}

			// 分裂ボス2体をそれぞれ更新する
			for (int i = 0; i < 2; ++i) {
				if (!splitBosses_[i] || splitBosses_[i]->IsDead()) {
					continue;
				}

				Vector3 oldBossPos = splitBosses_[i]->GetPosition();

				splitBosses_[i]->SetPlayerPosition(targetPos);

				// カットイン中はAppearだけ進める
				if (!isBossIntroPlaying_ || splitBosses_[i]->IsAppearing()) {
					splitBosses_[i]->Update();
				}

				if (splitBossObjs_[i]) {
					splitBossObjs_[i]->SetTranslation(splitBosses_[i]->GetPosition());
					splitBossObjs_[i]->SetRotation(splitBosses_[i]->GetRotation());
					splitBossObjs_[i]->SetScale(splitBosses_[i]->GetScale());
					splitBossObjs_[i]->Update();
				}
			}
		} else if (!boss_->IsDead()) {
			// ここは元の通常ボス更新のまま
		}
	}


	UpdateBeamWarning(mapManager);

	// =========================================================
	// ボス部屋で一定時間ごとにカードを落とす
	// =========================================================
	if (mapManager->IsBossMap() &&
		isBossCardRainEnabled_ &&
		!isBossIntroPlaying_) {

		int activeCardCount = 0;

		for (const auto& pickup : cardPickupManager->GetPickups()) {
			if (pickup.isActive) {
				activeCardCount++;
			}
		}

		// 上限未満の時だけタイマー進行
		if (activeCardCount < bossCardRainMax_) {
			bossCardRainTimer_--;

			if (bossCardRainTimer_ <= 0) {
				Vector3 center = mapManager->GetMapCenterFloorPosition(0.0f);
				Vector3 dropPos = center;

				// 他のカードに近すぎない位置を探す
				for (int attempt = 0; attempt < 10; ++attempt) {
					Vector3 candidate = center;
					candidate.x += static_cast<float>((rand() % 50) - 25);
					candidate.z += static_cast<float>((rand() % 50) - 25);
					candidate.y = -0.99f;

					bool tooClose = false;

					for (const auto& pickup : cardPickupManager->GetPickups()) {
						if (!pickup.isActive) {
							continue;
						}

						Vector3 diff = {
							candidate.x - pickup.position.x,
							0.0f,
							candidate.z - pickup.position.z
						};

						if (Length(diff) < 4.0f) {
							tooClose = true;
							break;
						}
					}

					if (!tooClose) {
						dropPos = candidate;
						break;
					}
				}

				Card dropCard = CardDatabase::GetRandomPlayerCard();
				cardPickupManager->AddPickup(dropPos, dropCard);

				// 次の出現までリセット
				bossCardRainTimer_ = bossCardRainInterval_;
			}
		}
	}

	// =========================================================
    // ボスの召喚リクエスト処理
    // =========================================================
	if (bossType_ == BossType::Split) {
		for (int i = 0; i < 2; ++i) {
			Boss* splitBoss = splitBosses_[i].get();
			if (!splitBoss || splitBoss->IsDead()) {
				continue;
			}

			if (splitBoss->GetSummonRequest()) {
				if (enemyManager && camera) {
					Vector3 summonCenter = splitBoss->GetPosition();
					summonCenter.y = 0.0f;
					enemyManager->SpawnBossMinions(
						splitBoss->GetSummonCount(),
						summonCenter,
						camera
					);
				}
				splitBoss->ClearSummonRequest();
			}
		}
	} else {
		if (boss_->GetSummonRequest()) {
			if (enemyManager && camera) {
				Vector3 summonCenter = boss_->GetPosition();
				summonCenter.y = 0.0f;
				enemyManager->SpawnBossMinions(
					boss_->GetSummonCount(),
					summonCenter,
					camera
				);
			}
			boss_->ClearSummonRequest();
		}
	}


	// =========================================================
    // ボスのカード使用処理
    // =========================================================
	if (player &&
		!player->IsDead() &&
		mapManager->IsBossMap() &&
		!isBossIntroPlaying_) {

		if (bossType_ == BossType::Split) {
			for (int i = 0; i < 2; ++i) {
				Boss* splitBoss = splitBosses_[i].get();
				if (!splitBoss || splitBoss->IsDead() || splitBoss->IsAppearing()) {
					continue;
				}

				if (splitBoss->GetCardUseRequest()) {
					if (bossCardSystem_) {
						Card useCard = splitBoss->GetSelectedCard();

						bossCardSystem_->UseCard(
							useCard,
							splitBoss->GetPosition(),
							splitBoss->GetRotation().y,
							false
						);
					}

					splitBoss->ClearCardUseRequest();
				}
			}
		} else if (!boss_->IsDead() && !boss_->IsAppearing()) {
			if (boss_->GetCardUseRequest()) {
				if (bossCardSystem_) {
					Card useCard = boss_->GetSelectedCard();

					bossCardSystem_->UseCard(
						useCard,
						boss_->GetPosition(),
						boss_->GetRotation().y,
						false
					);
				}

				boss_->ClearCardUseRequest();
			}
		}
	}

	// =========================================================
// ボス死亡時の処理
// =========================================================
	bool isBossBattleFinished = false;

	if (bossType_ == BossType::Split) {
		// 10階は2体とも倒れたら終了
		bool leftDead = !splitBosses_[0] || splitBosses_[0]->IsDead();
		bool rightDead = !splitBosses_[1] || splitBosses_[1]->IsDead();
		isBossBattleFinished = leftDead && rightDead;
	} else {
		// 5階など通常ボスは今まで通り
		isBossBattleFinished = boss_->IsDead();
	}

	if (isBossBattleFinished && !bossDeadHandled_) {
		// 経験値付与
		if (player) {
			player->AddExp(5);
		}

		// 倒した側の位置に近いところへカードを落とす
		Vector3 dropPos = boss_->GetPosition();
		if (bossType_ == BossType::Split) {
			dropPos = GetBossFocusPosition();
		}

		// ドロップカード処理
		if (bossType_ == BossType::Split) {
			// 分裂ボスは残っている個体から1枚落とす
			for (int i = 0; i < 2; ++i) {
				if (splitBosses_[i] && splitBosses_[i]->HasAnyCard()) {
					Card dropCard = splitBosses_[i]->GetRandomDropCard();
					if (dropCard.id != -1) {
						dropPos.y = mapManager->GetFloorSurfaceY(0.5f);
						cardPickupManager->AddPickup(dropPos, dropCard);
					}
					break;
				}
			}
		} else {
			if (boss_->HasAnyCard()) {
				Card dropCard = boss_->GetRandomDropCard();
				if (dropCard.id != -1) {
					dropPos.y = mapManager->GetFloorSurfaceY(0.5f);
					cardPickupManager->AddPickup(dropPos, dropCard);
				}
			}
		}

		// ボス部屋で倒したら中央に階段を置く
		if (mapManager->IsBossMap()) {
			LevelData& level = const_cast<LevelData&>(mapManager->GetLevelData());

			int centerX = level.width / 2;
			int centerZ = level.height / 2;

			level.tiles[centerZ][centerX] = 3;
			mapManager->SetStairsTile({ centerX, centerZ });
			mapManager->RebuildMapObjects();
		}

		bossDeadHandled_ = true;
	}


	// =========================================================
	// ボスのカード演出更新
	// =========================================================
	if (bossCardSystem_ &&
		mapManager->IsBossMap() &&
		!isBossIntroPlaying_) {


		bossCardSystem_->Update(
			player,
			nullptr,
			boss_.get(),
			playerPos,
			Vector3{ 0.0f, 0.0f, 0.0f },
			boss_->GetPosition(),
			mapManager->GetLevelData()
		);
	}
}

void BossManager::Draw(MapManager* mapManager) {
	// 必要な物が無ければ描画しない
	if (!boss_ || !mapManager) {
		return;
	}

	// ボス本体の描画
	if (mapManager->IsBossMap()) {
		if (beamWarningObj_) {
			beamWarningObj_->Draw();
		}

		if (bossType_ == BossType::Split) {
			// 分裂ボスは2体描画する
			for (int i = 0; i < 2; ++i) {
				if (splitBosses_[i] && !splitBosses_[i]->IsDead() && splitBosses_[i]->IsVisible() && splitBossObjs_[i]) {
					splitBossObjs_[i]->Draw();
				}
			}
		} else {
			// 通常ボスは今まで通り
			if (!boss_->IsDead() && boss_->IsVisible() && bossObj_) {
				bossObj_->Draw();
			}
		}
	}


	// ボスのカード演出描画
	if (bossCardSystem_) {
		bossCardSystem_->Draw();
	}
}

void BossManager::DrawHpBar(MapManager* mapManager) {
	// 無効な値なら描画しない
	if (!boss_ || !mapManager) {
		return;
	}

	// ボスHPバー描画
	bool shouldDrawHpBar = false;

	if (bossType_ == BossType::Split) {
		bool leftAlive = splitBosses_[0] && !splitBosses_[0]->IsDead();
		bool rightAlive = splitBosses_[1] && !splitBosses_[1]->IsDead();
		shouldDrawHpBar = leftAlive || rightAlive;
	} else {
		shouldDrawHpBar = !boss_->IsDead();
	}

	if (shouldDrawHpBar && mapManager->IsBossMap()) {
		if (bossHpBackSprite_) {
			bossHpBackSprite_->Draw();
		}
		if (bossHpFillSprite_) {
			bossHpFillSprite_->Draw();
		}
	}
}


Vector3 BossManager::GetBossFocusPosition() const {
	// 分裂ボス時は生きている個体の中心を見る
	if (bossType_ == BossType::Split) {
		Vector3 sum{ 0.0f, 0.0f, 0.0f };
		int aliveCount = 0;

		for (int i = 0; i < 2; ++i) {
			if (splitBosses_[i] && !splitBosses_[i]->IsDead()) {
				sum += splitBosses_[i]->GetPosition();
				aliveCount++;
			}
		}

		if (aliveCount > 0) {
			return sum / static_cast<float>(aliveCount);
		}

		return splitBossCenterPosition_;
	}

	return boss_ ? boss_->GetPosition() : Vector3{ 0.0f, 0.0f, 0.0f };
}

float BossManager::GetBossHpRate() const {
	// 分裂ボス時は2体の合計HP割合を使う
	if (bossType_ == BossType::Split) {
		int totalHp = 0;
		int totalMaxHp = 0;

		for (int i = 0; i < 2; ++i) {
			if (splitBosses_[i]) {
				totalHp += splitBosses_[i]->GetHP();
				totalMaxHp += splitBosses_[i]->GetMaxHP();
			}
		}

		if (totalMaxHp <= 0) {
			return 0.0f;
		}

		return static_cast<float>(totalHp) / static_cast<float>(totalMaxHp);
	}

	if (!boss_ || boss_->GetMaxHP() <= 0) {
		return 0.0f;
	}

	return static_cast<float>(boss_->GetHP()) / static_cast<float>(boss_->GetMaxHP());
}
