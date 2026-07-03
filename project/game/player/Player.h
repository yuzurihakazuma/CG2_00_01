#pragma once
#include "engine/math/VectorMath.h"
#include <vector>

// 前方宣言
class SplineRail;

// プレイヤークラス（レール上を距離ベースで移動する）
class Player{
public:
    // 初期化
    void Initialize();

    // 複数のレール情報を受け取って、自動で移動や乗り換えを行う
    void Update(const std::vector<SplineRail>& allRails);


    const Vector3& GetPosition() const{ return position_; }
    const Vector3& GetRotation() const{ return rotation_; }
    const Vector3& GetScale() const{ return scale_; }

    void SetPosition(const Vector3& pos){ position_ = pos; }
    void SetRotation(const Vector3& rot){ rotation_ = rot; }
    void SetScale(const Vector3& scale){ scale_ = scale; }

    // 接地状態の取得（空中判定、および踏みつけ判定に使用）
    bool IsGrounded() const{ return isGrounded_; }

    // 踏みつけ成功時にプレイヤーを上へ跳ね上がらせる
    void Bounce();

    // 卵を構えている間など、移動・ジャンプを止める（その場で待機）
    void SetMovementLocked(bool locked){ movementLocked_ = locked; }

    // --- ノードエディタの「→ ゲーム値」用：調整したい変数のポインタを公開 ---
    //   （Bloom の SetTargetEmissivePower と同じ、エディタに float* を登録するイディオム）
    float* MoveSpeedPtr(){ return &moveSpeed_; }
    float* JumpPowerPtr(){ return &jumpPower_; }

    // スタート地点（リスポーン先）。マップ設定から Sync 後にシーンが渡す
    void SetSpawn(int rail, float dist){ spawnRail_ = rail; spawnDist_ = dist; }

    // カメラの向き(yaw)。シーンが毎フレーム渡す。
    //   カメラが回り込んだ時（180°向き切替など）に、WASD→ワールド方向の割り当てを
    //   画面基準に合わせて回すために使う（Dを押せば常に画面の右へ進む）。
    void SetCameraYaw(float yawRad){ camYaw_ = yawRad; }

private: // メンバ変数

    // スタート地点（Initialize / リスポーンで使う）
    int   spawnRail_ = 0;
    float spawnDist_ = 0.0f;

    // カメラの向き(yaw)。入力のキー割り当てを画面基準に回すために使う
    float camYaw_ = 0.0f;
    // 直近に確定したキー割り当ての向き(0〜3)。カメラ回転の途中は前回を維持（ちらつき防止）
    mutable int lastKeyQuad_ = 0;

    bool movementLocked_ = false; // true の間は移動・ジャンプ入力を無視（構え中など）


    Vector3 position_ { 0.0f, 0.0f, 0.0f };
    Vector3 rotation_ { 0.0f, 0.0f, 0.0f };
    Vector3 scale_ { 1.0f, 1.0f, 1.0f };

    // ---- レール移動の主状態（これだけで位置が決まる）----
    float moveSpeed_ = 5.0f;          // 移動速度 (m/s)
    float currentDistance_ = 0.0f;    // ★主状態：現在レール上を進んだ距離 (m)
    int   currentRailIndex_ = 0;      // 現在乗っているレール番号

    // 入力(前後)→距離 の符号。ジャンクションで反転レールに入った時の連続性に使う
    // （holdしている同じキーで「同じ物理方向」へ進み続けられるようにするため）
    int   moveSign_ = 1;

    // ---- 進行方向の記憶 ----
    // キーを「押した瞬間」だけワールド方向(横=X/縦=Z)から進行符号を決め、
    // 押しっぱなしの間は保持する。円状レールの頂点や急カーブで接線の
    // 軸成分が反転しても止まらず・逆走しないための仕組み。
    float dsSign_ = 0.0f;        // 現在の進行符号(±1)。0=停止中
    float prevMoveInput_ = 0.0f; // 前フレームの移動入力（押した瞬間の検出用）
    bool  atJunction_ = false;   // 別レールへ合流した直後の停止中か（押し直すまで動かない）

    // 乗り換え(奥/手前スイッチ)の連打防止タイマー (秒)
    float switchCooldown_ = 0.0f;

    // ---- ジャンプ（フレームレート非依存：m, m/s, m/s^2 で扱う）----
    float heightOffset_ = 0.0f;       // レールからの浮き具合 (m)
    float jumpVelocity_ = 0.0f;       // 上下速度 (m/s)
    bool  isGrounded_ = true;         // 接地しているか（空中で再ジャンプ不可）
    float jumpPower_ = 8.0f;          // ジャンプ初速 (m/s)
    float gravity_ = 25.0f;           // 重力加速度 (m/s^2)

