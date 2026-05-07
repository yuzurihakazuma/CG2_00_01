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

    // bossマップでは中央固定
    Vector3 spawnPos = mapManager->GetMapCenterFloorPosition(2.0f);

    boss_->Initialize();
    boss_->SetSpawnPosition(spawnPos);
    boss_->SetScale({ 2.0f, 2.0f, 2.0f });
    bossDeadHandled_ = false;

    if (bossObj_) {
        bossObj_->SetTranslation(boss_->GetPosition());
        bossObj_->SetRotation(boss_->GetRotation());
        bossObj_->SetScale(boss_->GetScale());
        bossObj_->Update();
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
	// ボス部屋でボスを倒していたらクリア
	if (!mapManager || !boss_) {
		return false;
	}

	return mapManager->IsBossMap() && boss_->IsDead() && bossDeadHandled_;
}

void BossManager::UpdateBeamWarning(MapManager* mapManager) {
    if (!beamWarningObj_) {
        return;
    }

    const bool shouldShow =
        boss_ &&
        mapManager &&
        mapManager->IsBossMap() &&
        !boss_->IsDead() &&
        !boss_->IsAppearing() &&
        boss_->IsCasting() &&
        boss_->GetSelectedCard().id == 104;

    if (!shouldShow) {
        beamWarningObj_->SetTranslation({ 9999.0f, -9999.0f, 9999.0f });
        beamWarningObj_->Update();
        return;
    }

    const Vector3& bossPos = boss_->GetPosition();
    float bossYaw = boss_->GetRotation().y;
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
	if (!boss_->IsDead() && mapManager->IsBossMap()) {
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
	if (boss_->GetSummonRequest()) {
		if (enemyManager && camera) {
			Vector3 summonCenter = boss_->GetPosition();
			// 召喚雑魚は通常湧きと同じ高さにそろえる。
			summonCenter.y = 0.0f;
			enemyManager->SpawnBossMinions(
				boss_->GetSummonCount(),
				summonCenter,
				camera
			);
		}
		boss_->ClearSummonRequest();
	}

	// =========================================================
	// ボスのカード使用処理
	// =========================================================
	if (player &&
		!boss_->IsDead() &&
		!player->IsDead() &&
		mapManager->IsBossMap() &&
		!isBossIntroPlaying_ &&
		!boss_->IsAppearing()) {

		Vector3 bossPos = boss_->GetPosition();

		if (boss_->GetCardUseRequest()) {
			if (bossCardSystem_) {
				Card useCard = boss_->GetSelectedCard();

				// 召喚カードなら雑魚生成
				if (useCard.id == 103) {
					if (enemyManager && camera) {
						Vector3 summonCenter = boss_->GetPosition();
						summonCenter.y = 0.0f;
						enemyManager->SpawnBossMinions(
							boss_->GetSummonCount(),
							summonCenter,
							camera
						);
					}
				}

				bossCardSystem_->UseCard(
					useCard,
					bossPos,
					boss_->GetRotation().y,
					false
				);
			}

			boss_->ClearCardUseRequest();
		}
	}

	// =========================================================
	// ボス死亡時の処理
	// =========================================================
	if (boss_->IsDead() && !bossDeadHandled_) {
		// 経験値追加
		if (player) {
			player->AddExp(5);
		}

		// ドロップカード処理
		if (boss_->HasAnyCard()) {
			Card dropCard = boss_->GetRandomDropCard();
			if (dropCard.id != -1) {
				Vector3 dropPos = boss_->GetPosition();

				// 地面の高さに調整
				dropPos.y = mapManager->GetFloorSurfaceY(0.5f);

				cardPickupManager->AddPickup(dropPos, dropCard);
			}
		}

		// ボス部屋で倒したら中央に階段を出す
		if (mapManager->IsBossMap()) {
			LevelData& level = const_cast<LevelData&>(mapManager->GetLevelData());

			int centerX = level.width / 2;
			int centerZ = level.height / 2;

			// 中央タイルを階段にする
			level.tiles[centerZ][centerX] = 3;

			// 階段タイル情報も更新する
			mapManager->SetStairsTile({ centerX, centerZ });

			// 見た目を更新する
			mapManager->RebuildMapObjects();
		}

		bossDeadHandled_ = true;
	}

	// =========================================================
	// ボスのカード演出更新
	// =========================================================
	if (!boss_->IsDead() &&
		bossCardSystem_ &&
		mapManager->IsBossMap() &&
		!isBossIntroPlaying_ &&
		!boss_->IsAppearing()) {

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
	if (!boss_->IsDead() && boss_->IsVisible() && mapManager->IsBossMap()) {
		if (beamWarningObj_) {
			beamWarningObj_->Draw();
		}
		if (bossObj_) {
			bossObj_->Draw();
		}
	}

	// ボスのカード演出描画
	if (bossCardSystem_) {
		bossCardSystem_->Draw();
	}
}

void BossManager::DrawHpBar(MapManager* mapManager) {
	// 必要な物が無ければ描画しない
	if (!boss_ || !mapManager) {
		return;
	}

	// ボスHPバー描画
	if (!boss_->IsDead() && mapManager->IsBossMap()) {
		if (bossHpBackSprite_) {
			bossHpBackSprite_->Draw();
		}
		if (bossHpFillSprite_) {
			bossHpFillSprite_->Draw();
		}
	}
}
