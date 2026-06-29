#pragma once
#include "engine/math/struct.h"
#include <memory>

class Obj3d;

// =====================================================================
//  Egg：ヨッシーの卵1個。実体（Obj3d）を持ち、状態で振る舞いを変える。
//   高レベルの状態（Held/Flying/Broken）はステートマシン。
//   「生まれて後ろに並ぶ・追従」は状態ではなく目標位置への補間(lerp)で表現する。
// =====================================================================
enum class EggState {
    Held,    // ヨッシーの後ろで待機（目標スロットへ寄っていく）
    Flying,  // 投げられて飛行中
    Broken,  // 割れた（少ししてから消える）
};

class Egg {
public:
    explicit Egg(const Vector3& birthPos);
    ~Egg();

    // 実体（見た目のObj3d）を持たせる。baseScale=通常時の大きさ。
    void AttachVisual(std::unique_ptr<Obj3d> obj, const Vector3& baseScale);

    void Update(float dt); // 状態に応じた更新＋見た目の更新
    void Draw() const;

    // --- 状態遷移 ---
    void Throw(const Vector3& dir, float speed); // Held → Flying
    void Break();                                // → Broken

    // 並ぶ目標位置（後ろのスロット）。Held の間、ここへ滑らかに寄っていく。
    void SetTarget(const Vector3& t) { target_ = t; }

    EggState State() const { return state_; }
    bool IsHeld()   const { return state_ == EggState::Held; }
    bool IsFlying() const { return state_ == EggState::Flying; }
    bool IsBroken() const { return state_ == EggState::Broken; }
    bool IsDead()   const { return state_ == EggState::Broken && brokenTimer_ <= 0.0f; }

    const Vector3& GetPosition() const { return pos_; }
    void  SetPosition(const Vector3& p) { pos_ = p; }
    float GetRadius() const { return radius_; }

private:
    EggState state_ = EggState::Held;
    Vector3  pos_ { 0.0f, 0.0f, 0.0f };
    Vector3  target_ { 0.0f, 0.0f, 0.0f }; // 並ぶ目標（後ろのスロット）
    Vector3  vel_ { 0.0f, 0.0f, 0.0f };    // 飛行中の速度
    float    radius_ = 0.4f;
    float    flyTimer_   = 0.0f;
    float    brokenTimer_ = 0.0f;
    float    bornTimer_   = 0.25f;         // 生まれた直後の「ポンッ」と出る演出時間
    float    spinAngle_   = 0.0f;          // 見た目のくるくる回転

    Vector3  baseScale_ { 0.4f, 0.4f, 0.4f };
    std::unique_ptr<Obj3d> obj_;           // 実体（見た目）
};
