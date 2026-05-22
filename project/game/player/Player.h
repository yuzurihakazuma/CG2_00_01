#pragma once
#include "engine/math/VectorMath.h"
#include <vector>

// 前方宣言
class SplineRail;

// プレイヤークラス
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

    float moveSpeed_ = 5.0f;
    float currentT_ = 0.0f; // レール上の現在の進行度
    float currentDistance_ = 0.0f; // 現在レール上を何メートル進んだか

    int currentRailIndex_ = 0; // 現在乗っているレールの番号

    float jumpVelocity_ = 0.0f; // ジャンプの初速・落下速度
    float heightOffset_ = 0.0f; // レールからの「浮き具合」

    int traverseSign_ = 1; // レール上の進行符号: 1=通常, -1=終端から入った

    bool isHorizontal_ = true;  // レール乗り換え時のみ更新
    int  lastRailIndex_ = -1;   // 前フレームのレール番号

};