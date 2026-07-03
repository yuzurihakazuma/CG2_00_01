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

    // マップで指定されたスタート地点から開始（リスポーンもここへ戻る）
    currentDistance_  = spawnDist_;
    currentRailIndex_ = spawnRail_;
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
    posSmooth_   = { 0.0f, 0.0f, 0.0f };
}

// =====================================================================
//  メインのフレーム更新。処理を段階(0〜7)に分け、各段階は専用メソッドへ委譲する。
//  （状態は全部 Player のメンバなので、別クラスにせずメンバ関数化が素直）
// =====================================================================
void Player::Update(const std::vector<SplineRail>& allRails){
    if ( allRails.empty() ) return;
    if ( currentRailIndex_ < 0 || currentRailIndex_ >= ( int ) allRails.size() ) return;

    const float dt = Time::GetInstance()->GetDeltaTime(); // フレームレート非依存

    // 0. 空中状態（レール外）：自由落下しながら着地できるレールを探す
    if ( inAir_ ) { UpdateAir(allRails, dt); return; }

    if ( switchCooldown_ > 0.0f ) { switchCooldown_ -= dt; }

    // 乗り移り前の見た目位置（このフレームで座標が飛んだら差分を平滑化に回す）
    const Vector3 worldBefore = position_;

    const SplineRail& cur = allRails[currentRailIndex_];
    if ( cur.nodes.size() < 2 ) return;

    // 穴の上に来たら、レールを離れて自由落下（弾道）。下の別レールへ着地できる。
    if ( isGrounded_ && IsOverHole(allRails) ) {
        EnterAir(cur.GetPositionByDistance(currentDistance_), cur.GetTangentByDistance(currentDistance_), 0.0f, 0.15f);
        return;
    }

    const bool curHorizontal = ( cur.type == SplineRail::RailType::Horizontal );

    // 1. 入力 → 2. 移動
    float moveInput = 0.0f; int switchInput = 0;
    ReadRailInput(curHorizontal, moveInput, switchInput);
    if ( movementLocked_ ) { moveInput = 0.0f; switchInput = 0; } // 構え中はその場で待機
    MoveAlongRail(cur, curHorizontal, moveInput, dt);

    // 3. 終端処理（持ち越し/合流/落下/クランプ）。空中へ飛び出したら終了
    bool transitioned = false;
    if ( HandleRailEnds(allRails, cur, transitioned) ) return;

    // 4. 乗り換え（T字路の途中分岐を優先 → 無ければ別タイプの近接レールへ）
    if ( switchInput != 0 && switchCooldown_ <= 0.0f && !transitioned ) {
        if ( !TryBranch(allRails, cur, switchInput, transitioned) ) {
            TrySwitchRail(allRails, cur, curHorizontal, switchInput, transitioned);
        }
    }

    // 5. 距離クランプ（ループはラップ）
    const SplineRail& rail = allRails[currentRailIndex_];
    if ( rail.isLoop && rail.GetLength() > 0.0f ) {
        while ( currentDistance_ > rail.GetLength() ) currentDistance_ -= rail.GetLength();
        while ( currentDistance_ < 0.0f )             currentDistance_ += rail.GetLength();
    } else {
        currentDistance_ = std::clamp(currentDistance_, 0.0f, rail.GetLength());
    }

    // 6. ジャンプ＋着地。穴の上で降りてきて空中になったら終了
    if ( UpdateJumpAndLand(rail, allRails, dt) ) return;

    // 7. 座標・向きを確定（乗り移りの平滑化つき）
    FinalizePosition(rail, worldBefore, transitioned, dt);
}

