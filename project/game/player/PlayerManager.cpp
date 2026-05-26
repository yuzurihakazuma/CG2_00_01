#define NOMINMAX
#include <algorithm>

#include "game/player/PlayerManager.h"
#include "game/player/Player.h"
#include "Engine/Base/Input.h"
#include "Engine/Camera/Camera.h"
#include "Engine/Camera/DebugCamera.h"
#include "game/enemy/BossManager.h"
#include "game/map/MapManager.h"
#include "engine/collision/Collision.h"

using namespace VectorMath;

void PlayerManager::Initialize(Camera* camera) {
    // プレイヤー本体を作成
    player_ = std::make_unique<Player>();
    player_->Initialize();
    player_->SetPosition(playerPos_);
    player_->SetScale(playerScale_);
    player_->SetCamera(camera);
}

void PlayerManager::Finalize() {
    // プレイヤー関連を解放
    player_.reset();
}

void PlayerManager::Update(
    Input* input,
    MapManager* mapManager,
    DebugCamera* debugCamera,
    BossManager* bossManager,
    bool isMagicCasting
) {
    // 必要なものが無ければ更新しない
    if (!player_ || !mapManager || !input) {
        return;
    }

    // ボス登場演出中かどうかを確認
    bool isBossIntroPlaying = bossManager ? bossManager->IsBossIntroPlaying() : false;

    // デバッグカメラ中またはボス演出中は入力を止める
    if ((debugCamera && debugCamera->IsActive()) || isBossIntroPlaying) {
        player_->SetInputEnable(false);
    } else {
        player_->SetInputEnable(true);
    }

    // 衝突時に戻すため更新前の位置を保存
    Vector3 oldPos = player_->GetPosition();

    // プレイヤー本体を更新
    player_->Update(isMagicCasting);
    playerPos_ = player_->GetPosition();

    // プレイヤーの当たり判定を作成
    AABB playerAABB;
    playerAABB.min = { playerPos_.x - 0.5f, playerPos_.y - 0.5f, playerPos_.z - 0.5f };
    playerAABB.max = { playerPos_.x + 0.5f, playerPos_.y + 0.5f, playerPos_.z + 0.5f };

    const LevelData& level = mapManager->GetLevelData();

    // 現在フレームでプレイヤーが移動した量を求める
    Vector3 newPos = player_->GetPosition();
    Vector3 moveDelta = newPos - oldPos;

    // 指定座標でプレイヤーAABBが壁に当たるかを調べる
    auto IsWallHit = [&](const Vector3& checkPos) -> bool {
        AABB playerAABB;
        playerAABB.min = { checkPos.x - 0.5f, checkPos.y - 0.5f, checkPos.z - 0.5f };
        playerAABB.max = { checkPos.x + 0.5f, checkPos.y + 0.5f, checkPos.z + 0.5f };

        int centerGridX = static_cast<int>(std::round(checkPos.x / level.tileSize));
        int centerGridZ = static_cast<int>(std::round(checkPos.z / level.tileSize));

        int startX = std::max(0, centerGridX - 1);
        int endX = std::min(level.width - 1, centerGridX + 1);
        int startZ = std::max(0, centerGridZ - 1);
        int endZ = std::min(level.height - 1, centerGridZ + 1);

        for (int z = startZ; z <= endZ; z++) {
            for (int x = startX; x <= endX; x++) {
                // 壁タイル以外は無視する
                if (level.tiles[z][x] != 1 && level.tiles[z][x] != 2) {
                    continue;
                }

                float worldX = x * level.tileSize;
                float worldZ = z * level.tileSize;

                AABB blockAABB;
                blockAABB.min = { worldX - 1.0f, level.baseY, worldZ - 1.0f };
                blockAABB.max = { worldX + 1.0f, level.baseY + 2.0f, worldZ + 1.0f };

                if (Collision::IsCollision(playerAABB, blockAABB)) {
                    return true;
                }
            }
        }

        return false;
        };

    // まず元の位置から開始する
    Vector3 resolvedPos = oldPos;

    // x方向だけ先に反映して、壁に当たらなければ通す
    if (!IsWallHit({ oldPos.x + moveDelta.x, oldPos.y, oldPos.z })) {
        resolvedPos.x = oldPos.x + moveDelta.x;
    }

    // 次にx反映後の位置を基準にz方向を判定する
    if (!IsWallHit({ resolvedPos.x, oldPos.y, oldPos.z + moveDelta.z })) {
        resolvedPos.z = oldPos.z + moveDelta.z;
    }

    // 最終的に通れた分だけプレイヤー位置へ反映する
    player_->SetPosition(resolvedPos);
    playerPos_ = resolvedPos;

}

void PlayerManager::Draw() {
    // 死亡しておらず表示可能なら描画
    if (player_ && player_->IsVisible()) {
        player_->Draw();
    }
}

