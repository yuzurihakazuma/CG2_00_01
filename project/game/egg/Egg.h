#pragma once
#include "engine/math/struct.h"

// =====================================================================
//  Egg：ヨッシーの卵1個。今は「状態管理」だけ（実体＝モデル描画はまだ持たない）。
//   状態遷移：
//     Held   … ヨッシーの後ろで待機（まだ投げていない）
//     Flying … 投げられて飛行中
//     Broken … 敵に当たった/時間切れで割れた（後始末待ち→消える）
// =====================================================================
enum class EggState {
    Held,
    Flying,
    Broken,
};

class Egg {
public:
    explicit Egg(const Vector3& pos) : pos_(pos) {}

    // 状態に応じた更新（Held=待機 / Flying=移動 / Broken=消滅猶予）
    void Update(float dt);

    // --- 状態遷移 ---
    void Throw(const Vector3& dir, float speed); // Held → Flying
    void Break();                                // → Broken

    EggState State() const { return state_; }
    bool IsHeld()   const { return state_ == EggState::Held; }
    bool IsFlying() const { return state_ == EggState::Flying; }
    bool IsBroken() const { return state_ == EggState::Broken; }
    bool IsDead()   const { return state_ == EggState::Broken && brokenTimer_ <= 0.0f; } // 完全に消えてよい

    const Vector3& GetPosition() const { return pos_; }
    void  SetPosition(const Vector3& p) { pos_ = p; }
    float GetRadius() const { return radius_; }

private:
    EggState state_ = EggState::Held; // 生成時はお腹に保持
    Vector3  pos_ { 0.0f, 0.0f, 0.0f };
    Vector3  vel_ { 0.0f, 0.0f, 0.0f }; // 飛行中の速度
    float    radius_ = 0.4f;            // 当たり判定半径（敵との衝突に使う予定）
    float    flyTimer_   = 0.0f;        // 飛行できる残り時間（時間切れで割れる）
    float    brokenTimer_ = 0.0f;       // 割れてから消えるまでの猶予
};
