#include "game/player/Player.h"
#include "Engine/Base/Input.h"
#include "engine/math/VectorMath.h"
#include "engine/rail/SplineRail.h"
#include <algorithm>
#include <cmath>

using namespace VectorMath;

void Player::Initialize(){
	position_ = { 0.0f, 0.0f, 0.0f };
	rotation_ = { 0.0f, 0.0f, 0.0f };
	scale_ = { 1.0f, 1.0f, 1.0f };
	currentT_ = 0.0f;
	currentDistance_ = 0.0f;
	currentRailIndex_ = 0;
	heightOffset_ = 0.0f;
	jumpVelocity_ = 0.0f;
}

// ★修正：すべてのレールを一括で管理し、自動乗り継ぎや入力を切り替える！
void Player::Update(const std::vector<SplineRail>& allRails){
	if ( allRails.empty() || currentRailIndex_ >= allRails.size() ) return;

	Input* input = Input::GetInstance();
	const SplineRail& activeRail = allRails[currentRailIndex_];

	// レールが短すぎる場合は処理しない
	if ( activeRail.nodes.size() < 2 ) return;

	// ==================================================
	// 1. レールの向きに合わせて、操作キーを「自動」で切り替える
	// ==================================================
	Vector3 tangent = activeRail.EvaluateTangent(currentT_);

	// X方向(横)の動きが大きいか、Z方向(奥)の動きが大きいかを判定
	bool isHorizontal = std::abs(tangent.x) > std::abs(tangent.z);

	float moveInput = 0.0f;   // 前進・後退の入力
	float switchInput = 0.0f; // 別の道へ飛び移るための入力

	if ( isHorizontal ) {
		// 横道なら A/D で進み、W/S で奥手前へ乗り換える
		if ( input->Pushkey(DIK_D) ) moveInput += 1.0f;
		if ( input->Pushkey(DIK_A) ) moveInput -= 1.0f;
		if ( input->Triggerkey(DIK_W) ) switchInput += 1.0f; // 奥へ
		if ( input->Triggerkey(DIK_S) ) switchInput -= 1.0f; // 手前へ
	} else {
		// 縦道なら W/S で進み、A/D で左右の道へ乗り換える
		if ( input->Pushkey(DIK_W) ) moveInput += 1.0f;
		if ( input->Pushkey(DIK_S) ) moveInput -= 1.0f;
		if ( input->Triggerkey(DIK_D) ) switchInput += 1.0f; // 右へ
		if ( input->Triggerkey(DIK_A) ) switchInput -= 1.0f; // 左へ
	}

	// 実際の進行方向に合わせて進む向きを補正（例：奥に進むレールなのに、手前に進んでしまうのを防ぐ）
	float moveStep = moveSpeed_ * ( 1.0f / 60.0f ) * moveInput;
	if ( isHorizontal ) {
		if ( tangent.x < 0.0f ) moveStep *= -1.0f;
	} else {
		if ( tangent.z < 0.0f ) moveStep *= -1.0f;
	}

	currentDistance_ += moveStep;


	// ==================================================
	// 2. 終点・始点に来た時、他のレールが繋がっていれば自動で乗り継ぐ！
	// ==================================================
	if ( currentDistance_ < 0.0f ) {
		Vector3 startPos = activeRail.nodes.front();
		bool connected = false;

		for ( size_t i = 0; i < allRails.size(); ++i ) {
			if ( i == currentRailIndex_ ) continue;

			// 距離を測って、0.5m以内なら繋がっているとみなす！
			Vector3 diffToFront = { startPos.x - allRails[i].nodes.front().x, startPos.y - allRails[i].nodes.front().y, startPos.z - allRails[i].nodes.front().z };
			Vector3 diffToBack = { startPos.x - allRails[i].nodes.back().x, startPos.y - allRails[i].nodes.back().y, startPos.z - allRails[i].nodes.back().z };

			if ( std::sqrt(diffToFront.x * diffToFront.x + diffToFront.y * diffToFront.y + diffToFront.z * diffToFront.z) < 0.5f ) {
				currentRailIndex_ = static_cast< int >( i );
				currentDistance_ = 0.0f; // 次の路線の始点からスタート
				connected = true; break;
			}
			if ( std::sqrt(diffToBack.x * diffToBack.x + diffToBack.y * diffToBack.y + diffToBack.z * diffToBack.z) < 0.5f ) {
				currentRailIndex_ = static_cast< int >( i );
				currentDistance_ = allRails[i].totalLength_; // 次の路線の終点からスタート
				connected = true; break;
			}
		}
		if ( !connected ) currentDistance_ = 0.0f; // 繋がってなければ行き止まり

	} else if ( currentDistance_ > activeRail.totalLength_ ) {
		Vector3 endPos = activeRail.nodes.back();
		bool connected = false;

		for ( size_t i = 0; i < allRails.size(); ++i ) {
			if ( i == currentRailIndex_ ) continue;

			Vector3 diffToFront = { endPos.x - allRails[i].nodes.front().x, endPos.y - allRails[i].nodes.front().y, endPos.z - allRails[i].nodes.front().z };
			Vector3 diffToBack = { endPos.x - allRails[i].nodes.back().x, endPos.y - allRails[i].nodes.back().y, endPos.z - allRails[i].nodes.back().z };

			if ( std::sqrt(diffToFront.x * diffToFront.x + diffToFront.y * diffToFront.y + diffToFront.z * diffToFront.z) < 0.5f ) {
				currentRailIndex_ = static_cast< int >( i );
				currentDistance_ = 0.0f;
				connected = true; break;
			}
			if ( std::sqrt(diffToBack.x * diffToBack.x + diffToBack.y * diffToBack.y + diffToBack.z * diffToBack.z) < 0.5f ) {
				currentRailIndex_ = static_cast< int >( i );
				currentDistance_ = allRails[i].totalLength_;
				connected = true; break;
			}
		}
		if ( !connected ) currentDistance_ = activeRail.totalLength_; // 行き止まり
	}

	// ==================================================
	// 3. 道中での分岐ジャンプ乗り換え
	// ==================================================
	if ( switchInput != 0.0f ) {
		for ( size_t i = 0; i < allRails.size(); ++i ) {
			if ( i == currentRailIndex_ ) continue;

			// 他の路線の各ポイント(ノード)とプレイヤーの距離をチェック
			for ( size_t nodeIdx = 0; nodeIdx < allRails[i].nodes.size(); ++nodeIdx ) {
				Vector3 nodePos = allRails[i].nodes[nodeIdx];
				Vector3 diff = { position_.x - nodePos.x, position_.y - nodePos.y, position_.z - nodePos.z };

				// もし3.0m以内に別の路線の点があれば飛び移る！
				if ( std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z) < 3.0f ) {
					currentRailIndex_ = static_cast< int >( i );
					currentDistance_ = allRails[i].distanceTable_[nodeIdx];
					break;
				}
			}
		}
	}


	// ==================================================
	// 4. 最終的な座標の計算とジャンプ処理
	// ==================================================
	const SplineRail& finalRail = allRails[currentRailIndex_];
	currentT_ = finalRail.GetTFromDistance(currentDistance_);

	if ( heightOffset_ == 0.0f && input->Triggerkey(DIK_SPACE) ) {
		jumpVelocity_ = 0.3f;
	}
	jumpVelocity_ -= 0.015f;
	heightOffset_ += jumpVelocity_;
	if ( heightOffset_ <= 0.0f ) {
		heightOffset_ = 0.0f;
		jumpVelocity_ = 0.0f;
	}

	// 座標の合成
	Vector3 railBasePos = finalRail.EvaluatePosition(currentT_);
	position_ = railBasePos;
	position_.y += heightOffset_;

	// 向きの合成 (移動してる方向にキャラを向かせる)
	Vector3 newTangent = finalRail.EvaluateTangent(currentT_);
	if ( Length(newTangent) > 0.001f ) {
		// もし後退しているなら向きを反転させる
		if ( moveStep < 0.0f ) {
			newTangent.x *= -1.0f; newTangent.y *= -1.0f; newTangent.z *= -1.0f;
		}
		rotation_.y = std::atan2(newTangent.x, newTangent.z);
		rotation_.x = std::asin(-newTangent.y);
		rotation_.z = 0.0f;
	}
}