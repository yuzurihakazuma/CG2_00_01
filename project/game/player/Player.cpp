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
    traverseSign_ = 1;

    isHorizontal_ = true;
    lastRailIndex_ = -1;
}

void Player::Update(const std::vector<SplineRail>& allRails){
    if ( allRails.empty() || currentRailIndex_ >= ( int ) allRails.size() ) return;

    Input* input = Input::GetInstance();
    const SplineRail& activeRail = allRails[currentRailIndex_];
    if ( activeRail.nodes.size() < 2 ) return;

    // ==================================================
    // 0. 初回フレームだけ isHorizontal_ を初期化
    // ==================================================
    if ( lastRailIndex_ < 0 ) {
        Vector3 t = activeRail.EvaluateTangent(currentT_);
        isHorizontal_ = std::abs(t.x) > std::abs(t.z);
        lastRailIndex_ = currentRailIndex_;
    }

    // ==================================================
    // 1. キャッシュした isHorizontal_ でキー判定（毎フレーム再計算しない）
    // ==================================================
    float moveInput = 0.0f;
    float switchInput = 0.0f;

    if ( isHorizontal_ ) {
        if ( input->Pushkey(DIK_D) )    moveInput += 1.0f;
        if ( input->Pushkey(DIK_A) )    moveInput -= 1.0f;
        if ( input->Triggerkey(DIK_W) ) switchInput += 1.0f;
        if ( input->Triggerkey(DIK_S) ) switchInput -= 1.0f;
    } else {
        if ( input->Pushkey(DIK_W) )    moveInput += 1.0f;
        if ( input->Pushkey(DIK_S) )    moveInput -= 1.0f;
        if ( input->Triggerkey(DIK_D) ) switchInput += 1.0f;
        if ( input->Triggerkey(DIK_A) ) switchInput -= 1.0f;
    }

    float moveStep = moveSpeed_ * ( 1.0f / 60.0f ) * moveInput * static_cast< float >( traverseSign_ );
    currentDistance_ += moveStep;

    // ==================================================
   // 2. 終端での乗り継ぎ（インデックスで直接接続・距離チェック不要）
   // ==================================================
    int prevRailIndex = currentRailIndex_;
    bool terminalFired = false;

    if ( currentDistance_ < 0.0f ) {
        int connIdx = activeRail.frontConnIndex;
        if ( connIdx >= 0 && connIdx < ( int ) allRails.size() ) {
            currentRailIndex_ = connIdx;
            if ( activeRail.frontConnToFront ) {
                currentDistance_ = 0.0f;      traverseSign_ = 1;
            } else {
                currentDistance_ = allRails[connIdx].totalLength_; traverseSign_ = -1;
            }
        } else {
            currentDistance_ = 0.0f;
        }
        terminalFired = true;

    } else if ( currentDistance_ > activeRail.totalLength_ ) {
        int connIdx = activeRail.backConnIndex;
        if ( connIdx >= 0 && connIdx < ( int ) allRails.size() ) {
            currentRailIndex_ = connIdx;
            if ( activeRail.backConnToFront ) {
                currentDistance_ = 0.0f;      traverseSign_ = 1;
            } else {
                currentDistance_ = allRails[connIdx].totalLength_; traverseSign_ = -1;
            }
        } else {
            currentDistance_ = activeRail.totalLength_;
        }
        terminalFired = true;
    }

    // ==================================================
    // 3. 分岐乗り換え（プレイヤー位置からXY近傍ノードを直接探索）
    // ==================================================
    if ( switchInput != 0.0f && !terminalFired ) {
        float bestXYDist     = 1.5f;   // XY平面での探索半径（単位: m）
        int   bestRail       = -1;
        float bestNodeDist   = 0.0f;

        for ( size_t i = 0; i < allRails.size(); ++i ) {
            if ( ( int ) i == currentRailIndex_ ) continue;

            for ( size_t nodeIdx = 0; nodeIdx < allRails[i].nodes.size(); ++nodeIdx ) {
                // 末尾ノードへ飛ぶとtotalLength_になり即終端転送でワープするのでスキップ
                if ( nodeIdx == allRails[i].nodes.size() - 1 ) continue;

                Vector3 nodePos = allRails[i].nodes[nodeIdx];
                float dx = position_.x - nodePos.x;
                float dy = position_.y - nodePos.y;
                float xyDist = std::sqrt(dx * dx + dy * dy);
                if ( xyDist >= bestXYDist ) continue;

                float zDiff = nodePos.z - position_.z;
                const float minZDiff = 0.5f;
                if ( switchInput > 0.0f && zDiff < minZDiff )  continue;
                if ( switchInput < 0.0f && zDiff > -minZDiff ) continue;

                bestXYDist   = xyDist;
                bestRail     = ( int ) i;
                bestNodeDist = allRails[i].GetDistanceFromT(static_cast< float >( nodeIdx ));
            }
        }

        if ( bestRail >= 0 ) {
            currentRailIndex_ = bestRail;
            currentDistance_  = bestNodeDist;
            traverseSign_     = 1;

            // 乗り換え直後に向きを即時更新（moveInput=0でも崩れない）
            float newT = allRails[bestRail].GetTFromDistance(bestNodeDist);
            Vector3 newTan = allRails[bestRail].EvaluateTangent(newT);
            if ( Length(newTan) > 0.001f ) {
                rotation_.y = std::atan2(newTan.x, newTan.z);
                rotation_.x = 0.0f;
                rotation_.z = 0.0f;
            }
        }
    }

    // ==================================================
    // レールが変わった時だけ isHorizontal_ を更新
    // ==================================================
    if ( currentRailIndex_ != prevRailIndex ) {
        const SplineRail& newRail = allRails[currentRailIndex_];
        float entryT = newRail.GetTFromDistance(currentDistance_);
        Vector3 et = newRail.EvaluateTangent(entryT);
        isHorizontal_ = std::abs(et.x) > std::abs(et.z);
        lastRailIndex_ = currentRailIndex_;
    }

    // ==================================================
    // 4. 座標計算とジャンプ
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

    Vector3 railBasePos = finalRail.EvaluatePosition(currentT_);
    position_ = railBasePos;
    position_.y += heightOffset_;

    // 移動中だけ向きを更新・X/Z成分のみ使い傾かせない
    Vector3 newTangent = finalRail.EvaluateTangent(currentT_);
    if ( Length(newTangent) > 0.001f && moveInput != 0.0f ) {
        if ( moveStep < 0.0f ) {
            newTangent.x *= -1.0f;
            newTangent.z *= -1.0f;
        }
        rotation_.y = std::atan2(newTangent.x, newTangent.z);
        rotation_.x = 0.0f;
        rotation_.z = 0.0f;
    }
}