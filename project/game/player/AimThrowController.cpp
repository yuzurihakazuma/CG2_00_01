#include "game/player/AimThrowController.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "engine/2d/Sprite.h"
#include "engine/audio/AudioManager.h"
#include "engine/base/Input.h"
#include "engine/base/TimeManager.h"
#include "engine/base/WindowProc.h"
#include "engine/camera/Camera.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/math/VectorMath.h"
#include "engine/math/Matrix4x4.h"

#include <algorithm>
#include <cmath>

using namespace VectorMath;
using namespace MatrixMath;

AimThrowController::AimThrowController() = default;
AimThrowController::~AimThrowController() = default;

void AimThrowController::Initialize(uint32_t cursorTexSrv){
    cursorSprite_ = Sprite::Create(cursorTexSrv, { 640.0f, 360.0f });
    cursorSprite_->SetSize({ 56.0f, 56.0f });
    lockRingSprite_ = Sprite::Create(cursorTexSrv, { 640.0f, 360.0f });
    lockRingSprite_->SetSize({ 96.0f, 96.0f });
    state_ = State::Idle;
    aimLocked_ = false;
}

// Q長押しで構え、矢印で「画面上のカーソル」を直感的に動かし、離して投げる。
//   ・ロック中はその敵へ。未ロックはカーソルの先(奥)へ投げる（クラフトワールド風）。
void AimThrowController::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, Camera* camera, float dt){
    if ( !camera ) return;
    Input* input = Input::GetInstance();
    Vector3 playerPos = player.GetPosition();

    const float screenWidth = ( float ) WindowProc::GetInstance()->GetClientWidth();
    const float screenHeight = ( float ) WindowProc::GetInstance()->GetClientHeight();

    // ワールド点 → スクリーン画素。NDC計算は MatrixMath::WorldToNdc に一本化（カメラ後方なら false）
    auto project = [&](const Vector3& worldPos, float& screenX, float& screenY) -> bool {
        Vector2 ndc;
        if ( !WorldToNdc(worldPos, camera->GetViewProjectionMatrix(), ndc) ) return false;
        screenX = ( ndc.x * 0.5f + 0.5f ) * screenWidth;
        screenY = ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * screenHeight;
        return true;
        };

    if ( state_ == State::Idle ) {
        // Q を押し始めた＆卵を持っている → 構えに入る（地上/ジャンプ/踏ん張り中どこでもOK）
        if ( input->Pushkey(DIK_Q) && eggs.HeldCount() > 0 ) {
            state_ = State::Aiming;
            aimAxisYaw_ = player.GetRotation().y; // レール軸の基準は構え開始時の向きで固定
                                                  // （狙いでプレイヤーが振り向いても軸はブレない）
            // 構え中も移動・ジャンプ・踏ん張りは受け付ける（狙いは矢印キーで別操作なので競合しない）
            // カーソルの初期位置：プレイヤーの少し前方上をスクリーン投影（無理なら画面中央）
            float projectedX, projectedY;
            Vector3 facing = { std::sin(player.GetRotation().y), 0.0f, std::cos(player.GetRotation().y) };
            Vector3 ahead = { playerPos.x + facing.x * 5.0f, playerPos.y + 1.0f, playerPos.z + facing.z * 5.0f };
            if ( project(ahead, projectedX, projectedY) ) { cursorX_ = projectedX; cursorY_ = projectedY; }
            else { cursorX_ = screenWidth * 0.5f; cursorY_ = screenHeight * 0.45f; }
            // ★1f点滅対策：入場フレームのうちにカーソル位置を確定（return せず下の処理へ落ちる）
            if ( cursorSprite_ ) { cursorSprite_->SetPosition({ cursorX_, cursorY_ }); cursorSprite_->Update(); }
        } else {
            return; // 構えていない時は何もしない
        }
    }

    // --- 構え中（Aiming）---
    // 矢印キーで「画面上のカーソル」を動かす（直感的：上=上 / 右=右 / 等速）。
    const float kCursorSpeed = 620.0f; // px/s
    if ( input->Pushkey(DIK_UP) )    cursorY_ -= kCursorSpeed * dt;
    if ( input->Pushkey(DIK_DOWN) )  cursorY_ += kCursorSpeed * dt;
    if ( input->Pushkey(DIK_LEFT) )  cursorX_ -= kCursorSpeed * dt;
    if ( input->Pushkey(DIK_RIGHT) ) cursorX_ += kCursorSpeed * dt;
    cursorX_ = std::clamp(cursorX_, 0.0f, screenWidth);
    cursorY_ = std::clamp(cursorY_, 0.0f, screenHeight);

    // --- ロックオン対象を探す：カーソル(自由位置)に画面上で一番近い敵（吸いつき無し）---
    Enemy* lockEnemy = nullptr;
    float  bestPixelDist = 1e9f, bestEnemyX = 0.0f, bestEnemyY = 0.0f;
    for ( auto& enemy : enemies.GetEnemies() ) {
        if ( !enemy->IsAlive() ) continue;
        float enemyScreenX, enemyScreenY;
        if ( !project(enemy->GetPosition(), enemyScreenX, enemyScreenY) ) continue; // 後方は対象外
        float pixelDist = std::sqrt(( enemyScreenX - cursorX_ ) * ( enemyScreenX - cursorX_ ) + ( enemyScreenY - cursorY_ ) * ( enemyScreenY - cursorY_ ));
        if ( pixelDist < bestPixelDist ) { bestPixelDist = pixelDist; lockEnemy = enemy.get(); bestEnemyX = enemyScreenX; bestEnemyY = enemyScreenY; }
    }
    float lockThreshold = aimLocked_ ? 120.0f : 80.0f; // 粘り（付く<外れる）
    aimLocked_ = ( lockEnemy != nullptr && bestPixelDist < lockThreshold );

    Vector3 origin = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
    Vector3 throwDir = { 0.0f, 0.0f, 1.0f };
    float   throwSpeed = throwSpeedNormal_;  // 通常の投げ速度（ノードエディタから調整可）
    float   displayX = cursorX_, displayY = cursorY_;
    Vector3 cursorWorld;         // 狙い線の先端（奥に追従させる）

    if ( aimLocked_ ) {
        // ロック中：その敵へ。命中しやすいよう速度を上げる。カーソルは敵にピタッと合わせる。
        displayX = bestEnemyX; displayY = bestEnemyY;
        cursorWorld = lockEnemy->GetPosition();
        Vector3 toTarget = cursorWorld - origin;
        float targetDist = Length(toTarget);
        if ( targetDist > 1e-4f ) throwDir = { toTarget.x / targetDist, toTarget.y / targetDist, toTarget.z / targetDist };
        throwSpeed = throwSpeedLock_; // ★敵ロック時は速く（ノードエディタから調整可）
        DebugDraw::GetInstance()->Sphere(cursorWorld, lockEnemy->GetRadius() + 0.25f, { 1.0f, 0.2f, 0.2f, 1.0f }, 16);
    } else {
        // 未ロック：レール軸（プレイヤーの向き）に沿ってだけ投げる。
        //   奥への投げ込みは敵にロックオンした時だけ＝レールアクションとして狙いが迷わない。
        //   カーソルの上下は投げの山なり（ピッチ）にだけ効く
        Vector3 axis = { std::sin(aimAxisYaw_), 0.0f, std::cos(aimAxisYaw_) };
        float pitch = std::clamp(( screenHeight * 0.5f - cursorY_ ) / ( screenHeight * 0.5f ), -0.2f, 0.9f);
        // カーソルが画面上でプレイヤーのどちら側にあるかで、軸の前/後ろを選ぶ（後ろにも投げられる）
        float side = 1.0f;
        float playerScreenX = 0.0f, playerScreenY = 0.0f;
        float axisScreenX = 0.0f, axisScreenY = 0.0f;
        if ( project(origin, playerScreenX, playerScreenY)
          && project({ origin.x + axis.x, origin.y, origin.z + axis.z }, axisScreenX, axisScreenY) ) {
            float axisDX = axisScreenX - playerScreenX;
            if ( std::abs(axisDX) > 1e-3f && ( cursorX_ - playerScreenX ) * axisDX < 0.0f ) side = -1.0f;
        }
        Vector3 dir = { axis.x * side, pitch, axis.z * side };
        float dirLength = Length(dir);
        if ( dirLength > 1e-4f ) throwDir = { dir.x / dirLength, dir.y / dirLength, dir.z / dirLength };
        // 狙い線の先端＝軸方向の少し奥
        cursorWorld = { origin.x + throwDir.x * 12.0f, origin.y + throwDir.y * 12.0f, origin.z + throwDir.z * 12.0f };
    }

    // プレイヤーの見た目もこの狙い方向を向く（GamePlayScene が毎フレーム反映）
    aimYaw_ = std::atan2(throwDir.x, throwDir.z);

    // 狙い線：プレイヤー → カーソルの先端（奥に合わせると線もそちらへ伸びる）
    DebugDraw::GetInstance()->Line(origin, cursorWorld, { 1.0f, 0.9f, 0.2f, 0.9f });

    // カーソル表示（視認性向上：脈動＋通常=黄(狙い線と同色)/ロック=赤で大きく）
    pulseT_ += dt;
    float pulse = 1.0f + 0.10f * std::sin(pulseT_ * 9.0f); // ふわふわ脈動して目を引く
    if ( cursorSprite_ ) {
        cursorSprite_->SetPosition({ displayX, displayY });
        float baseSize = aimLocked_ ? 68.0f : 52.0f;
        cursorSprite_->SetSize({ baseSize * pulse, baseSize * pulse });
        cursorSprite_->SetColor(aimLocked_ ? Vector4{ 1.0f, 0.25f, 0.2f, 1.0f }    // ロック=赤
                                           : Vector4{ 1.0f, 0.95f, 0.35f, 0.95f }); // 通常=黄
        cursorSprite_->Update();
    }
    // ロックオンリング：ロック中の敵の周りでゆっくり回りながら収縮（「捕まえてる」感）
    if ( lockRingSprite_ && aimLocked_ ) {
        lockRingSprite_->SetPosition({ displayX, displayY });
        float ring = 104.0f + 10.0f * std::sin(pulseT_ * 6.0f);
        lockRingSprite_->SetSize({ ring, ring });
        lockRingSprite_->SetRotation(pulseT_ * 2.5f);
        lockRingSprite_->SetColor({ 1.0f, 0.4f, 0.25f, 0.5f });
        lockRingSprite_->Update();
    }

    // Q を離した瞬間 → 投げる（ロック中=敵へ速く / 未ロック=カーソルの奥へ）。
    //   ※「投げずにキャンセル」したい時用のコマンドは別途キーで足せる（今は常に投げる）。
    // Xでキャンセル：投げずに構え解除。上にのせていた卵は消費されず後ろ（ストック）へ戻る
    if ( input->Triggerkey(DIK_X) ) {
        canceled_ = true;
        Reset();
        return;
    }

    if ( !input->Pushkey(DIK_Q) ) {
        // 投げる方向へプレイヤーの向きも合わせる（水平成分のyaw）
        if ( std::abs(throwDir.x) > 1e-4f || std::abs(throwDir.z) > 1e-4f ) {
            float yaw = std::atan2(throwDir.x, throwDir.z);
            Vector3 rotation = player.GetRotation();
            player.SetRotation({ rotation.x, yaw, rotation.z });
        }
        if ( eggs.TryThrow(playerPos, throwDir, throwSpeed) ) {
            AudioManager::GetInstance()->PlayWave("resources/se/eggThrow.wav", false, 0.55f); // 投擲音
        }
        state_ = State::Idle;
        aimLocked_ = false;
    }
}

void AimThrowController::DrawSprite(){
    if ( state_ != State::Aiming ) return;
    if ( lockRingSprite_ && aimLocked_ ) { lockRingSprite_->Draw(); } // リングはカーソルの下に
    if ( cursorSprite_ ) { cursorSprite_->Draw(); }
}
