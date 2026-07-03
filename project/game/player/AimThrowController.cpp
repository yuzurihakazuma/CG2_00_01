#include "game/player/AimThrowController.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "engine/2d/Sprite.h"
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
    state_ = State::Idle;
    aimLocked_ = false;
}

// Q長押しで構え、矢印で「画面上のカーソル」を直感的に動かし、離して投げる。
//   ・ロック中はその敵へ。未ロックはカーソルの先(奥)へ投げる（クラフトワールド風）。
void AimThrowController::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, Camera* camera, float dt){
    if ( !camera ) return;
    Input* input = Input::GetInstance();
    Vector3 ppos = player.GetPosition();

    const float W = ( float ) WindowProc::GetInstance()->GetClientWidth();
    const float H = ( float ) WindowProc::GetInstance()->GetClientHeight();

    // ワールド点 → スクリーン画素（行ベクトル v*VP）。カメラ後方なら false。
    auto project = [&](const Vector3& w, float& px, float& py) -> bool {
        const Matrix4x4& vp = camera->GetViewProjectionMatrix();
        float cw = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
        if ( cw <= 0.0001f ) return false;
        float sx = ( w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0] ) / cw;
        float sy = ( w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1] ) / cw;
        px = ( sx * 0.5f + 0.5f ) * W;
        py = ( 1.0f - ( sy * 0.5f + 0.5f ) ) * H;
        return true;
        };

    if ( state_ == State::Idle ) {
        // Q を押し始めた＆卵を持っている → 構えに入る（地上/ジャンプ/踏ん張り中どこでもOK）
        if ( input->Pushkey(DIK_Q) && eggs.HeldCount() > 0 ) {
            state_ = State::Aiming;
            // 構え中も移動・ジャンプ・踏ん張りは受け付ける（狙いは矢印キーで別操作なので競合しない）
            // カーソルの初期位置：プレイヤーの少し前方上をスクリーン投影（無理なら画面中央）
            float px, py;
            Vector3 facing = { std::sin(player.GetRotation().y), 0.0f, std::cos(player.GetRotation().y) };
            Vector3 ahead = { ppos.x + facing.x * 5.0f, ppos.y + 1.0f, ppos.z + facing.z * 5.0f };
            if ( project(ahead, px, py) ) { cursorX_ = px; cursorY_ = py; }
            else { cursorX_ = W * 0.5f; cursorY_ = H * 0.45f; }
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
    cursorX_ = std::clamp(cursorX_, 0.0f, W);
    cursorY_ = std::clamp(cursorY_, 0.0f, H);

    // --- ロックオン対象を探す：カーソル(自由位置)に画面上で一番近い敵（吸いつき無し）---
    Enemy* lockEnemy = nullptr;
    float  bestPix = 1e9f, bestEx = 0.0f, bestEy = 0.0f;
    for ( auto& e : enemies.GetEnemies() ) {
        if ( !e->IsAlive() ) continue;
        float ex, ey;
        if ( !project(e->GetPosition(), ex, ey) ) continue; // 後方は対象外
        float dpix = std::sqrt(( ex - cursorX_ ) * ( ex - cursorX_ ) + ( ey - cursorY_ ) * ( ey - cursorY_ ));
        if ( dpix < bestPix ) { bestPix = dpix; lockEnemy = e.get(); bestEx = ex; bestEy = ey; }
    }
    float lockThresh = aimLocked_ ? 120.0f : 80.0f; // 粘り（付く<外れる）
    aimLocked_ = ( lockEnemy != nullptr && bestPix < lockThresh );

    Vector3 origin = { ppos.x, ppos.y + 0.5f, ppos.z };
    Vector3 throwDir = { 0.0f, 0.0f, 1.0f };
    float   throwSpeed = throwSpeedNormal_;  // 通常の投げ速度（ノードエディタから調整可）
    float   dispX = cursorX_, dispY = cursorY_;
    Vector3 cursorWorld;         // 狙い線の先端（奥に追従させる）

    if ( aimLocked_ ) {
        // ロック中：その敵へ。命中しやすいよう速度を上げる。カーソルは敵にピタッと合わせる。
        dispX = bestEx; dispY = bestEy;
        cursorWorld = lockEnemy->GetPosition();
        Vector3 t = cursorWorld - origin;
        float d = Length(t);
        if ( d > 1e-4f ) throwDir = { t.x / d, t.y / d, t.z / d };
        throwSpeed = throwSpeedLock_; // ★敵ロック時は速く（ノードエディタから調整可）
        DebugDraw::GetInstance()->Sphere(cursorWorld, lockEnemy->GetRadius() + 0.25f, { 1.0f, 0.2f, 0.2f, 1.0f }, 16);
    } else {
        // 未ロック：カーソルの画面位置を奥へアンプロジェクトした方向へ投げる（奥に投げ込める）。
        Matrix4x4 invVP = Inverse(camera->GetViewProjectionMatrix());
        float ndcX = cursorX_ / W * 2.0f - 1.0f;
        float ndcY = 1.0f - cursorY_ / H * 2.0f;
        auto unproj = [&](float z) -> Vector3 {
            float ow = ndcX * invVP.m[0][3] + ndcY * invVP.m[1][3] + z * invVP.m[2][3] + invVP.m[3][3];
            return { ( ndcX * invVP.m[0][0] + ndcY * invVP.m[1][0] + z * invVP.m[2][0] + invVP.m[3][0] ) / ow,
                     ( ndcX * invVP.m[0][1] + ndcY * invVP.m[1][1] + z * invVP.m[2][1] + invVP.m[3][1] ) / ow,
                     ( ndcX * invVP.m[0][2] + ndcY * invVP.m[1][2] + z * invVP.m[2][2] + invVP.m[3][2] ) / ow };
            };
        Vector3 nearP = unproj(0.0f), farP = unproj(1.0f);
        Vector3 ray = { farP.x - nearP.x, farP.y - nearP.y, farP.z - nearP.z };
        float rl = Length(ray);
        if ( rl > 1e-4f ) throwDir = { ray.x / rl, ray.y / rl, ray.z / rl };
        // 狙い線の先端＝カーソル方向の少し奥（奥に動かすと線もそちらへ追従する）
        cursorWorld = { origin.x + throwDir.x * 12.0f, origin.y + throwDir.y * 12.0f, origin.z + throwDir.z * 12.0f };
    }

    // 狙い線：プレイヤー → カーソルの先端（奥に合わせると線もそちらへ伸びる）
    DebugDraw::GetInstance()->Line(origin, cursorWorld, { 1.0f, 0.9f, 0.2f, 0.9f });

    // カーソル表示
    if ( cursorSprite_ ) {
        cursorSprite_->SetPosition({ dispX, dispY });
        cursorSprite_->SetSize(aimLocked_ ? Vector2{ 64.0f, 64.0f } : Vector2{ 48.0f, 48.0f });
        cursorSprite_->SetColor(aimLocked_ ? Vector4{ 1.0f, 0.25f, 0.2f, 1.0f }   // ロック=赤
                                           : Vector4{ 1.0f, 1.0f, 1.0f, 0.85f }); // 通常=白
        cursorSprite_->Update();
    }

    // Q を離した瞬間 → 投げる（ロック中=敵へ速く / 未ロック=カーソルの奥へ）。
    //   ※「投げずにキャンセル」したい時用のコマンドは別途キーで足せる（今は常に投げる）。
    if ( !input->Pushkey(DIK_Q) ) {
        // 投げる方向へプレイヤーの向きも合わせる（水平成分のyaw）
        if ( std::abs(throwDir.x) > 1e-4f || std::abs(throwDir.z) > 1e-4f ) {
            float yaw = std::atan2(throwDir.x, throwDir.z);
            Vector3 r = player.GetRotation();
            player.SetRotation({ r.x, yaw, r.z });
        }
        eggs.TryThrow(ppos, throwDir, throwSpeed);
        state_ = State::Idle;
        aimLocked_ = false;
    }
}

void AimThrowController::DrawSprite(){
    if ( cursorSprite_ && state_ == State::Aiming ) { cursorSprite_->Draw(); }
}
