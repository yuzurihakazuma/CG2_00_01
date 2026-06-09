#include "game/player/Player.h"
#include "Engine/Base/Input.h"
#include "Engine/Base/TimeManager.h"
#include "engine/math/VectorMath.h"
#include "engine/rail/SplineRail.h"
#include <algorithm>
#include <cmath>

using namespace VectorMath;

// =====================================================================
//  Player：レール上を「距離(s)」で動く。レールのタイプで操作キーが変わる。
//   - 横レール(Horizontal) … A/D で移動、W/S で縦レールへ乗り換え
//   - 縦レール(Vertical)   … W/S で移動、A/D で横レールへ乗り換え
//   - 移動は「ワールド方向の意思」で行う（D=世界+X / W=世界+Z）。
//     ノード順に依存しないので、どちら向きに引いたレールでも操作が一貫する。
//   - 同じタイプの連結レールは地続きで持ち越し。違うタイプの境目は自動で
//     進まず、プレイヤーが乗り換えキーを押した時だけ移る（行くか選べる）。
// =====================================================================

// 横レール=世界X / 縦レール=世界Z に沿った「進む向きの符号」を返すヘルパ。
//   moveInput(±1) を入れると、ds（距離の増分符号）が返る。
//   接線のその軸成分の符号に合わせるので、レールのノード順に依存しない。
static float MoveAlongSign(const SplineRail& rail, float currentDistance, float moveInput);

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

    const SplineRail& cur = allRails[currentRailIndex_];
    if ( cur.nodes.size() < 2 ) return;
    const bool curHorizontal = ( cur.type == SplineRail::RailType::Horizontal );

    // =================================================================
    // 1. 入力（レールのタイプで「移動キー」と「乗り換えキー」が入れ替わる）
    // =================================================================
    float moveInput   = 0.0f; // 今のレールを進む入力
    int   switchInput = 0;    // 別タイプのレールへ乗り換える入力
    if ( curHorizontal ) {
        if ( input->Pushkey(DIK_D) )    moveInput   += 1.0f; // 右(+X)
        if ( input->Pushkey(DIK_A) )    moveInput   -= 1.0f; // 左(-X)
        if ( input->Triggerkey(DIK_W) ) switchInput += 1;    // 奥(+Z)の縦レールへ
        if ( input->Triggerkey(DIK_S) ) switchInput -= 1;    // 手前(-Z)の縦レールへ
    } else {
        if ( input->Pushkey(DIK_W) )    moveInput   += 1.0f; // 奥(+Z)
        if ( input->Pushkey(DIK_S) )    moveInput   -= 1.0f; // 手前(-Z)
        if ( input->Triggerkey(DIK_D) ) switchInput += 1;    // 右(+X)の横レールへ
        if ( input->Triggerkey(DIK_A) ) switchInput -= 1;    // 左(-X)の横レールへ
    }

    // =================================================================
    // 2. 今のレールをワールド方向に沿って進める
    //    （横レール:D=世界+X / 縦レール:W=世界+Z。ノード順に依存しない）
    // =================================================================
    currentDistance_ += MoveAlongSign(cur, currentDistance_, moveInput) * moveSpeed_ * dt;

    // =================================================================
    // 3. 終端：同じタイプの連結レールへは地続きで持ち越し。
    //    違うタイプ／接続なしは端で停止（自動で別タイプへ突っ込まない）。
    // =================================================================
    bool transitioned = false;
    if ( switchCooldown_ <= 0.0f ) {
        const float len = cur.GetLength();

        auto tryContinue = [&](int connIdx, bool enterFront, float over) -> bool{
            if ( connIdx < 0 || connIdx >= ( int ) allRails.size() ) return false;
            if ( allRails[connIdx].type != cur.type ) return false; // 同じタイプだけ地続き
            float newLen = allRails[connIdx].GetLength();
            currentRailIndex_ = connIdx;
            currentDistance_  = enterFront ? over : ( newLen - over );
            currentDistance_  = std::clamp(currentDistance_, 0.0f, newLen);
            return true;
            };

        if ( currentDistance_ > len ) {
            if ( tryContinue(cur.backConnIndex, cur.backConnToFront, currentDistance_ - len) ) transitioned = true;
            else currentDistance_ = len;   // 端で停止
        } else if ( currentDistance_ < 0.0f ) {
            if ( tryContinue(cur.frontConnIndex, cur.frontConnToFront, -currentDistance_) ) transitioned = true;
            else currentDistance_ = 0.0f;  // 端で停止
        }
    } else {
        // クールダウン中は端でクランプ（乗り換え直後のワープ防止）
        const float len = cur.GetLength();
        if ( currentDistance_ < 0.0f )      currentDistance_ = 0.0f;
        else if ( currentDistance_ > len )  currentDistance_ = len;
    }

    // =================================================================
    // 4. 乗り換え：別タイプの近接レールへ（押した方向に伸びているもの）
    //    横レール上の W/S → 近くの縦レールへ／縦レール上の A/D → 近くの横レールへ
    // =================================================================
    if ( switchInput != 0 && switchCooldown_ <= 0.0f && !transitioned ) {
        const float kReach  = 4.0f;  // 乗り換え先の最寄り点までの最大3D距離
        const float kMinOff = 0.3f;  // 押した方向にこれ以上伸びているレールであること
        const bool  wantHorizontalTarget = !curHorizontal; // 縦に乗ってたら横へ／横なら縦へ

        // 乗り換えの判定軸：横レール上はZ(奥/手前)、縦レール上はX(右/左)
        const float myAxis = curHorizontal ? position_.z : position_.x;

        int   bestRail  = -1;
        float bestDist  = 0.0f;
        float bestScore = 1e30f;

        for ( int j = 0; j < ( int ) allRails.size(); ++j ) {
            if ( j == currentRailIndex_ ) continue;
            const SplineRail& rj = allRails[j];
            if ( rj.nodes.size() < 2 ) continue;
            bool jHorizontal = ( rj.type == SplineRail::RailType::Horizontal );
            if ( jHorizontal != wantHorizontalTarget ) continue; // 反対タイプのみ

            // 相手レール上で今のプレイヤーに最も近い点
            float cd = rj.GetClosestDistance(position_);
            Vector3 cp = rj.GetPositionByDistance(cd);
            float dx = cp.x - position_.x, dy = cp.y - position_.y, dz = cp.z - position_.z;
            float dist3d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if ( dist3d > kReach ) continue; // 遠いレールへは飛ばない

            // 押した方向に rj が伸びているか（ノードの判定軸 min/max で見る）
            float axisMin = 1e30f, axisMax = -1e30f;
            for ( const auto& n : rj.nodes ) {
                float v = curHorizontal ? n.z : n.x;
                axisMin = std::min(axisMin, v);
                axisMax = std::max(axisMax, v);
            }
            if ( switchInput > 0 ) { if ( axisMax < myAxis + kMinOff ) continue; } // 奥/右へ伸びてる？
            else                   { if ( axisMin > myAxis - kMinOff ) continue; } // 手前/左へ伸びてる？

            if ( dist3d < bestScore ) { bestScore = dist3d; bestRail = j; bestDist = cd; }
        }

        if ( bestRail >= 0 ) {
            currentRailIndex_ = bestRail;
            // 端ちょうどに着地しないよう少し内側へ
            float bl = allRails[bestRail].GetLength();
            float margin = std::min(0.15f, bl * 0.25f);
            currentDistance_ = std::clamp(bestDist, margin, bl - margin);
            switchCooldown_  = 0.25f;
            transitioned     = true;
        }
    }

    // =================================================================
    // 5. 最終的な距離をクランプ
    // =================================================================
    const SplineRail& rail = allRails[currentRailIndex_];
    currentDistance_ = std::clamp(currentDistance_, 0.0f, rail.GetLength());

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

    // 向き：最終レールのタイプに対応する移動キーで、進行方向(ワールド)へ向ける。
    // 乗り換え直後にレールタイプが変わっても正しい向きになるよう、ここで取り直す。
    const bool finalHorizontal = ( rail.type == SplineRail::RailType::Horizontal );
    float facingMove = 0.0f;
    if ( finalHorizontal ) {
        if ( input->Pushkey(DIK_D) ) facingMove += 1.0f;
        if ( input->Pushkey(DIK_A) ) facingMove -= 1.0f;
    } else {
        if ( input->Pushkey(DIK_W) ) facingMove += 1.0f;
        if ( input->Pushkey(DIK_S) ) facingMove -= 1.0f;
    }
    Vector3 tangent = rail.GetTangentByDistance(currentDistance_);
    if ( Length(tangent) > 0.001f && facingMove != 0.0f ) {
        float axisComp = finalHorizontal ? tangent.x : tangent.z;
        float along    = facingMove * ( ( axisComp >= 0.0f ) ? 1.0f : -1.0f ); // ワールド進行の符号
        Vector3 vel = { tangent.x * along, tangent.y * along, tangent.z * along };
        rotation_.y = std::atan2(vel.x, vel.z);
        rotation_.x = 0.0f;
        rotation_.z = 0.0f;
    }
}

// 横レール=世界X / 縦レール=世界Z に沿って進む向きの符号を返す
static float MoveAlongSign(const SplineRail& rail, float currentDistance, float moveInput){
    if ( moveInput == 0.0f ) return 0.0f;
    const bool horizontal = ( rail.type == SplineRail::RailType::Horizontal );
    Vector3 tan = rail.GetTangentByDistance(currentDistance);
    float axisComp = horizontal ? tan.x : tan.z;
    float dir = ( axisComp >= 0.0f ) ? 1.0f : -1.0f; // +s でその軸が増える向き
    return moveInput * dir;
}