// 足元の位置が、どれかのレールの「穴」区間の真上にあるか？
//   今乗っているレールだけでなく全レールの穴を見る。これで乗り換え地点で
//   隣のレールに乗ったまま穴の上を通っても取りこぼさず落下できる。
//   範囲は穴ノード付近だけ（狭め）なので、離れた所では落ちない。
bool Player::IsOverHole(const std::vector<SplineRail>& rails) const{
    Vector3 foot = rails[currentRailIndex_].GetPositionByDistance(currentDistance_);
    for ( int i = 0; i < ( int ) rails.size(); ++i ) {
        const SplineRail& r = rails[i];
        if ( r.nodes.size() < 2 || r.nodeHole.empty() ) continue;
        float cd = r.GetClosestDistance(foot);
        if ( !r.IsHoleAtDistance(cd) ) continue;
        Vector3 cp = r.GetPositionByDistance(cd);
        float dx = cp.x - foot.x, dz = cp.z - foot.z;
        // 高さ許容は±0.35m（広すぎると上下に重なった別の階の穴に誤爆して落ちる）
        if ( std::sqrt(dx * dx + dz * dz) < 0.5f && std::abs(cp.y - foot.y) < 0.35f ) return true;
    }
    return false;
}

// レールを離れて空中(弾道)状態へ移行する共通処理。
void Player::EnterAir(const Vector3& pos, const Vector3& tangent, float upVel, float landCooldown){
    inAir_       = true;
    position_    = pos;
    airVelocity_ = { tangent.x * dsSign_ * moveSpeed_, upVel, tangent.z * dsSign_ * moveSpeed_ };
    airDir_      = HorizDir(tangent.x * dsSign_, tangent.z * dsSign_);
    heightOffset_ = 0.0f;
    jumpVelocity_ = 0.0f;
    isGrounded_   = false;
    if ( flutterCdTimer_ <= 0.0f ) flutterCdTimer_ = 0.6f; // 落下中もふんばり可能
    airLandCooldown_ = landCooldown;
    airFromRail_     = currentRailIndex_; // この間は元レールへの即再着地を抑止
}

// 端から空中へ飛び出す（Gap レール用）。飛び出す端は heightOffset_ を保ったまま。
void Player::DetachToAir(const SplineRail& cur, float edgeS){
    Vector3 edgePos = cur.GetPositionByDistance(edgeS);
    Vector3 tan     = cur.GetTangentByDistance(edgeS);
    EnterAir({ edgePos.x, edgePos.y + heightOffset_, edgePos.z }, tan, jumpVelocity_, 0.25f);
    currentDistance_ = edgeS;
}

// カメラの向き(90°単位)に応じた「実キー → ワールド方向」の割り当て。
//   通常（カメラが後ろ・0°）は D=+X / A=-X / W=+Z / S=-Z。
//   カメラが正面へ回り込んだ(180°)ら D=-X / W=-Z … と割り当てごと回すので、
//   どの向きでも「押したキーの方向＝画面で進む方向」が一致する。
//   ※連続角で合成せず90°に量子化することで、既存の移動ロジック（dsSign等）を一切変えずに済む。
void Player::GetWorldKeys(int& plusX, int& minusX, int& plusZ, int& minusZ) const{
    // camYaw_ は「カメラが見ている方向」の yaw（camera->GetRotation().y）。
    //   画面の奥(W) = 注視方向 (sinθ, cosθ) / 画面の右(D) = (cosθ, -sinθ)。
    //   θ=+90°: W=+X, D=-Z / θ=-90°: W=-X, D=+Z（±90°を取り違えないこと！）
    const float kHalfPi = 1.57079632f;
    int qi = ( int ) std::lround(camYaw_ / kHalfPi);

    // ヒステリシス：90°の中心から大きく外れている（＝カメラが回転の途中）間は
    // 前回の割り当てを維持する。境界(45°)付近で毎フレーム切り替わるのを防ぐ。
    float diff = std::abs(camYaw_ - qi * kHalfPi);
    int q;
    if ( diff < 0.6f ) { // 中心から約34°以内なら確定
        q = ( ( qi % 4 ) + 4 ) % 4;
        lastKeyQuad_ = q;
    } else {
        q = lastKeyQuad_; // 回転途中は前回の向きのまま
    }

    switch ( q ) {
    case 1:  plusX = DIK_W; minusX = DIK_S; plusZ = DIK_A; minusZ = DIK_D; break; // 注視+90°（左側から見る）
    case 2:  plusX = DIK_A; minusX = DIK_D; plusZ = DIK_S; minusZ = DIK_W; break; // 正面(180°)＝完全反転
    case 3:  plusX = DIK_S; minusX = DIK_W; plusZ = DIK_D; minusZ = DIK_A; break; // 注視-90°（右側から見る）
    default: plusX = DIK_D; minusX = DIK_A; plusZ = DIK_W; minusZ = DIK_S; break; // 後ろから(0°)
    }
}

