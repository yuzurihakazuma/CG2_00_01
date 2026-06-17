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

// 端に接続が無い時に「飛び出して落ちる」か。
//   true  … 端を越えると空中へ（穴に落ちる／ジャンプで飛び越えられる）
//   false … 端で停止する（崖なしの安全仕様）
static constexpr bool kFallOffEdges = true;

// ここより下に落ちたらスタートへリスポーン
static constexpr float kKillY = -10.0f;

void Player::Initialize(){
    position_ = { 0.0f, 0.0f, 0.0f };
    rotation_ = { 0.0f, 0.0f, 0.0f };
    scale_    = { 1.0f, 1.0f, 1.0f };

    currentDistance_  = 0.0f;
    currentRailIndex_ = 0;
    moveSign_         = 1;
    dsSign_           = 0.0f;
    prevMoveInput_    = 0.0f;
    atJunction_       = false;
    switchCooldown_   = 0.0f;

    heightOffset_ = 0.0f;
    jumpVelocity_ = 0.0f;
    isGrounded_   = true;

    inAir_       = false;
    airVelocity_ = { 0.0f, 0.0f, 0.0f };
    airLandCooldown_ = 0.0f;
    airFromRail_ = -1;
}

void Player::Update(const std::vector<SplineRail>& allRails){
    if ( allRails.empty() ) return;
    if ( currentRailIndex_ < 0 || currentRailIndex_ >= ( int ) allRails.size() ) return;

    const float dt = Time::GetInstance()->GetDeltaTime(); // フレームレート非依存
    Input* input = Input::GetInstance();

    // =================================================================
    // 0. 空中状態（レール外）：自由落下しながら着地できるレールを探す
    // =================================================================
    if ( inAir_ ) {
        UpdateAir(allRails, dt);
        return;
    }

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
    // 2. 今のレールを進める（進行方向の記憶つき）
    //    キーを「押した瞬間」だけワールド方向(横=X/縦=Z)から進行符号を決め、
    //    押しっぱなしの間は符号を保持する。
    //    → 円状レールの頂点・急カーブで接線の軸成分が反転しても止まらない/逆走しない
    // =================================================================
    if ( moveInput != 0.0f ) {
        bool freshPress = ( prevMoveInput_ == 0.0f ) || ( moveInput * prevMoveInput_ < 0.0f );
        if ( atJunction_ && !freshPress ) {
            // 合流直後のジャンクションで一旦停止中：キーを押し直す（離して再入力 or 逆キー）
            // まで動かない。dsSign_ は 0 のまま＝その場で待機し、進む向きを選び直せる。
        } else {
            atJunction_ = false;
            if ( freshPress || dsSign_ == 0.0f ) {
                Vector3 tan = cur.GetTangentByDistance(currentDistance_);
                float axisComp = curHorizontal ? tan.x : tan.z;
                if ( std::abs(axisComp) > 0.05f ) {
                    // 接線の軸成分の向きに合わせる（D=世界+X / W=世界+Z）
                    dsSign_ = moveInput * ( ( axisComp >= 0.0f ) ? 1.0f : -1.0f );
                } else {
                    // 接線がほぼ直交（円の頂点など）→ とりあえず押した向きへ
                    dsSign_ = moveInput;
                }
            }
            currentDistance_ += dsSign_ * moveSpeed_ * dt;
        }
    } else {
        dsSign_ = 0.0f; // 離したら次に押した時に向きを決め直す
        atJunction_ = false;
    }
    prevMoveInput_ = moveInput;

    // =================================================================
    // 3. 終端：同じタイプの連結レールへは地続きで持ち越し。
    //    ループ（円状）は距離をラップして回り続ける。
    //    接続なしの端は「空中へ飛び出す」（穴に落ちる／ジャンプで飛び越える）。
    // =================================================================
    bool transitioned = false;
    if ( cur.isLoop ) {
        // ループ：端が無い。距離を周回でラップ → A/D(W/S) でくるくる回れる
        const float len = cur.GetLength();
        if ( len > 0.0f ) {
            while ( currentDistance_ > len )  currentDistance_ -= len;
            while ( currentDistance_ < 0.0f ) currentDistance_ += len;
        }
    } else if ( switchCooldown_ <= 0.0f ) {
        const float len = cur.GetLength();

        auto tryContinue = [&](int connIdx, bool enterFront, float over) -> bool{
            if ( connIdx < 0 || connIdx >= ( int ) allRails.size() ) return false;
            if ( allRails[connIdx].type != cur.type ) return false; // 同じタイプだけ地続き
            float newLen = allRails[connIdx].GetLength();
            currentRailIndex_ = connIdx;
            currentDistance_  = enterFront ? over : ( newLen - over );
            currentDistance_  = std::clamp(currentDistance_, 0.0f, newLen);
            // 持ち越し後も同じ物理方向へ進み続けるよう、進行符号を引き継ぐ
            dsSign_ = enterFront ? 1.0f : -1.0f;
            return true;
            };

        // 接続の無い端を越えた → 勢いのまま空中へ飛び出す
        auto detachToAir = [&](float edgeS){
            Vector3 edgePos = cur.GetPositionByDistance(edgeS);
            Vector3 tan = cur.GetTangentByDistance(edgeS);
            inAir_ = true;
            position_ = { edgePos.x, edgePos.y + heightOffset_, edgePos.z };
            airVelocity_ = { tan.x * dsSign_ * moveSpeed_, jumpVelocity_, tan.z * dsSign_ * moveSpeed_ };
            heightOffset_ = 0.0f;
            jumpVelocity_ = 0.0f;
            isGrounded_   = false;
            currentDistance_ = edgeS;
            airLandCooldown_ = 0.25f;            // この間は…
            airFromRail_     = currentRailIndex_; // …元のレールへの再着地だけ抑止（端で跳ね返らない）
            };

        // 端点溶接が無くても、端のすぐ近くに別レールの「本体（途中含む）」があれば
        // そこへ「合流」する。合流地点では一旦止まり（ジャンクション）、進む向きは
        // プレイヤーが入力で決める。これで線同士が近ければ落下せず繋がる。
        //   ※地上(歩行)で端に着いた時だけ合流。ジャンプ中(空中)はそのまま飛び出す
        //     （アクションの落下・飛び越えを潰さないため）。
        auto tryJoinNearbyBody = [&](float edgeS) -> bool{
            const float kJoinReach = 1.2f; // 端がこの距離以内に他レール本体があれば連結（グリッド約1マス強）
            Vector3 edgePos = cur.GetPositionByDistance(edgeS);

            int   bestRail = -1;
            float bestDist = kJoinReach;
            float bestCd   = 0.0f;
            for ( int j = 0; j < ( int ) allRails.size(); ++j ) {
                if ( j == currentRailIndex_ ) continue;
                const SplineRail& rj = allRails[j];
                if ( rj.nodes.size() < 2 ) continue;
                float cd = rj.GetClosestDistance(edgePos);
                Vector3 cp = rj.GetPositionByDistance(cd);
                float dx = cp.x - edgePos.x, dy = cp.y - edgePos.y, dz = cp.z - edgePos.z;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if ( d < bestDist ) { bestDist = d; bestRail = j; bestCd = cd; }
            }
            if ( bestRail < 0 ) return false;

            // 合流：その地点で停止。押し直す/別方向キーで進む向きを決める。
            currentRailIndex_ = bestRail;
            currentDistance_  = bestCd;
            dsSign_         = 0.0f;
            atJunction_     = true;
            switchCooldown_ = 0.15f; // 合流直後の即再合流を防ぐ
            return true;
            };

        if ( currentDistance_ > len ) {
            if ( tryContinue(cur.backConnIndex, cur.backConnToFront, currentDistance_ - len) ) {
                transitioned = true;
            } else if ( isGrounded_ && tryJoinNearbyBody(len) ) {   // 地上：近い別レールへ合流（停止）
                transitioned = true;
            } else if ( kFallOffEdges && dsSign_ != 0.0f ) {
                detachToAir(len);
                return; // 以降はレール上の処理なのでスキップ
            } else {
                currentDistance_ = len;   // 端で停止
            }
        } else if ( currentDistance_ < 0.0f ) {
            if ( tryContinue(cur.frontConnIndex, cur.frontConnToFront, -currentDistance_) ) {
                transitioned = true;
            } else if ( isGrounded_ && tryJoinNearbyBody(0.0f) ) {  // 地上：近い別レールへ合流（停止）
                transitioned = true;
            } else if ( kFallOffEdges && dsSign_ != 0.0f ) {
                detachToAir(0.0f);
                return; // 以降はレール上の処理なのでスキップ
            } else {
                currentDistance_ = 0.0f;  // 端で停止
            }
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
        // 乗り換えは「ジャンクション（接続点）のすぐそば」でしか発動しない。
        // 範囲を狭くすることで、横レール上では実質 A/D だけ・縦レール上では W/S だけが
        // 効き、交差点に立った時だけ乗り換えキーが意味を持つ（誤爆しない）。
        const float kReach   = 0.9f;  // 乗り換え先の最寄り点までの最大3D距離（狭いほど誤爆しない）
        const float kLateral = 0.5f;  // 進行軸(横=X/縦=Z)の横ズレ上限。真上で交差してる相手だけ拾う
        const float kMinOff  = 0.3f;  // 押した方向にこれ以上伸びているレールであること
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

            // 真上で交差している相手だけ拾う：進行軸(横=X/縦=Z)の横ズレが小さいこと。
            // これで「すぐ横を平行に走る縦レール」を W/S で誤って拾わなくなる。
            float lateral = curHorizontal ? std::abs(dx) : std::abs(dz);
            if ( lateral > kLateral ) continue;

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
            dsSign_          = 0.0f; // 新しいレールでは次の入力で進行方向を決め直す
        }
    }

    // =================================================================
    // 5. 最終的な距離をクランプ（ループはラップ）
    // =================================================================
    const SplineRail& rail = allRails[currentRailIndex_];
    if ( rail.isLoop && rail.GetLength() > 0.0f ) {
        while ( currentDistance_ > rail.GetLength() ) currentDistance_ -= rail.GetLength();
        while ( currentDistance_ < 0.0f )             currentDistance_ += rail.GetLength();
    } else {
        currentDistance_ = std::clamp(currentDistance_, 0.0f, rail.GetLength());
    }

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

    // 向き：実際に進んでいる方向（接線 × 進行符号）へ向ける。
    // dsSign_ ベースなので円状レールの途中でも進行方向と一致してブレない。
    Vector3 tangent = rail.GetTangentByDistance(currentDistance_);
    if ( Length(tangent) > 0.001f && dsSign_ != 0.0f ) {
        Vector3 vel = { tangent.x * dsSign_, tangent.y * dsSign_, tangent.z * dsSign_ };
        rotation_.y = std::atan2(vel.x, vel.z);
        rotation_.x = 0.0f;
        rotation_.z = 0.0f;
    }
}

