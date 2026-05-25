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
    camera_ = camera;
    bossCastCardObjs_.clear();

    boss_ = std::make_unique<Boss>();
    boss_->Initialize();
    boss_->SetCamera(camera);
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
		splitBosses_[i]->SetCamera(camera);
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
        beamWarningObj_->SetScale({ 2.0f, 0.04f, 14.6f });

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

    for (int i = 0; i < 2; ++i) {
        splitBossHpBackSprites_[i] = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
        splitBossHpBackSprites_[i]->SetSize({ 120.0f, 14.0f });
        splitBossHpBackSprites_[i]->SetColor({ 0.0f, 0.0f, 0.0f, 0.75f });

        splitBossHpFillSprites_[i] = Sprite::Create("resources/white1x1.png", { 0.0f, 0.0f });
        splitBossHpFillSprites_[i]->SetSize({ 112.0f, 8.0f });
        splitBossHpFillSprites_[i]->SetColor({ 0.2f, 1.0f, 0.2f, 1.0f });
    }

    bossDeadHandled_ = false;
    bossIntroCameraState_ = IntroCameraState::None;
    bossIntroTimer_ = 0;
    isBossIntroPlaying_ = false;

    bossCardRainTimer_ = 0;
    isBossCardRainEnabled_ = true;

	bossAttackIntervalMinFrames_ = 120; // 通常ボスの最小攻撃間隔。2秒
	bossAttackIntervalMaxFrames_ = 240; // 通常ボスの最大攻撃間隔。4秒

	splitBossAttackIntervalMinFrames_[0] = 120; // 10階左ボスの最小攻撃間隔。2秒
	splitBossAttackIntervalMaxFrames_[0] = 240; // 10階左ボスの最大攻撃間隔。4秒
	splitBossAttackIntervalMinFrames_[1] = 120; // 10階右ボスの最小攻撃間隔。2秒
	splitBossAttackIntervalMaxFrames_[1] = 240; // 10階右ボスの最大攻撃間隔。4秒
}

void BossManager::Finalize() {
    bossCastCardObjs_.clear();
    bossCardSystem_.reset();
    beamWarningObj_.reset();
    bossHpBackSprite_.reset();
    bossHpFillSprite_.reset();
    for (int i = 0; i < 2; ++i) {
        splitBossHpBackSprites_[i].reset();
        splitBossHpFillSprites_[i].reset();
    }
    bossObj_.reset();
    boss_.reset();
	for (int i = 0; i < 2; ++i) {
		splitBossObjs_[i].reset();
		splitBosses_[i].reset();
	}
    camera_ = nullptr;

}

void BossManager::Reset() {
    if (boss_) {
        boss_->Initialize();
        boss_->SetScale({ 2.0f, 2.0f, 2.0f });
    }
	boss_->SetAttackIntervalRangeFrames(bossAttackIntervalMinFrames_, bossAttackIntervalMaxFrames_); // 通常ボス用の攻撃間隔範囲を設定

    if (bossCardSystem_) {
        bossCardSystem_->Reset();
    }
    bossCastCardObjs_.clear();

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

			splitBosses_[i]->SetAttackIntervalRangeFrames(
				splitBossAttackIntervalMinFrames_[i],
				splitBossAttackIntervalMaxFrames_[i]
			); // 10階の各ボスに個別の攻撃間隔範囲を設定

			// 最初は中央に重ねて出して、着地後に左右へ分裂させる
splitBosses_[i]->SetSpawnPosition(spawnPos);

// 分裂前は元ボスと同じ大きさで見せる
splitBosses_[i]->SetScale({ 2.0f, 2.0f, 2.0f });



			// 今のボス60HPを半分にして30HPずつにする
			splitBosses_[i]->SetMaxHP(18);
			splitBosses_[i]->SetHP(18);
			// 10階の分裂ボスだけ専用AIを使う
			splitBosses_[i]->SetSplitBehaviorEnabled(true);
		hasSplitParticleTriggered_ = false;
		hasSplitSeenAppearing_     = false;

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

bool BossManager::IsBossBattleFinished(MapManager* mapManager) const {
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
		bool leftDead = !splitBosses_[0] || (splitBosses_[0]->IsDead() && !splitBosses_[0]->IsDeathAnimationPlaying());
		bool rightDead = !splitBosses_[1] || (splitBosses_[1]->IsDead() && !splitBosses_[1]->IsDeathAnimationPlaying());
		return leftDead && rightDead && bossDeadHandled_;
	}

	// 5階など通常ボスは今まで通り
	return boss_->IsDead() && bossDeadHandled_;
}

bool BossManager::ShouldTriggerGameClear(MapManager* mapManager) const {
	if (!mapManager || mapManager->GetCurrentFloor() < 10) {
		return false;
	}

	return IsBossBattleFinished(mapManager);
}