// 入力をレールタイプに応じて「移動入力」と「乗り換え入力」へ振り分ける。
//   横レール … ±Xキー=移動 / ±Zキー=縦レールへ乗り換え
//   縦レール … ±Zキー=移動 / ±Xキー=横レールへ乗り換え
//   （キー→ワールド方向の対応はカメラの向きで回る。GetWorldKeys 参照）
void Player::ReadRailInput(bool curHorizontal, float& moveInput, int& switchInput) const{
    Input* input = Input::GetInstance();
    moveInput = 0.0f; switchInput = 0;

    int kPX, kMX, kPZ, kMZ;
    GetWorldKeys(kPX, kMX, kPZ, kMZ);

    if ( curHorizontal ) {
        if ( input->Pushkey(( BYTE ) kPX) )    moveInput   += 1.0f; // ワールド+X へ
        if ( input->Pushkey(( BYTE ) kMX) )    moveInput   -= 1.0f; // ワールド-X へ
        if ( input->Triggerkey(( BYTE ) kPZ) ) switchInput += 1;    // 奥(+Z)の縦レールへ
        if ( input->Triggerkey(( BYTE ) kMZ) ) switchInput -= 1;    // 手前(-Z)の縦レールへ
    } else {
        if ( input->Pushkey(( BYTE ) kPZ) )    moveInput   += 1.0f; // ワールド+Z へ
        if ( input->Pushkey(( BYTE ) kMZ) )    moveInput   -= 1.0f; // ワールド-Z へ
        if ( input->Triggerkey(( BYTE ) kPX) ) switchInput += 1;    // 右(+X)の横レールへ
        if ( input->Triggerkey(( BYTE ) kMX) ) switchInput -= 1;    // 左(-X)の横レールへ
    }
}

// 今のレールを距離で進める（進行方向の記憶つき）。
//   キーを「押した瞬間」だけワールド方向(横=X/縦=Z)から進行符号を決め、押しっぱなしの
//   間は符号を保持する → 円状レールの頂点・急カーブで接線が反転しても止まらない/逆走しない。
void Player::MoveAlongRail(const SplineRail& cur, bool curHorizontal, float moveInput, float dt){
    if ( moveInput != 0.0f ) {
        bool freshPress = ( prevMoveInput_ == 0.0f ) || ( moveInput * prevMoveInput_ < 0.0f );
        if ( atJunction_ && !freshPress ) {
            // 合流直後のジャンクションで一旦停止中：押し直す(離して再入力 or 逆キー)まで動かない。
        } else {
            atJunction_ = false;
            if ( freshPress || dsSign_ == 0.0f ) {
                Vector3 tan = cur.GetTangentByDistance(currentDistance_);
                float axisComp = curHorizontal ? tan.x : tan.z;
                if ( std::abs(axisComp) > 0.05f ) {
                    dsSign_ = moveInput * ( ( axisComp >= 0.0f ) ? 1.0f : -1.0f ); // 接線の軸成分に合わせる
                } else {
                    dsSign_ = moveInput; // 接線がほぼ直交（円の頂点など）→ 押した向きへ
                }
            }
            // 片方向レール：許可されていない向きへは進めない（ジェットコースター区間など）
            if ( cur.oneWay == 1 && dsSign_ < 0.0f ) dsSign_ = 0.0f; // 正方向(front→back)のみ
            if ( cur.oneWay == 2 && dsSign_ > 0.0f ) dsSign_ = 0.0f; // 逆方向(back→front)のみ

            // レールごとの速度倍率（加速/減速レール）を掛ける
            currentDistance_ += dsSign_ * moveSpeed_ * cur.speedMul * dt;
        }
    } else {
        dsSign_ = 0.0f; // 離したら次に押した時に向きを決め直す
        atJunction_ = false;
    }
    prevMoveInput_ = moveInput;
}