    // ---- ふんばりジャンプ（ヨッシー風：長押しでなめらかにふわっと滞空）----
    float flutterCdTimer_ = 0.0f;     // 滞空できる残り時間 (秒)

    // ---- 空中状態（レール外）：穴の飛び越え・落下用 ----
    // レール端から飛び出すとレールから離れて自由落下し、下にレールがあれば着地、
    // 無ければ落下してリスポーンする
    bool    inAir_ = false;                  // レールから離れて空中にいるか
    Vector3 airVelocity_ { 0.0f, 0.0f, 0.0f }; // 空中の速度 (m/s)
    float   airLandCooldown_ = 0.0f;         // 飛び出した直後に "元のレール" へ即着地しない猶予 (秒)
    int     airFromRail_ = -1;               // 飛び出した元のレール番号（その猶予中だけ再着地を抑止）
    Vector3 airDir_ { 0.0f, 0.0f, 0.0f };    // 空中に出た時の進行方向(水平・単位)。WASDはこの前後だけ受付

    // ---- 乗り移り時の見た目補間（ワープ隠し）----
    // 別レールへドッキング/乗り換え/着地でレール座標が飛ぶと見た目が瞬間移動する。
    // 飛んだ差分をこのオフセットに入れ、毎フレーム0へ減衰させて滑らかに繋ぐ。
    Vector3 posSmooth_ { 0.0f, 0.0f, 0.0f };

    // =================================================================
    //  Update の内部処理（読みやすさのため段階ごとに関数化。状態は全部メンバ）。
    //  すべて Player の状態を直接読み書きするため、別クラスに切り出すより
    //  Player のメンバ関数にするのが素直（状態の受け渡しが増えない）。
    // =================================================================

    // 空中状態の更新（自由落下・着地判定・落下死）
    void UpdateAir(const std::vector<SplineRail>& allRails, float dt);

    // 足元がどれかのレールの「穴」区間の真上にあるか（乗り換え地点でも取りこぼさない）
    bool IsOverHole(const std::vector<SplineRail>& rails) const;

    // レールを離れて空中(弾道)状態へ移行する共通処理。
    //   pos=開始位置 / tangent=接線 / upVel=初速Y / landCooldown=元レール再着地の抑止猶予
    void EnterAir(const Vector3& pos, const Vector3& tangent, float upVel, float landCooldown);
    // 端から空中へ飛び出す（Gap レール用）。edgeS=飛び出す端の距離
    void DetachToAir(const SplineRail& cur, float edgeS);

    // カメラの向き(90°単位)に応じた「実キー → ワールド方向」の割り当てを返す。
    //   戻り値は DIK_～ のキーコード。カメラが正面(180°)なら D は「ワールド-X」担当になる等、
    //   どの向きでも「Dを押せば画面の右へ進む」ようにするための表引き。
    void GetWorldKeys(int& plusX, int& minusX, int& plusZ, int& minusZ) const;

    // 入力をレールタイプに応じて「移動入力」と「乗り換え入力」へ振り分ける
    void ReadRailInput(bool curHorizontal, float& moveInput, int& switchInput) const;
    // 今のレールを距離で進める（進行方向の記憶つき）
    void MoveAlongRail(const SplineRail& cur, bool curHorizontal, float moveInput, float dt);

    // 連結している端へ地続きで持ち越す（成功:true）
    bool TryContinueToConnected(const std::vector<SplineRail>& rails, const SplineRail& cur, int connIdx, bool enterFront, float over);
    // 端の近くにある別レール本体へ合流（動的ドッキング。成功:true）
    bool TryJoinNearbyBody(const std::vector<SplineRail>& rails, const SplineRail& cur, float edgeS);
    // レール終端の処理（持ち越し/合流/落下/クランプ）。空中へ飛び出したら true（呼び出し側は return）
    bool HandleRailEnds(const std::vector<SplineRail>& rails, const SplineRail& cur, bool& transitioned);

    // 別タイプの近接レールへ乗り換える（押した方向に伸びているもの）
    void TrySwitchRail(const std::vector<SplineRail>& rails, const SplineRail& cur, bool curHorizontal, int switchInput, bool& transitioned);

    // T字路の途中分岐（branchPoints）で分岐する（同タイプ同士でも乗り換え可能。成功:true）
    bool TryBranch(const std::vector<SplineRail>& rails, const SplineRail& cur, int switchInput, bool& transitioned);

    // ジャンプ＋ふんばり＋着地。穴の上で降りてきて空中状態になったら true（呼び出し側は return）
    bool UpdateJumpAndLand(const SplineRail& rail, const std::vector<SplineRail>& rails, float dt);

    // 最終的な座標・向きを確定（乗り移りの見た目平滑化つき。落下死ならリスポーン）
    void FinalizePosition(const SplineRail& rail, const Vector3& worldBefore, bool transitioned, float dt);
};
