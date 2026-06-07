#include "game/player/Player.h"
#include "Engine/Base/Input.h"
#include "Engine/Base/TimeManager.h"
#include "engine/math/VectorMath.h"
#include "engine/rail/SplineRail.h"
#include <algorithm>
#include <cmath>

using namespace VectorMath;

// =====================================================================
//  Player：レール上を「距離(s)」で動くシンプルなモデル
//   - 主状態は currentDistance_（レール上を進んだ距離）だけ
//   - 入力は A/D=前後、W/S=奥/手前スイッチ（案1：レール基準で一貫）
//   - 旧 isHorizontal_（ワールド軸でキーを切替）/ traverseSign_ は廃止
// =====================================================================

void Player::Initialize(){
    position_ = { 0.0f, 0.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f };
    scale_    = { 1.0f, 1.0f, 1.0f };

    currentDistance_  = 0.0f;
    currentRailIndex_ = 0;
    moveSign_         = 1;
    switchCooldown_   = 0.0f;

    heightOffset_ = 0.0f;
    jumpVelocity_ = 0.0f;
    isGrounded_   = true;
}

void Player::Update(const std::vector<SplineRail>& allRails){
    if ( allRails.empty() ) return;
    if ( currentRailIndex_ < 0 || currentRailIndex_ >= ( int ) allRails.size() ) return;

    const float dt = Time::GetInstance()->GetDeltaTime(); // フレームレート非依存
    Input* input = Input::GetInstance();

    if ( switchCooldown_ > 0.0f ) { switchCooldown_ -= dt; }

    // =================================================================
    // 1. 入力（案1：A/D=前後移動、W/S=奥/手前への乗り換え）
    //    ワールド軸に依存しないのでレールが曲がっても操作が一貫する
    // =================================================================
    float moveInput = 0.0f;
    if ( input->Pushkey(DIK_D) ) moveInput += 1.0f;
    if ( input->Pushkey(DIK_A) ) moveInput -= 1.0f;

    int switchInput = 0;
    if ( input->Triggerkey(DIK_W) ) switchInput += 1; // 奥 (Z+)
    if ( input->Triggerkey(DIK_S) ) switchInput -= 1; // 手前 (Z-)

    // =================================================================
    // 2. 距離を進める（ds/dt の符号は moveInput * moveSign_ で決まる）
    // =================================================================
    if ( allRails[currentRailIndex_].nodes.size() >= 2 ) {
        currentDistance_ += moveInput * static_cast< float >( moveSign_ ) * moveSpeed_ * dt;
    }

    // =================================================================
    // 3. 終端での乗り継ぎ（はみ出した距離を次レールへ「持ち越す」）
    //    持ち越すことでジャンクションでカクつかず等速が保たれる
    //    入った側(front/back)に応じて moveSign_ を決め、同じキーで物理的に
    //    同じ方向へ進み続けられるようにする
    // =================================================================
    bool transitioned = false;
    {
        const float len = allRails[currentRailIndex_].GetLength();
        const int   inDir = ( moveInput > 0.0f ) ? 1 : ( moveInput < 0.0f ? -1 : moveSign_ );

        if ( currentDistance_ > len ) {
            // back端を越えた
            float over = currentDistance_ - len;
            int connIdx = allRails[currentRailIndex_].backConnIndex;
            bool enterFront = allRails[currentRailIndex_].backConnToFront;
            if ( connIdx >= 0 && connIdx < ( int ) allRails.size() ) {
                currentRailIndex_ = connIdx;
                float newLen = allRails[connIdx].GetLength();
                if ( enterFront ) { currentDistance_ = over;            moveSign_ =  inDir; }
                else              { currentDistance_ = newLen - over;   moveSign_ = -inDir; }
                currentDistance_ = std::clamp(currentDistance_, 0.0f, newLen);
                transitioned = true;
            } else {
                currentDistance_ = len; // 行き止まり
            }
        } else if ( currentDistance_ < 0.0f ) {
            // front端を越えた
            float over = -currentDistance_;
            int connIdx = allRails[currentRailIndex_].frontConnIndex;
            bool enterFront = allRails[currentRailIndex_].frontConnToFront;
            if ( connIdx >= 0 && connIdx < ( int ) allRails.size() ) {
                currentRailIndex_ = connIdx;
                float newLen = allRails[connIdx].GetLength();
                if ( enterFront ) { currentDistance_ = over;            moveSign_ =  inDir; }
                else              { currentDistance_ = newLen - over;   moveSign_ = -inDir; }
                currentDistance_ = std::clamp(currentDistance_, 0.0f, newLen);
                transitioned = true;
            } else {
                currentDistance_ = 0.0f;
            }
        }
    }

    // =================================================================
    // 4. 隣レーンへの乗り換え（W/S）
    //    ・押した瞬間だけ全レールを走査（軽い）
    //    ・相手レール上で「今の自分に一番近い点」に着地（固定ノードへワープしない）
    //    ・進行方向(facing)を保って moveSign_ を決める（操作が反転しない）
    // =================================================================
    if ( switchInput != 0 && switchCooldown_ <= 0.0f && !transitioned ) {
        const float kMaxXY = 3.0f; // 乗り換え可能な水平(XY)距離
        const float kMinZ  = 0.3f; // Z方向に最低これだけ離れた別レーンであること
        const float kMaxZ  = 8.0f; // 離れすぎは対象外

        int   bestRail  = -1;
        float bestDist  = 0.0f;
        float bestScore = 1e30f;

        for ( int j = 0; j < ( int ) allRails.size(); ++j ) {
            if ( j == currentRailIndex_ ) continue;
            if ( allRails[j].nodes.size() < 2 ) continue;

            // 相手レール上で今のプレイヤーに最も近い点
            float cd = allRails[j].GetClosestDistance(position_);
            Vector3 cp = allRails[j].GetPositionByDistance(cd);

            // 押した方向(W=奥/Z+, S=手前/Z-)に一致する別レーンか
            float zDiff = cp.z - position_.z;
            if ( switchInput > 0 && zDiff < kMinZ )  continue;
            if ( switchInput < 0 && zDiff > -kMinZ ) continue;
            if ( std::abs(zDiff) > kMaxZ ) continue;

            // 水平に近い（＝隣レーンらしい）か
            float dx = cp.x - position_.x, dy = cp.y - position_.y;
            float xy = std::sqrt(dx * dx + dy * dy);
            if ( xy > kMaxXY ) continue;

            // 一番近いZのレーンを優先
            float score = std::abs(zDiff);
            if ( score < bestScore ) { bestScore = score; bestRail = j; bestDist = cd; }
        }

        if ( bestRail >= 0 ) {
            // 乗り換え前の「向き(facing)」を基準に、進行方向を保つ
            float yaw = rotation_.y;
            Vector3 face = { std::sin(yaw), 0.0f, std::cos(yaw) };

            currentRailIndex_ = bestRail;
            currentDistance_  = bestDist; // 一番近い点へ（横にスッと移る・飛ばない）

            Vector3 newTan = allRails[bestRail].GetTangentByDistance(bestDist);
            // 相手レールの +s が今の向きと逆なら moveSign_ を反転（D=前 を維持）
            float dot = newTan.x * face.x + newTan.z * face.z;
            moveSign_ = ( dot >= 0.0f ) ? 1 : -1;

            // 向きを実際の進行方向に合わせて更新（lane移動でも視覚的に自然）
            Vector3 travel = { newTan.x * moveSign_, newTan.y * moveSign_, newTan.z * moveSign_ };
            if ( Length(travel) > 0.001f ) {
                rotation_.y = std::atan2(travel.x, travel.z);
                rotation_.x = 0.0f; rotation_.z = 0.0f;
            }
            switchCooldown_ = 0.25f;
        }
    }

    // =================================================================
    // 5. 最終的な距離をクランプ
    // =================================================================
    const SplineRail& rail = allRails[currentRailIndex_];
    const float len = rail.GetLength();
    currentDistance_ = std::clamp(currentDistance_, 0.0f, len);

    // =================================================================
    // 6. ジャンプ（m / m/s / m/s^2 で物理計算 → フレームレート非依存）
    // =================================================================
    if ( isGrounded_ && input->Triggerkey(DIK_SPACE) ) {
        jumpVelocity_ = jumpPower_;
        isGrounded_   = false;
    }
    jumpVelocity_ -= gravity_ * dt;
    heightOffset_ += jumpVelocity_ * dt;
    if ( heightOffset_ <= 0.0f ) {
        heightOffset_ = 0.0f;
        jumpVelocity_ = 0.0f;
        isGrounded_   = true;
    }

    // =================================================================
    // 7. 座標と向きを確定
    // =================================================================
    Vector3 basePos = rail.GetPositionByDistance(currentDistance_);
    basePos.y += heightOffset_;
    position_ = basePos;

    // 進行方向に向く（後退時は接線を反転）。移動入力がある時だけ更新
    Vector3 tangent = rail.GetTangentByDistance(currentDistance_);
    float ds = moveInput * static_cast< float >( moveSign_ );
    if ( ds < 0.0f ) {
        tangent.x = -tangent.x; tangent.y = -tangent.y; tangent.z = -tangent.z;
    }
    if ( Length(tangent) > 0.001f && moveInput != 0.0f ) {
        rotation_.y = std::atan2(tangent.x, tangent.z);
        rotation_.x = 0.0f;
        rotation_.z = 0.0f;
    }
}