// 連結している端へ地続きで持ち越す。
bool Player::TryContinueToConnected(const std::vector<SplineRail>& rails, const SplineRail& cur, int connIdx, bool enterFront, float over){
    if ( connIdx < 0 || connIdx >= ( int ) rails.size() ) return false;
    // 動くレールへ/からは静的連結しない（rest 位置基準なので animOffset 分ワープする）。
    // 動くレールは TryJoinNearbyBody の「今の位置」での動的ドッキングに任せる。
    if ( rails[connIdx].HasMotion() || cur.HasMotion() ) return false;
    // 型が違うレールへ渡った場合は switchCooldown_ で即乗り換えを防ぐ。
    if ( rails[connIdx].type != cur.type ) { switchCooldown_ = 0.25f; }
    float newLen = rails[connIdx].GetLength();
    currentRailIndex_ = connIdx;
    currentDistance_  = enterFront ? over : ( newLen - over );
    currentDistance_  = std::clamp(currentDistance_, 0.0f, newLen);
    dsSign_ = enterFront ? 1.0f : -1.0f; // 持ち越し後も同じ物理方向へ
    return true;
}

// 端のすぐ近くにある別レール本体へ合流（動的ドッキング）。
//   地上なら一旦停止（ジャンクション）。ジャンプ中は高さを保ったまま乗り移る。
bool Player::TryJoinNearbyBody(const std::vector<SplineRail>& rails, const SplineRail& cur, float edgeS){
    const float kJoinReach = 1.2f;
    Vector3 edgePos = cur.GetPositionByDistance(edgeS);

    // 進行方向（端から出て行く向き）。「前方」にあるレールへだけ合流する。
    //   これが無いと、1.2m以内を平行に走る隣のレールへ端に来ただけで勝手に飛び移る誤爆が起きる。
    Vector3 tan = cur.GetTangentByDistance(edgeS);
    Vector3 fwd = HorizDir(tan.x * dsSign_, tan.z * dsSign_);

    int     bestRail = -1;
    float   bestDist = kJoinReach;
    float   bestCd   = 0.0f;
    Vector3 bestPos  = {};
    for ( int j = 0; j < ( int ) rails.size(); ++j ) {
        if ( j == currentRailIndex_ ) continue;
        const SplineRail& rj = rails[j];
        if ( rj.nodes.size() < 2 ) continue;
        float cd = rj.GetClosestDistance(edgePos);
        Vector3 cp = rj.GetPositionByDistance(cd);
        float dx = cp.x - edgePos.x, dy = cp.y - edgePos.y, dz = cp.z - edgePos.z;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if ( d >= bestDist ) continue;

        // 前方チェック：合流先が真横〜後方なら弾く（端点の真上を通る動くレール等、ほぼ同位置は許可）
        if ( Length(fwd) > 1e-4f ) {
            Vector3 to = HorizDir(dx, dz);
            if ( Length(to) > 1e-4f && ( to.x * fwd.x + to.z * fwd.z ) < 0.1f ) continue;
        }

        // 乗り移りのスナップは posSmooth_ で滑らかに補間するので、動くレールも 1.2m で確実に乗れる。
        bestDist = d; bestRail = j; bestCd = cd; bestPos = cp;
    }
    if ( bestRail < 0 ) return false;

    // ジャンプ中：レール間の高低差を heightOffset_ に反映し、見た目の高さを維持
    if ( !isGrounded_ ) {
        heightOffset_ += ( edgePos.y - bestPos.y );
        if ( heightOffset_ < 0.0f ) {
            heightOffset_ = 0.0f; jumpVelocity_ = 0.0f; isGrounded_ = true; flutterCdTimer_ = 0.0f;
        }
    }

    currentRailIndex_ = bestRail;
    currentDistance_  = bestCd;
    dsSign_         = 0.0f;
    atJunction_     = isGrounded_; // 地上のみジャンクション停止。空中はそのまま着地を待つ
    switchCooldown_ = 0.15f;
    return true;
}