void PlayerManager::DrawAfterimage() {
    if (player_ && player_->IsVisible()) {
        player_->DrawAfterimage();
    }
}

void PlayerManager::RespawnInRoom(MapManager* mapManager, Camera* camera) {
    // 必要なものが無ければ再配置しない
    if (!mapManager || !player_) {
        return;
    }

    // ボス部屋と通常部屋で出現位置を分ける
    if (mapManager->IsBossMap()) {
        // 通常部屋のスポーン高さ(baseY + 1.0f + 1.5f)とそろえる。
        Vector3 center = mapManager->GetMapCenterFloorPosition(1.5f);
        playerPos_ = center;
        playerPos_.z -= 6.0f;
    } else {
        playerPos_ = mapManager->GetRandomPlayerSpawnPosition(1.5f);
    }

    // スケールを初期値に戻す
    playerScale_ = { 1.0f, 1.0f, 1.0f };

    // プレイヤー本体に位置とスケールと回転を反映
    player_->SetPosition(playerPos_);
    player_->SetScale(playerScale_);
    player_->SetRotation({ 0.0f, 0.0f, 0.0f });

    // カメラもプレイヤー初期位置に合わせる
    if (camera) {
        camera->SetTranslation({
            playerPos_.x,
            playerPos_.y + 15.0f,
            playerPos_.z - 15.0f
            });
        camera->SetRotation({ 0.9f, 0.0f, 0.0f });
        camera->Update();

        // プレイヤー側にも使用カメラを再設定
        player_->SetCamera(camera);
    }
}

void PlayerManager::SetPosition(const Vector3& pos) {
    // 保存位置を更新
    playerPos_ = pos;

    // プレイヤー本体にも反映
    if (player_) {
        player_->SetPosition(pos);
    }
}

void PlayerManager::SetScale(const Vector3& scale) {
    // 保存スケールを更新
    playerScale_ = scale;

    // プレイヤー本体にも反映
    if (player_) {
        player_->SetScale(scale);
    }
}

bool PlayerManager::IsDead() const {
    return player_ ? player_->IsDead() : false;
}

bool PlayerManager::IsDeathAnimationFinished() const {
    return player_ ? player_->IsDeathAnimationFinished() : false;
}

bool PlayerManager::IsVisible() const {
    return player_ ? player_->IsVisible() : false;
}

void PlayerManager::ApplyInfiniteMode(bool isInfiniteMode) {
    // プレイヤーがいない時は何もしない
    if (!player_) {
        return;
    }

    // 無限モードのON/OFFに合わせて「完全無敵」を切り替える！
    player_->SetDebugInvincible(isInfiniteMode);

    // 無限モードじゃない時はここで終わる
    if (!isInfiniteMode) {
        return;
    }

    player_->SetMaxCost(999);
    player_->SetCost(player_->GetMaxCost());
    player_->SetHP(player_->GetMaxHP());
}

int PlayerManager::GetHP() const {
    return player_ ? player_->GetHP() : 0;
}

int PlayerManager::GetMaxHP() const {
    return player_ ? player_->GetMaxHP() : 0;
}

int PlayerManager::GetCost() const {
    return player_ ? player_->GetCost() : 0;
}

int PlayerManager::GetMaxCost() const {
    return player_ ? player_->GetMaxCost() : 0;
}

int PlayerManager::GetLevel() const {
    return player_ ? player_->GetLevel() : 0;
}

int PlayerManager::GetExp() const {
    return player_ ? player_->GetExp() : 0;
}

int PlayerManager::GetNextLevelExp() const {
    return player_ ? player_->GetNextLevelExp() : 0;
}

bool PlayerManager::IsDodging() const {
    return player_ ? player_->IsDodging() : false;
}

bool PlayerManager::IsActionLocked() const {
    return player_ ? player_->IsActionLocked() : false;
}

bool PlayerManager::IsHit() const {
    return player_ ? player_->IsHit() : false;
}

bool PlayerManager::IsInvincible() const {
    return player_ ? player_->IsInvincible() : false;
}

bool PlayerManager::CanUseCost(int cost) const {
    return player_ ? player_->CanUseCost(cost) : false;
}

void PlayerManager::UseCost(int cost) {
    if (player_) {
        player_->UseCost(cost);
    }
}

void PlayerManager::AddExp(int value) {
    if (player_) {
        player_->AddExp(value);
    }
}

float PlayerManager::GetRotationY() const {
    return player_ ? player_->GetRotation().y : 0.0f;
}

void PlayerManager::Reset() {
    // プレイヤーが無ければ何もしない
    if (!player_) {
        return;
    }

    // スケールを初期値に戻す
    playerScale_ = { 1.0f, 1.0f, 1.0f };
    player_->SetScale(playerScale_);

    // 保存位置を現在のプレイヤー座標に合わせる
    playerPos_ = player_->GetPosition();
}
