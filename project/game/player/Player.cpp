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

// 水平(x,z)を単位ベクトル化（長さ0なら0ベクトル）。空中の進行方向の記録に使う
static Vector3 HorizDir(float x, float z){
    float len = std::sqrt(x * x + z * z);
    if ( len < 1e-4f ) return { 0.0f, 0.0f, 0.0f };
    return { x / len, 0.0f, z / len };
}

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
    flutterCdTimer_ = 0.0f;

    inAir_       = false;
    airVelocity_ = { 0.0f, 0.0f, 0.0f };
    airLandCooldown_ = 0.0f;
    airFromRail_ = -1;
    airDir_      = { 0.0f, 0.0f, 0.0f };
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

    // 指定距離が「穴」か？（ノード穴 / 支えのない NoGround レール）。
    //   穴の上はレールに地面が無い → 接地できず落下する。
    //   ※プレイヤーはレールから離れず、レール上を距離で進みつつ高さ(heightOffset_)だけ落ちる。
    //     これで「進行方向にレールへ沿って動く」を保ったまま穴で落下できる。
    auto overHoleAt = [&](float s) -> bool{
        // 乗り換え後も正しく判定するため、常に「今の」レールを見る
        const SplineRail& rr = allRails[currentRailIndex_];
        if ( rr.IsHoleAtDistance(s) ) return true;
        if ( rr.groundType == SplineRail::GroundType::NoGround ) {
            // 足元に別の地面レールがあれば支えられる（交差点など）
            Vector3 fp = rr.GetPositionByDistance(s);
            for ( int i = 0; i < ( int ) allRails.size(); ++i ) {
                if ( i == currentRailIndex_ ) continue;
                const SplineRail& r = allRails[i];
                if ( r.groundType == SplineRail::GroundType::NoGround || r.nodes.size() < 2 ) continue;
                float cd = r.GetClosestDistance(fp);
                Vector3 cp = r.GetPositionByDistance(cd);
                float dx = cp.x - fp.x, dz = cp.z - fp.z;
                if ( std::sqrt(dx * dx + dz * dz) < 1.0f && std::abs(cp.y - fp.y) < 0.8f ) return false;
            }
            return true; // NoGround で支えなし
        }
        return false;
        };

    // 接地中に穴の上へ来たら、その場から落下開始（レールには乗ったまま高さだけ落ちる）。
    if ( isGrounded_ && overHoleAt(currentDistance_) ) {
        isGrounded_   = false;
        jumpVelocity_ = 0.0f;                                  // 静かに落ち始める
        if ( flutterCdTimer_ <= 0.0f ) flutterCdTimer_ = 0.6f; // 落下中もふんばりで粘れる
    }

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
            // connIndex で明示的に繋がっている端点は型を問わず地続きにする。
            // 型が違うレールへ渡った場合は switchCooldown_ で即乗り換えを防ぐ。
            if ( allRails[connIdx].type != cur.type ) {
                switchCooldown_ = 0.25f;
            }
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
            airDir_ = HorizDir(tan.x * dsSign_, tan.z * dsSign_);
            heightOffset_ = 0.0f;
            jumpVelocity_ = 0.0f;
            isGrounded_   = false;
            currentDistance_ = edgeS;
            airLandCooldown_ = 0.25f;            // この間は…
            airFromRail_     = currentRailIndex_; // …元のレールへの再着地だけ抑止（端で跳ね返らない）
            if ( flutterCdTimer_ <= 0.0f ) flutterCdTimer_ = 0.6f; // 落下中もふんばり可能
            };

        // 端点溶接が無くても、端のすぐ近くに別レールの「本体（途中含む）」があれば
        // そこへ「合流」する。地上なら一旦停止（ジャンクション）して進む向きを選ぶ。
        // ジャンプ中は高さを保ったままレールを乗り移り、飛び越えを防ぐ。
        auto tryJoinNearbyBody = [&](float edgeS) -> bool{
            const float kJoinReach = 1.2f;
            Vector3 edgePos = cur.GetPositionByDistance(edgeS);

            int     bestRail = -1;
            float   bestDist = kJoinReach;
            float   bestCd   = 0.0f;
            Vector3 bestPos  = {};
            for ( int j = 0; j < ( int ) allRails.size(); ++j ) {
                if ( j == currentRailIndex_ ) continue;
                const SplineRail& rj = allRails[j];
                if ( rj.nodes.size() < 2 ) continue;
                if ( rj.groundType == SplineRail::GroundType::NoGround ) continue; // 地面なしには合流しない
                float cd = rj.GetClosestDistance(edgePos);
                Vector3 cp = rj.GetPositionByDistance(cd);
                float dx = cp.x - edgePos.x, dy = cp.y - edgePos.y, dz = cp.z - edgePos.z;
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if ( d < bestDist ) { bestDist = d; bestRail = j; bestCd = cd; bestPos = cp; }
            }
            if ( bestRail < 0 ) return false;

            // ジャンプ中：レール間の高低差を heightOffset_ に反映し、見た目の高さを維持
            if ( !isGrounded_ ) {
                heightOffset_ += ( edgePos.y - bestPos.y );
                if ( heightOffset_ < 0.0f ) {
                    heightOffset_   = 0.0f;
                    jumpVelocity_   = 0.0f;
                    isGrounded_     = true;
                    flutterCdTimer_ = 0.0f;
                }
            }

            currentRailIndex_ = bestRail;
            currentDistance_  = bestCd;
            dsSign_         = 0.0f;
            atJunction_     = isGrounded_; // 地上のみジャンクション停止。空中はそのまま着地を待つ
            switchCooldown_ = 0.15f;
            return true;
            };

        // 接続先が NoGround（穴）か？ → そこへは乗らず、空中へ飛び出して飛び越える
        auto connIsNoGround = [&](int connIdx) -> bool{
            return connIdx >= 0 && connIdx < ( int ) allRails.size()
                && allRails[connIdx].groundType == SplineRail::GroundType::NoGround;
            };

        if ( currentDistance_ > len ) {
            if ( connIsNoGround(cur.backConnIndex) && dsSign_ != 0.0f ) {
                detachToAir(len);   // 穴へ飛び出す（ジャンプ中＝飛び越え／地上＝落下）
                return;
            } else if ( tryContinue(cur.backConnIndex, cur.backConnToFront, currentDistance_ - len) ) {
                transitioned = true;
            } else if ( tryJoinNearbyBody(len) ) {
                transitioned = true;
            } else if ( cur.groundType == SplineRail::GroundType::Gap && kFallOffEdges && dsSign_ != 0.0f ) {
                // Gap レール：端を越えたら空中へ飛び出す
                // ただし接続済み端点でジャンプ中はクランプ（飛び越え防止）
                if ( !isGrounded_ && cur.backConnIndex >= 0 ) {
                    currentDistance_ = len;
                } else {
                    detachToAir(len);
                    return;
                }
            } else if ( !isGrounded_ && dsSign_ != 0.0f ) {
                // Safe レールの未接続な端でも、ジャンプ中なら飛び出して次レールへ飛び移れる
                // （連結していなくても空中状態の着地走査で次のレールに乗れる）
                detachToAir(len);
                return;
            } else {
                currentDistance_ = len; // 地上 or 停止中：端でクランプ（歩いて落ちない）
            }
        } else if ( currentDistance_ < 0.0f ) {
            if ( connIsNoGround(cur.frontConnIndex) && dsSign_ != 0.0f ) {
                detachToAir(0.0f);
                return;
            } else if ( tryContinue(cur.frontConnIndex, cur.frontConnToFront, -currentDistance_) ) {
                transitioned = true;
            } else if ( tryJoinNearbyBody(0.0f) ) {
                transitioned = true;
            } else if ( cur.groundType == SplineRail::GroundType::Gap && kFallOffEdges && dsSign_ != 0.0f ) {
                if ( !isGrounded_ && cur.frontConnIndex >= 0 ) {
                    currentDistance_ = 0.0f;
                } else {
                    detachToAir(0.0f);
                    return;
                }
            } else if ( !isGrounded_ && dsSign_ != 0.0f ) {
                detachToAir(0.0f);
                return;
            } else {
                currentDistance_ = 0.0f;
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

        // 空中でも乗り換えできるよう、判定にはレール表面の足元位置(高さオフセット無し)を使う。
        //   position_ は heightOffset_ を含むため、ジャンプ/落下中は相手レールとのY差で
        //   距離(kReach)に引っかかり乗り換えできなかった。footPos なら地上と同条件になる。
        Vector3 footPos = cur.GetPositionByDistance(currentDistance_);

        // 乗り換えの判定軸：横レール上はZ(奥/手前)、縦レール上はX(右/左)
        const float myAxis = curHorizontal ? footPos.z : footPos.x;

        int   bestRail  = -1;
        float bestDist  = 0.0f;
        float bestScore = 1e30f;

        for ( int j = 0; j < ( int ) allRails.size(); ++j ) {
            if ( j == currentRailIndex_ ) continue;
            const SplineRail& rj = allRails[j];
            if ( rj.nodes.size() < 2 ) continue;
            bool jHorizontal = ( rj.type == SplineRail::RailType::Horizontal );
            if ( jHorizontal != wantHorizontalTarget ) continue; // 反対タイプのみ

            // 相手レール上で今のプレイヤー(足元)に最も近い点
            float cd = rj.GetClosestDistance(footPos);
            Vector3 cp = rj.GetPositionByDistance(cd);
            float dx = cp.x - footPos.x, dy = cp.y - footPos.y, dz = cp.z - footPos.z;
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
            float oldFootY = footPos.y; // 乗り換え前のレール面の高さ
            currentRailIndex_ = bestRail;
            // 端ちょうどに着地しないよう少し内側へ
            float bl = allRails[bestRail].GetLength();
            float margin = std::min(0.15f, bl * 0.25f);
            currentDistance_ = std::clamp(bestDist, margin, bl - margin);
            // 空中で乗り換えた時は、見た目のワールドYが飛ばないよう heightOffset_ を補正
            //   （新レール面 + heightOffset_ ＝ 旧の見た目の高さ を保つ）。地上ならそのまま。
            if ( !isGrounded_ ) {
                float newFootY = allRails[bestRail].GetPositionByDistance(currentDistance_).y;
                heightOffset_ += oldFootY - newFootY;
            }
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
    // 6. ジャンプ＋ふんばり（なめらか版）
    //    ・SPACEでジャンプ。空中でSPACEを長押しすると、重力がぐっと弱まって
    //      なめらかにふわっと滞空＋ほんの少し上昇する（羽ばたかず連続的）。
    //    ・滞空できる時間に上限があり、使い切る／離すと通常落下へ戻る。
    // =================================================================
    const float kFloatTime   = 0.6f;  // ふんばりで滞空できる最大時間（秒）
    const float kFloatTarget = 1.2f;  // 滞空中になめらかに近づく上向き速度（ゆっくり上昇）
    const float kFloatEase   = 6.0f;  // 目標へ近づく速さ（小さいほどなめらか）

    if ( isGrounded_ && input->Triggerkey(DIK_SPACE) ) {
        jumpVelocity_   = jumpPower_;
        isGrounded_     = false;
        flutterCdTimer_ = kFloatTime; // 滞空budgetを補充
    }

    // 空中でSPACE長押し中は「弱い重力＋ゆるい上昇」へなめらかに移行（ふわっと浮く）。それ以外は通常重力。
    if ( !isGrounded_ && input->Pushkey(DIK_SPACE) && flutterCdTimer_ > 0.0f && jumpVelocity_ < kFloatTarget ) {
        jumpVelocity_  += ( kFloatTarget - jumpVelocity_ ) * std::min(kFloatEase * dt, 1.0f);
        flutterCdTimer_ -= dt;
    } else {
        jumpVelocity_ -= gravity_ * dt;
    }

    heightOffset_ += jumpVelocity_ * dt;
    // 着地：高さが0以下に降りてきた時。ただし
    //   ・穴の上では着地しない（地面が無いのでそのまま落下を続ける＝穴に落ちる）
    //   ・深く落ちすぎ(< -kLandBand)は「穴に落ちた」とみなし着地させない（横移動でワープ復帰しない）
    const float kLandBand = 0.6f; // この範囲内で降りてきたら地面に着地
    if ( heightOffset_ <= 0.0f ) {
        bool overHole = overHoleAt(currentDistance_);
        if ( !overHole && heightOffset_ >= -kLandBand ) {
            // 地面あり＆降りてきた → 着地
            heightOffset_   = 0.0f;
            jumpVelocity_   = 0.0f;
            isGrounded_     = true;
            flutterCdTimer_ = 0.0f;
        }
        // 穴の上 or 深く落下中：着地せず heightOffset_ は負へ進む（レール下に落ちていく）
    }

    // =================================================================
    // 7. 座標と向きを確定
    // =================================================================
    Vector3 basePos = rail.GetPositionByDistance(currentDistance_);
    basePos.y += heightOffset_;
    position_ = basePos;

    // 穴に落ちて規定の高さより下まで落ちたらスタートへリスポーン
    if ( position_.y < kKillY ) { Initialize(); return; }

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

    // ---- 空中の軌道修正（前後は主操作・左右は弱い復帰ナッジ）----
    //   ワールド入力(D=+X / A=-X / W=+Z / S=-Z)を「進行方向(前後)」と「横」に分解。
    //   前後はしっかり効き、横は弱め＋入力を離すと真っ直ぐに戻る（自由飛行にはならない）。
    //   真下落下(airDir_=0)の時だけは弱い全方向操作で復帰を狙える。
    {
        float ax = 0.0f, az = 0.0f;
        if ( input->Pushkey(DIK_D) ) ax += 1.0f;
        if ( input->Pushkey(DIK_A) ) ax -= 1.0f;
        if ( input->Pushkey(DIK_W) ) az += 1.0f;
        if ( input->Pushkey(DIK_S) ) az -= 1.0f;

        const float kAccelFwd = 12.0f;            // 前後（進行方向）の加速：主操作
        const float kAccelLat = 5.0f;             // 左右（横）の加速：弱い復帰ナッジ
        const float kMaxFwd   = moveSpeed_ * 0.9f; // 前後の最高速度
        const float kMaxLat   = moveSpeed_ * 0.5f; // 横の最高速度（小さめ）
        const float kLatDrag  = 4.0f;             // 横入力が無い時に真っ直ぐへ戻る減衰

        if ( Length(airDir_) > 1e-4f ) {
            // 進行方向に垂直な単位ベクトル（横方向）
            Vector3 perp = { airDir_.z, 0.0f, -airDir_.x };
            float along   = ax * airDir_.x + az * airDir_.z; // 前後入力
            float lateral = ax * perp.x   + az * perp.z;     // 横入力

            // 加速（前後は強め／横は弱め）
            airVelocity_.x += ( airDir_.x * along * kAccelFwd + perp.x * lateral * kAccelLat ) * dt;
            airVelocity_.z += ( airDir_.z * along * kAccelFwd + perp.z * lateral * kAccelLat ) * dt;

            // 速度を前後成分・横成分に分解してそれぞれクランプ
            float sAlong = airVelocity_.x * airDir_.x + airVelocity_.z * airDir_.z;
            float sLat   = airVelocity_.x * perp.x   + airVelocity_.z * perp.z;
            sAlong = std::clamp(sAlong, -kMaxFwd, kMaxFwd);
            sLat   = std::clamp(sLat,   -kMaxLat, kMaxLat);
            if ( lateral == 0.0f ) sLat -= sLat * std::min(kLatDrag * dt, 1.0f); // 横は離すと戻る
            airVelocity_.x = airDir_.x * sAlong + perp.x * sLat;
            airVelocity_.z = airDir_.z * sAlong + perp.z * sLat;
        } else {
            // 真下落下：進行方向が無いので、弱い全方向操作で復帰だけ可能
            airVelocity_.x += ax * kAccelLat * dt;
            airVelocity_.z += az * kAccelLat * dt;
            if ( ax == 0.0f ) airVelocity_.x -= airVelocity_.x * std::min(kLatDrag * dt, 1.0f);
            if ( az == 0.0f ) airVelocity_.z -= airVelocity_.z * std::min(kLatDrag * dt, 1.0f);
            float hs = std::sqrt(airVelocity_.x * airVelocity_.x + airVelocity_.z * airVelocity_.z);
            if ( hs > kMaxLat && hs > 1e-4f ) { float k = kMaxLat / hs; airVelocity_.x *= k; airVelocity_.z *= k; }
        }
    }

    // ふんばり（flutter）：空中でも SPACE 長押しで滞空できる
    const float kFloatTarget = 1.2f;
    const float kFloatEase   = 6.0f;
    if ( input->Pushkey(DIK_SPACE) && flutterCdTimer_ > 0.0f && airVelocity_.y < kFloatTarget ) {
        airVelocity_.y += ( kFloatTarget - airVelocity_.y ) * std::min(kFloatEase * dt, 1.0f);
        flutterCdTimer_ -= dt;
    } else {
        airVelocity_.y -= gravity_ * dt;
    }

    // 移動前のYを覚えておく：着地はレール面を上→下に通過した瞬間に判定
    const float prevY = position_.y;
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
            if ( r.groundType == SplineRail::GroundType::NoGround ) continue; // 地面なしレールには着地しない

            float cd = r.GetClosestDistance(position_);
            Vector3 cp = r.GetPositionByDistance(cd);

            // 穴区間には着地しない（飛び越え中に穴の上で着地→即落下のループを防ぐ）
            if ( r.IsHoleAtDistance(cd) ) continue;

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
            flutterCdTimer_ = 0.0f;
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

void Player::Bounce() {
    // 敵を踏みつけた際に上方向へ跳ね返る処理
    if ( inAir_ ) {
        // 空中自由落下状態の場合：空中用のY速度を直接上向きに設定
        airVelocity_.y = jumpPower_;
        flutterCdTimer_ = 0.6f;
    } else {
        // レール移動中の場合：ジャンプ速度を上向きにし、滞空状態を開始
        jumpVelocity_   = jumpPower_;
        isGrounded_     = false;
        flutterCdTimer_ = 0.6f; // ふんばり滞空用のタイマーも補充
    }
}