// レール終端の処理（ループ周回 / クールダウン中クランプ / 持ち越し・合流・落下・クランプ）。
//   空中へ飛び出したら true を返す（呼び出し側は return する）。
bool Player::HandleRailEnds(const std::vector<SplineRail>& rails, const SplineRail& cur, bool& transitioned){
    if ( cur.isLoop ) {
        const float len = cur.GetLength(); // ループ：端が無い。距離を周回でラップ
        if ( len > 0.0f ) {
            while ( currentDistance_ > len )  currentDistance_ -= len;
            while ( currentDistance_ < 0.0f ) currentDistance_ += len;
        }
        return false;
    }
    if ( switchCooldown_ > 0.0f ) { // クールダウン中は端でクランプ（乗り換え直後のワープ防止）
        const float len = cur.GetLength();
        if ( currentDistance_ < 0.0f )      currentDistance_ = 0.0f;
        else if ( currentDistance_ > len )  currentDistance_ = len;
        return false;
    }

    const float len = cur.GetLength();
    if ( currentDistance_ > len ) {
        if ( TryContinueToConnected(rails, cur, cur.backConnIndex, cur.backConnToFront, currentDistance_ - len) ) {
            transitioned = true;
        } else if ( TryJoinNearbyBody(rails, cur, len) ) {
            transitioned = true;
        } else if ( cur.groundType == SplineRail::GroundType::Gap && kFallOffEdges ) {
            DetachToAir(cur, len); // Gap レール：明示的に「落ちてよい端」（キーを離した瞬間でも飛び出せる）
            return true;
        } else {
            currentDistance_ = len; // 未接続の端 → クランプ（落下は穴/Gap だけ）
        }
    } else if ( currentDistance_ < 0.0f ) {
        if ( TryContinueToConnected(rails, cur, cur.frontConnIndex, cur.frontConnToFront, -currentDistance_) ) {
            transitioned = true;
        } else if ( TryJoinNearbyBody(rails, cur, 0.0f) ) {
            transitioned = true;
        } else if ( cur.groundType == SplineRail::GroundType::Gap && kFallOffEdges ) {
            DetachToAir(cur, 0.0f);
            return true;
        } else {
            currentDistance_ = 0.0f;
        }
    }
    return false;
}

// 別タイプの近接レールへ乗り換える（押した方向に伸びているもの。交差点近くだけ発動）。
void Player::TrySwitchRail(const std::vector<SplineRail>& rails, const SplineRail& cur, bool curHorizontal, int switchInput, bool& transitioned){
    const float kReach   = 0.9f;  // 乗り換え先の最寄り点までの最大3D距離（狭いほど誤爆しない）
    const float kLateral = 0.5f;  // 進行軸(横=X/縦=Z)の横ズレ上限。真上で交差してる相手だけ拾う
    const float kMinOff  = 0.3f;  // 押した方向にこれ以上伸びているレールであること
    const bool  wantHorizontalTarget = !curHorizontal; // 縦に乗ってたら横へ／横なら縦へ

    // 空中でも乗り換えできるよう、判定にはレール表面の足元位置(高さオフセット無し)を使う。
    Vector3 footPos = cur.GetPositionByDistance(currentDistance_);
    const float myAxis = curHorizontal ? footPos.z : footPos.x; // 横=Z(奥/手前) / 縦=X(右/左)

    int   bestRail  = -1;
    float bestDist  = 0.0f;
    float bestScore = 1e30f;
    for ( int j = 0; j < ( int ) rails.size(); ++j ) {
        if ( j == currentRailIndex_ ) continue;
        const SplineRail& rj = rails[j];
        if ( rj.nodes.size() < 2 ) continue;
        if ( !rj.visible ) continue; // 見えない連結レールへは乗り換えできない（見えない道を歩く混乱防止）
        if ( ( rj.type == SplineRail::RailType::Horizontal ) != wantHorizontalTarget ) continue; // 反対タイプのみ

        float cd = rj.GetClosestDistance(footPos);
        Vector3 cp = rj.GetPositionByDistance(cd);
        float dx = cp.x - footPos.x, dy = cp.y - footPos.y, dz = cp.z - footPos.z;
        float dist3d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if ( dist3d > kReach ) continue; // 遠いレールへは飛ばない

        float lateral = curHorizontal ? std::abs(dx) : std::abs(dz); // 真上で交差してる相手だけ
        if ( lateral > kLateral ) continue;

        float axisMin = 1e30f, axisMax = -1e30f; // 押した方向に伸びているか
        for ( const auto& n : rj.nodes ) {
            float v = curHorizontal ? n.z : n.x;
            axisMin = std::min(axisMin, v);
            axisMax = std::max(axisMax, v);
        }
        if ( switchInput > 0 ) { if ( axisMax < myAxis + kMinOff ) continue; }
        else                   { if ( axisMin > myAxis - kMinOff ) continue; }

        if ( dist3d < bestScore ) { bestScore = dist3d; bestRail = j; bestDist = cd; }
    }

    if ( bestRail >= 0 ) {
        float oldFootY = footPos.y;
        currentRailIndex_ = bestRail;
        float bl = rails[bestRail].GetLength();
        float margin = std::min(0.15f, bl * 0.25f); // 端ちょうどに着地しないよう少し内側へ
        currentDistance_ = std::clamp(bestDist, margin, bl - margin);
        // 空中で乗り換えた時は、見た目のワールドYが飛ばないよう heightOffset_ を補正
        if ( !isGrounded_ ) {
            float newFootY = rails[bestRail].GetPositionByDistance(currentDistance_).y;
            heightOffset_ += oldFootY - newFootY;
        }
        switchCooldown_  = 0.25f;
        transitioned     = true;
        dsSign_          = 0.0f; // 新しいレールでは次の入力で進行方向を決め直す
    }
}

