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

    // 乗り換え(奥/手前スイッチ)の連打防止タイマー (秒)
    float switchCooldown_ = 0.0f;

    // ---- ジャンプ（フレームレート非依存：m, m/s, m/s^2 で扱う）----
    float heightOffset_ = 0.0f;       // レールからの浮き具合 (m)
    float jumpVelocity_ = 0.0f;       // 上下速度 (m/s)
    bool  isGrounded_ = true;         // 接地しているか（空中で再ジャンプ不可）
    float jumpPower_ = 8.0f;          // ジャンプ初速 (m/s)
    float gravity_ = 25.0f;           // 重力加速度 (m/s^2)
};
