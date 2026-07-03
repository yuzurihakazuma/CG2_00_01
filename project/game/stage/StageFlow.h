#pragma once
#include "engine/math/struct.h"

class RailField;
class HitFeel;

// =====================================================================
//  StageFlow：ステージの進行状態（ゴール判定・表示）。
//   将来のチェックポイント・クリア遷移・リザルトの置き場でもある。
//  シーンから分離した理由：ステージの「進行ルール」はゲーム進行と独立に育つ関心事のため。
// =====================================================================
class StageFlow {
public:
    // マップが変わった時に呼ぶ（ゴール状態をリセット）
    void Reset(){ goalReached_ = false; }

    // ゴール判定＋ゴールマーカー描画（Play中に毎フレーム呼ぶ）
    void Update(const Vector3& playerPos, const RailField& rails, HitFeel& hitFeel);

    // ゴール到達表示（ImGui。DrawDebugUI から呼ぶ）
    void DrawDebugUI();

    bool IsGoalReached() const { return goalReached_; }

private:
    bool goalReached_ = false;
};
