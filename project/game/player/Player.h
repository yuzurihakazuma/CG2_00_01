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

private: // メンバ変数

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

    // 空中状態の更新（自由落下・着地判定・落下死）
    void UpdateAir(const std::vector<SplineRail>& allRails, float dt);
};