// T字路の途中分岐（branchPoints）で分岐する。
//   接続情報のロード時に「レールjの端がレールiの途中に接している」箇所を検出済み。
//   分岐点の近く(0.9m)で、分岐の向きに合う乗り換えキーを押すと分岐先レールへ移る。
//   TrySwitchRail と違い「同じタイプ同士のT字路」でも分岐できる。
bool Player::TryBranch(const std::vector<SplineRail>& rails, const SplineRail& cur, int switchInput, bool& transitioned){
    const float kNear = 0.9f; // 分岐点に反応する距離
    for ( const auto& bp : cur.branchPoints ) {
        if ( std::abs(currentDistance_ - bp.distance) > kNear ) continue;
        if ( ( switchInput > 0 ) != ( bp.zSign > 0 ) ) continue; // 押した向きと分岐の向きが一致する時だけ
        if ( bp.targetRail < 0 || bp.targetRail >= ( int ) rails.size() ) continue;

        const SplineRail& tr = rails[bp.targetRail];
        float len = tr.GetLength();
        if ( len <= 0.0f ) continue;

        float oldFootY = cur.GetPositionByDistance(currentDistance_).y;
        float margin = ( std::min )( 0.15f, len * 0.25f ); // 端ちょうどに乗らないよう少し内側へ
        currentRailIndex_ = bp.targetRail;
        currentDistance_  = std::clamp(bp.targetDist, margin, len - margin);
        // 空中で分岐した時は、見た目のワールドYが飛ばないよう heightOffset_ を補正
        if ( !isGrounded_ ) {
            float newFootY = tr.GetPositionByDistance(currentDistance_).y;
            heightOffset_ += oldFootY - newFootY;
        }
        switchCooldown_ = 0.25f;
        dsSign_         = 0.0f; // 分岐先では次の入力で進行方向を決め直す
        transitioned    = true;
        return true;
    }
    return false;
}

