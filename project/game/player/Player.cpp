#include "game/player/Player.h"
#include "Engine/Base/Input.h"
#include "engine/math/VectorMath.h"
#include "engine/3d/SplineRail.h"
#include <algorithm>
#include <cmath>

using namespace VectorMath;

void Player::Initialize() {
    position_ = { 0.0f, 0.0f, 0.0f };
	rotation_ = { 0.0f, 0.0f, 0.0f };
    scale_ = { 1.0f, 1.0f, 1.0f };
	currentT_ = 0.0f;
}

void Player::Update(const SplineRail& currentRail){

	UpdateRailMovement(currentRail);



}

void Player::UpdateRailMovement(const SplineRail& rail){
	if ( rail.nodes.size() < 2 ){ return; }

	Input* input = Input::GetInstance();
	float maxT = static_cast< float >( rail.nodes.size() - 1 );

	// 1. レールの進行(T)の更新
	if ( input->Pushkey(DIK_D) ){ currentT_ += moveSpeed_ * ( 0.1f / 60.0f ); }
	if ( input->Pushkey(DIK_A) ){ currentT_ -= moveSpeed_ * ( 0.1f / 60.0f ); }
	currentT_ = std::clamp(currentT_, 0.0f, maxT);

	// 2. ジャンプの処理（★新規追加）
	// 地面（レール）にいて、スペースキーが押されたらジャンプ
	if ( heightOffset_ == 0.0f && input->Triggerkey(DIK_SPACE) ) {
		jumpVelocity_ = 0.3f; // ジャンプ力
	}

	// 重力と高さの計算
	jumpVelocity_ -= 0.015f; // 重力
	heightOffset_ += jumpVelocity_;

	// 地面（レール）に着地したら止まる
	if ( heightOffset_ <= 0.0f ) {
		heightOffset_ = 0.0f;
		jumpVelocity_ = 0.0f;
	}

	// 3. 座標の合成（★ここが超重要！）
	// レールの基礎座標を取得
	Vector3 railBasePos = rail.EvaluatePosition(currentT_);

	// 最終的な座標 = レールの座標 + (Y軸方向に heightOffset_ 分だけ浮かす)
	position_ = railBasePos;
	position_.y += heightOffset_; // これでレールの上をジャンプできる！

	// 4. 向きの計算（既存のまま）
	float nextT = std::min(currentT_ + 0.01f, maxT);
	if ( nextT > currentT_ ){
		Vector3 nextPos = rail.EvaluatePosition(nextT);
		// ※向きを計算する時は、ジャンプの高さを含めない(railBasePosを使う)のがコツです
		Vector3 forward = Normalize(Subtract(nextPos, railBasePos));
		rotation_.y = std::atan2(forward.x, forward.z);
		rotation_.x = std::asin(-forward.y);
		rotation_.z = 0.0f;
	}
}