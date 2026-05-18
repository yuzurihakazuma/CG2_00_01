#pragma once
#include "engine/math/VectorMath.h"


// 前方宣言
class SplineRail;

// プレイヤークラス
class Player {
public:
	// 初期化
    void Initialize();

    void Update(const SplineRail& currentRail);


    const Vector3& GetPosition() const { return position_; }
	const Vector3& GetRotation() const{ return rotation_; }
    const Vector3& GetScale() const { return scale_; }

    void SetPosition(const Vector3& pos) { position_ = pos; }
	void SetRotation(const Vector3& rot){ rotation_ = rot; }
    void SetScale(const Vector3& scale) { scale_ = scale; }

private: // 関数

	// レールに沿って移動する関数
    void UpdateRailMovement(const SplineRail& rail);

private: // メンバ変数
    Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 rotation_ { 0.0f, 0.0f, 0.0f };
    Vector3 scale_{ 1.0f, 1.0f, 1.0f };

    float moveSpeed_ = 5.0f;
	float currentT_ = 0.0f; // レール上の現在の進行度

    float jumpVelocity_ = 0.0f; // ジャンプの初速・落下速度
    float heightOffset_ = 0.0f; // レールからの「浮き具合」
};