bool BossManager::IsBossDeathAnimationPlaying() const {
	if (bossType_ == BossType::Split) {
		bool allDead = true;
		bool anyDeathAnimationPlaying = false;

		for (const auto& splitBoss : splitBosses_) {
			if (!splitBoss) {
				continue;
			}

			allDead = allDead && splitBoss->IsDead();
			anyDeathAnimationPlaying = anyDeathAnimationPlaying || splitBoss->IsDeathAnimationPlaying();
		}

		return allDead && anyDeathAnimationPlaying;
	}

	return boss_ && boss_->IsDeathAnimationPlaying();
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

	const float beamBaseLength = 14.0f;
	const float playerHitRadius = 0.6f;
	const float warningHalfWidth = 1.4f + playerHitRadius;
	const float warningHalfLength = beamBaseLength + playerHitRadius;
	Vector3 warningPos = {
		bossPos.x + forward.x * (beamBaseLength * 0.90f),
		mapManager->GetFloorSurfaceY(0.08f),
		bossPos.z + forward.z * (beamBaseLength * 0.90f)
	};

	float chargeRatio = 0.0f;
	const int castDuration = warningBoss->GetCastDurationCurrent();
	if (castDuration > 0) {
		chargeRatio = 1.0f - static_cast<float>(warningBoss->GetCastTimer()) / static_cast<float>(castDuration);
		chargeRatio = std::clamp(chargeRatio, 0.0f, 1.0f);
	}
	const Vector4 warningColor = {
		1.0f,
		0.90f - 0.88f * chargeRatio,
		0.0f,
		0.24f + 0.24f * chargeRatio
	};

	if (Model* model = beamWarningObj_->GetModel()) {
		if (Model::Material* material = model->GetMaterial()) {
			material->color = warningColor;
			material->emissive = 1.6f + 2.2f * chargeRatio;
		}
	}

	beamWarningObj_->SetTranslation(warningPos);
	beamWarningObj_->SetRotation({ 0.0f, bossYaw, 0.0f });
	beamWarningObj_->SetScale({ warningHalfWidth, 0.04f, warningHalfLength });
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
	if (bossType_ != BossType::Split &&
		(!boss_->IsDead() || boss_->IsDeathAnimationPlaying()) &&
		mapManager->IsBossMap()) {

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
				if (!splitBosses_[i]) {
					continue;
				}

				if (splitBosses_[i]->IsDead()) {
					if (splitBosses_[i]->IsDeathAnimationPlaying()) {
						splitBosses_[i]->Update();
						if (splitBossObjs_[i]) {
							splitBossObjs_[i]->SetTranslation(splitBosses_[i]->GetPosition());
							splitBossObjs_[i]->SetRotation(splitBosses_[i]->GetRotation());
							splitBossObjs_[i]->SetScale(splitBosses_[i]->GetScale());
							splitBossObjs_[i]->Update();
						}
					}
					continue;
				}

				Vector3 oldBossPos = splitBosses_[i]->GetPosition();

				splitBosses_[i]->SetPlayerPosition(targetPos);

				// カットイン中はAppearだけ進める
				if (!isBossIntroPlaying_ || splitBosses_[i]->IsAppearing()) {
					splitBosses_[i]->Update();
					// 10階の分裂ボスも、更新後に壁へめり込んでいたら元の位置へ戻す
					Vector3 bossPos = splitBosses_[i]->GetPosition();

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

							// 壁に当たったら更新前の位置に戻す
							if (Collision::IsCollision(bossAABB, blockAABB)) {
								splitBosses_[i]->SetPosition(oldBossPos);
								isBossHit = true;
								break;
							}
						}
					}
				}

				if (splitBossObjs_[i]) {
					splitBossObjs_[i]->SetTranslation(splitBosses_[i]->GetPosition());
					splitBossObjs_[i]->SetRotation(splitBosses_[i]->GetRotation());
					splitBossObjs_[i]->SetScale(splitBosses_[i]->GetScale());
					splitBossObjs_[i]->Update();
				}
			}

			// 両ボスの登場アニメが終わった瞬間に1回だけ分裂爆発を出す
			if (!hasSplitParticleTriggered_ && !isBossIntroPlaying_) {
				// まず Appear 中の個体を記憶する
				for (int i = 0; i < 2; ++i) {
					if (splitBosses_[i] && !splitBosses_[i]->IsDead() && splitBosses_[i]->IsAppearing()) {
						hasSplitSeenAppearing_ = true;
					}
				}
				// 「Appear を見た」後に全員が Appear を抜けたら発火
				if (hasSplitSeenAppearing_) {
					bool anyStillAppearing = false;
					for (int i = 0; i < 2; ++i) {
						if (splitBosses_[i] && !splitBosses_[i]->IsDead() && splitBosses_[i]->IsAppearing()) {
							anyStillAppearing = true;
						}
					}
					if (!anyStillAppearing) {
						hasSplitParticleTriggered_ = true;
						Vector3 c = splitBossCenterPosition_;

						// ① 全方向バースト（小粒・明るい紫白）
						for (int j = 0; j < 60; j++) {
							float a = static_cast<float>(rand() % 628) * 0.01f;
							float sp = 1.5f + static_cast<float>(rand() % 15) * 0.2f;
							float vy = 0.3f + static_cast<float>(rand() % 10) * 0.15f;
							Vector3 v = { std::cosf(a) * sp, vy, std::sinf(a) * sp };
							Vector4 col = (rand() % 2 == 0)
								? Vector4{ 0.85f, 0.5f, 1.0f, 1.0f }
								: Vector4{ 1.0f,  0.85f, 1.0f, 1.0f };
							GPUParticleManager::GetInstance()->Emit(c, v, 0.4f, 0.3f + static_cast<float>(rand() % 4) * 0.08f, col);
						}

						// ② 水平衝撃波リング 2段（紫・小粒）
						for (int ring = 0; ring < 2; ring++) {
							float h  = 0.3f + ring * 1.0f;
							float sp = 1.8f + ring * 0.4f;
							for (int j = 0; j < 32; j++) {
								float a = (3.14159f * 2.0f / 32.0f) * j;
								Vector3 v = { std::sinf(a) * sp, 0.0f, std::cosf(a) * sp };
								GPUParticleManager::GetInstance()->Emit({ c.x, c.y + h, c.z }, v, 0.35f, 0.4f, { 0.6f, 0.2f, 1.0f, 1.0f });
							}
						}

						// ③ 左右分裂ストリーク（どちらに分かれたかを表す）
						for (int j = 0; j < 24; j++) {
							float dir = (j < 12) ? -1.0f : 1.0f;
							Vector3 v = {
								dir * (2.0f + static_cast<float>(rand() % 10) * 0.25f),
								0.4f + static_cast<float>(rand() % 6) * 0.12f,
								static_cast<float>(rand() % 9 - 4) * 0.1f
							};
							GPUParticleManager::GetInstance()->Emit(c, v, 0.45f, 0.35f, { 0.8f, 0.4f, 1.0f, 1.0f });
						}

						// ④ 白い中心フラッシュ（小粒で複数、大玉にしない）
						for (int j = 0; j < 10; j++) {
							Vector3 v = { static_cast<float>(rand() % 7 - 3) * 0.08f, 0.08f, static_cast<float>(rand() % 7 - 3) * 0.08f };
							GPUParticleManager::GetInstance()->Emit({ c.x, c.y + 1.5f, c.z }, v, 0.15f, 0.45f, { 1.0f, 1.0f, 1.0f, 1.0f });
						}
					}
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

				Card dropCard = CardDatabase::GetRandomBossRoomPlayerCard();
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
							false,
							nullptr,
							splitBoss
						);
					}

					splitBoss->ClearCardUseRequest();

					bool leftAlive = splitBosses_[0] && !splitBosses_[0]->IsDead();
					bool rightAlive = splitBosses_[1] && !splitBosses_[1]->IsDead();

				
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
						false,
						nullptr,
						boss_.get()
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
		bool leftDead = !splitBosses_[0] || (splitBosses_[0]->IsDead() && !splitBosses_[0]->IsDeathAnimationPlaying());
		bool rightDead = !splitBosses_[1] || (splitBosses_[1]->IsDead() && !splitBosses_[1]->IsDeathAnimationPlaying());
		isBossBattleFinished = leftDead && rightDead;
	} else {
		// 5階など通常ボスは今まで通り
		isBossBattleFinished = boss_->IsDead() && !boss_->IsDeathAnimationPlaying();
	}

	if (isBossBattleFinished && !bossDeadHandled_) {
		// 経験値付与
		if (player) {
			player->AddExp(15);
		}

		if (enemyManager) {
			enemyManager->DefeatAllWithoutRewards();
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
					Card dropCard = CardDatabase::GetRandomBossRoomPlayerCard();
					if (false && dropCard.id != -1) {
						dropPos.y = mapManager->GetFloorSurfaceY(0.5f);
						cardPickupManager->AddPickup(dropPos, dropCard);
					}
					break;
				}
			}
		} else {
			if (boss_->HasAnyCard()) {
				Card dropCard = CardDatabase::GetRandomBossRoomPlayerCard();
				if (false && dropCard.id != -1) {
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

		// 分裂ボスでも通常ボスでも、倒れたら魔法を強制終了！
		if (bossCardSystem_) {
			bossCardSystem_->Reset();
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
			nullptr,
			playerPos,
			Vector3{ 0.0f, 0.0f, 0.0f },
			boss_->GetPosition(),
			mapManager->GetLevelData()
		);
	}
}

void BossManager::DrawCastCardForBoss(const Boss& boss) {
	if (!camera_ || boss.IsDead() || boss.IsAppearing() || !boss.IsVisible() || !boss.IsCasting()) {
		return;
	}

	const Card& currentCard = boss.GetSelectedCard();
	const int currentCardId = currentCard.id;
	if (currentCardId < 0 || currentCard.modelName.empty() || currentCard.modelName == "nullptr") {
		return;
	}

	auto it = bossCastCardObjs_.find(currentCardId);
	if (it == bossCastCardObjs_.end()) {
		auto cardObj = Obj3d::Create(currentCard.modelName);
		if (!cardObj) {
			return;
		}
		cardObj->SetCamera(camera_);
		it = bossCastCardObjs_.emplace(currentCardId, std::move(cardObj)).first;
	}

	auto& cardObj = it->second;
	if (!cardObj) {
		return;
	}

	const Vector3& bossPos = boss.GetPosition();
	const Vector3& bossScale = boss.GetBaseScale();
	const float height = bossScale.y * 1.65f + 1.1f;
	const float cardScale = (std::max)(1.0f, bossScale.y * 0.55f);
	const float elapsed = static_cast<float>((std::max)(0, boss.GetCastDurationCurrent() - boss.GetCastTimer()));
	const float rotateSpeed = 0.1f;
	const float yRotation = elapsed * rotateSpeed;

	cardObj->SetCamera(camera_);
	cardObj->SetTranslation({ bossPos.x, bossPos.y + height, bossPos.z });
	cardObj->SetScale({ cardScale, cardScale, cardScale });

	cardObj->SetRotation({ 0.0f, yRotation, 0.0f });
	cardObj->Update();
	cardObj->Draw();

	cardObj->SetRotation({ 0.0f, yRotation + 3.14159f, 0.0f });
	cardObj->Update();
	cardObj->Draw();
}

void BossManager::DrawBossCastCards(MapManager* mapManager) {
	if (!mapManager || !mapManager->IsBossMap() || isBossIntroPlaying_) {
		return;
	}

	if (bossType_ == BossType::Split) {
		for (const auto& splitBoss : splitBosses_) {
			if (splitBoss) {
				DrawCastCardForBoss(*splitBoss);
			}
		}
		return;
	}

	if (boss_) {
		DrawCastCardForBoss(*boss_);
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
				if (splitBosses_[i] && (!splitBosses_[i]->IsDead() || splitBosses_[i]->IsDeathAnimationPlaying()) && splitBosses_[i]->IsVisible() && splitBossObjs_[i]) {
					splitBossObjs_[i]->Draw();
				}
			}
		} else {
			// 通常ボスは今まで通り
			if ((!boss_->IsDead() || boss_->IsDeathAnimationPlaying()) && boss_->IsVisible() && bossObj_) {
				bossObj_->Draw();
			}
		}
	}

	DrawBossCastCards(mapManager);

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
		if (bossType_ == BossType::Split) {
			for (int i = 0; i < 2; ++i) {
				if (!splitBosses_[i] || splitBosses_[i]->IsDead()) {
					continue;
				}

				if (splitBossHpBackSprites_[i]) {
					splitBossHpBackSprites_[i]->Draw();
				}
				if (splitBossHpFillSprites_[i]) {
					splitBossHpFillSprites_[i]->Draw();
				}
			}
		} else {
			if (bossHpBackSprite_) {
				bossHpBackSprite_->Draw();
			}
			if (bossHpFillSprite_) {
				bossHpFillSprite_->Draw();
			}
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

		// 全滅後の死亡演出中は、最後に沈んでいる個体をカメラの注視点にする
		Vector3 deathAnimationSum{ 0.0f, 0.0f, 0.0f };
		int deathAnimationCount = 0;
		for (int i = 0; i < 2; ++i) {
			if (splitBosses_[i] && splitBosses_[i]->IsDeathAnimationPlaying()) {
				deathAnimationSum += splitBosses_[i]->GetPosition();
				deathAnimationCount++;
			}
		}

		if (deathAnimationCount > 0) {
			return deathAnimationSum / static_cast<float>(deathAnimationCount);
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

float BossManager::GetBossHpRateAt(int index) const {
	if (index < 0 || index >= static_cast<int>(splitBosses_.size())) {
		return 0.0f;
	}

	Boss* splitBoss = splitBosses_[index].get();
	if (!splitBoss || splitBoss->GetMaxHP() <= 0) {
		return 0.0f;
	}

	return static_cast<float>(splitBoss->GetHP()) / static_cast<float>(splitBoss->GetMaxHP());
}