// ジャンプ＋ふんばり＋着地。穴の上で降りてきて空中状態になったら true（呼び出し側は return）。
bool Player::UpdateJumpAndLand(const SplineRail& rail, const std::vector<SplineRail>& rails, float dt){
    Input* input = Input::GetInstance();
    const float kFloatTime   = 0.6f;  // ふんばりで滞空できる最大時間（秒）
    const float kFloatTarget = 1.2f;  // 滞空中になめらかに近づく上向き速度
    const float kFloatEase   = 6.0f;  // 目標へ近づく速さ

    if ( isGrounded_ && !movementLocked_ && input->Triggerkey(DIK_SPACE) ) {
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
    if ( heightOffset_ <= 0.0f ) {
        if ( IsOverHole(rails) ) {
            // 穴の上で降りてきた → レールを離れて自由落下（下の別レールに着地できる）
            EnterAir(rail.GetPositionByDistance(currentDistance_), rail.GetTangentByDistance(currentDistance_), jumpVelocity_, 0.15f);
            return true;
        }
        // 地面あり＆降りてきた → 着地
        heightOffset_   = 0.0f;
        jumpVelocity_   = 0.0f;
        isGrounded_     = true;
        flutterCdTimer_ = 0.0f;
    }
    return false;
}

// 最終的な座標・向きを確定する。乗り移りの瞬間移動は posSmooth_ で滑らかに繋ぐ。
void Player::FinalizePosition(const SplineRail& rail, const Vector3& worldBefore, bool transitioned, float dt){
    Vector3 basePos = rail.GetPositionByDistance(currentDistance_);
    basePos.y += heightOffset_;

    // 乗り移りでレール座標が飛んだら、その差分を平滑化に回す（動くレールでもワープしない）。
    if ( transitioned ) {
        Vector3 jump = { worldBefore.x - basePos.x, worldBefore.y - basePos.y, worldBefore.z - basePos.z };
        if ( Length(jump) < 3.0f ) posSmooth_ = jump; // 大ワープ(リスポーン等)は補間しない
    }
    float k = std::min(14.0f * dt, 1.0f); // 約0.15秒で 0 へ減衰
    posSmooth_.x -= posSmooth_.x * k;
    posSmooth_.y -= posSmooth_.y * k;
    posSmooth_.z -= posSmooth_.z * k;
    position_ = { basePos.x + posSmooth_.x, basePos.y + posSmooth_.y, basePos.z + posSmooth_.z };

    // 落下死 → スタートへリスポーン
    if ( position_.y < kKillY ) { Initialize(); return; }

    // 向き：実際に進んでいる方向（接線 × 進行符号）へ向ける。
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

    // ---- 空中の軌道修正（進行方向の前後だけ）----
    //   空中に出た時の進行方向(airDir_)に沿った前後だけ速度を増減できる。
    //   横方向の自由移動は不可（落下中に WASD で自由に動き回れないようにする）。
    if ( Length(airDir_) > 1e-4f ) {
        // キー→ワールド方向の対応はカメラの向きで回す（レール上と同じ操作感にする）
        int kPX, kMX, kPZ, kMZ;
        GetWorldKeys(kPX, kMX, kPZ, kMZ);
        float ax = 0.0f, az = 0.0f;
        if ( input->Pushkey(( BYTE ) kPX) ) ax += 1.0f;
        if ( input->Pushkey(( BYTE ) kMX) ) ax -= 1.0f;
        if ( input->Pushkey(( BYTE ) kPZ) ) az += 1.0f;
        if ( input->Pushkey(( BYTE ) kMZ) ) az -= 1.0f;
        float along = ax * airDir_.x + az * airDir_.z; // 進行方向成分(+前/-後)

        const float kAccelFwd = 12.0f;             // 前後の加速
        const float kMaxFwd   = moveSpeed_ * 0.9f;  // 前後の最高速度

        // 進行方向(airDir_)に沿ってだけ加速。横成分は生まれない＝軌道は直線のまま
        airVelocity_.x += airDir_.x * along * kAccelFwd * dt;
        airVelocity_.z += airDir_.z * along * kAccelFwd * dt;

        // 速度を進行方向(±)に分解してクランプ。横ブレ分は捨てる
        float sAlong = airVelocity_.x * airDir_.x + airVelocity_.z * airDir_.z;
        sAlong = std::clamp(sAlong, -kMaxFwd, kMaxFwd);
        airVelocity_.x = airDir_.x * sAlong;
        airVelocity_.z = airDir_.z * sAlong;
    }
    // airDir_ が0（真下に落下）の時は水平入力を受け付けない＝その場でまっすぐ落ちる

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
            if ( !r.visible ) continue; // 見えない連結レールには着地しない（床ではなく「道」なので）

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

            // 着地：到達したレール点に合わせる。水平のズレは平滑化に回して見た目を滑らかに。
            {
                Vector3 jump = { position_.x - cp.x, 0.0f, position_.z - cp.z };
                if ( Length(jump) < 3.0f ) posSmooth_ = jump;
            }
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