// =====================================================================
//  空中状態：レールから離れて自由落下。
//   ・下降中に下へレールがあれば着地して復帰
//   ・kKillY より下に落ちたらスタートへリスポーン
// =====================================================================
void Player::UpdateAir(const std::vector<SplineRail>& allRails, float dt){
    Input* input = Input::GetInstance();

    // ---- 空中機動（エアコントロール）----
    // レール間をジャンプ中でも軌道を調整できるようにする。
    //   A/D=世界±X / W/S=世界±Z（レール上の操作と同じワールド方向の意思）。
    //   入力で水平速度を増減し、最高速度でクランプ → 隣のレールへ寄せて着地できる。
    float ax = 0.0f, az = 0.0f;
    if ( input->Pushkey(DIK_D) ) ax += 1.0f;
    if ( input->Pushkey(DIK_A) ) ax -= 1.0f;
    if ( input->Pushkey(DIK_W) ) az += 1.0f;
    if ( input->Pushkey(DIK_S) ) az -= 1.0f;

    const float kAirAccel = 28.0f;            // 空中での加速 (m/s^2)。大きいほどキビキビ操作できる
    const float kAirMaxXZ = moveSpeed_ * 1.5f; // 空中の水平最高速度 (m/s)
    const float kAirDrag  = 2.0f;             // 入力の無い軸を緩く減衰させる係数 (1/s)

    airVelocity_.x += ax * kAirAccel * dt;
    airVelocity_.z += az * kAirAccel * dt;

    // 入力していない軸は軽く減衰させて止めやすく（オーバーシュート防止）
    if ( ax == 0.0f ) airVelocity_.x -= airVelocity_.x * std::min(kAirDrag * dt, 1.0f);
    if ( az == 0.0f ) airVelocity_.z -= airVelocity_.z * std::min(kAirDrag * dt, 1.0f);

    // 水平速度を最高速度でクランプ（斜めでも一定以上には加速しない）
    float hs = std::sqrt(airVelocity_.x * airVelocity_.x + airVelocity_.z * airVelocity_.z);
    if ( hs > kAirMaxXZ && hs > 1e-4f ) {
        float k = kAirMaxXZ / hs;
        airVelocity_.x *= k;
        airVelocity_.z *= k;
    }

    // 重力で自由落下（移動前のYを覚えておく：着地はレール面を上→下に通過した瞬間に判定）
    const float prevY = position_.y;
    airVelocity_.y -= gravity_ * dt;
    position_.x += airVelocity_.x * dt;
    position_.y += airVelocity_.y * dt;
    position_.z += airVelocity_.z * dt;

    // 進行方向を向く
    float horizSpeed = std::sqrt(airVelocity_.x * airVelocity_.x + airVelocity_.z * airVelocity_.z);
    if ( horizSpeed > 0.1f ) {
        rotation_.y = std::atan2(airVelocity_.x, airVelocity_.z);
        rotation_.x = 0.0f;
        rotation_.z = 0.0f;
    }

    // 飛び出した直後の "元レール" 再着地を抑止する猶予を減らす
    if ( airLandCooldown_ > 0.0f ) { airLandCooldown_ -= dt; }

    // 下降中だけ着地判定（上昇中にレールへ吸い付かないように）
    if ( airVelocity_.y <= 0.0f ) {
        const float kLandXZ = 0.8f; // 水平にこの距離以内なら「レールの真上」とみなす

        for ( int i = 0; i < ( int ) allRails.size(); ++i ) {
            // 飛び出した直後だけ、元のレールへの再着地を抑止（端で跳ね返らない）。
            // 別のレールへは猶予中でも着地できる（同じ高さの渡りを取りこぼさない）。
            if ( airLandCooldown_ > 0.0f && i == airFromRail_ ) continue;

            const SplineRail& r = allRails[i];
            if ( r.nodes.size() < 2 ) continue;

            float cd = r.GetClosestDistance(position_);
            Vector3 cp = r.GetPositionByDistance(cd);

            // 水平にレールの真上にいるか
            float dx = cp.x - position_.x, dz = cp.z - position_.z;
            if ( std::sqrt(dx * dx + dz * dz) > kLandXZ ) continue;

            // 縦：レール面に「降りてきて到達した」時だけ着地する（落下を最後まで見せる）。
            //   ・reached … レール面のすぐ近く(上0.1m〜下0.3m)に降りてきた＝自然な接地
            //   ・crossed … 高速落下で1フレームに面を上→下へ通過してもすり抜けずに拾う
            //   まだ上にいる間（above>0.1）は着地させない＝瞬間移動にならない。
            float above   = position_.y - cp.y;
            bool  reached = ( above <= 0.1f && above >= -0.3f );
            bool  crossed = ( prevY >= cp.y && position_.y <= cp.y );
            if ( !reached && !crossed ) continue;

            // 着地：到達したレール点に合わせる（縦のズレは降りてきた分だけ＝ごく僅か）
            inAir_ = false;
            currentRailIndex_ = i;
            currentDistance_  = cd;
            position_     = cp;
            heightOffset_ = 0.0f;
            jumpVelocity_ = 0.0f;
            isGrounded_   = true;
            airVelocity_  = { 0.0f, 0.0f, 0.0f };
            dsSign_       = 0.0f;  // 次の入力で進行方向を決め直す
            switchCooldown_ = 0.1f;
            return;
        }
    }

    // 落下死 → スタートへリスポーン
    if ( position_.y < kKillY ) {
        Initialize();
    }
}
