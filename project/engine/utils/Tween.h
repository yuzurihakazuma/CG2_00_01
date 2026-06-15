#pragma once
// =============================================================
//  Tween : 値を「N秒かけて A→B」に補間するヘルパー（ヘッダオンリー）
//   float でも Vector3 でも使える（+,-,*float が定義されていればOK）。
//   毎フレーム Update(dt) を呼び、Value() で現在値を取る。
//
//   使い方:
//     Tween<Vector3> t;
//     t.Start(startPos, endPos, 0.5f, Easing::EaseOutCubic);
//     ...
//     t.Update(dt);
//     obj.SetPos(t.Value());
//     if (t.IsFinished()) { ... }
// =============================================================
#include "engine/utils/Easing.h"

template<typename T>
class Tween{
public:
    using EaseFunc = float ( * )( float );

    // 補間開始（duration 秒で from→to）
    void Start(const T& from, const T& to, float duration, EaseFunc ease = Easing::Linear){
        from_ = from;
        to_ = to;
        duration_ = ( duration > 1e-6f ) ? duration : 1e-6f;
        ease_ = ease ? ease : Easing::Linear;
        elapsed_ = 0.0f;
        active_ = true;
        current_ = from;
    }

    // 毎フレーム呼ぶ
    void Update(float dt){
        if ( !active_ ) return;
        elapsed_ += dt;
        float t = elapsed_ / duration_;
        if ( t >= 1.0f ) { t = 1.0f; active_ = false; }
        float k = ease_(t);
        // from + (to - from) * k （Vector3/float ともに operator が必要）
        current_ = from_ + ( to_ - from_ ) * k;
    }

    const T& Value() const { return current_; }
    bool IsActive() const { return active_; }
    bool IsFinished() const { return !active_ && elapsed_ >= duration_; }
    float Progress() const { return ( duration_ > 0.0f ) ? ( elapsed_ / duration_ ) : 1.0f; }

    // 最後まで進めて止める
    void Finish(){ current_ = to_; active_ = false; elapsed_ = duration_; }
    // 即座に止める（現在値はそのまま）
    void Stop(){ active_ = false; }

private:
    T from_{};
    T to_{};
    T current_{};
    float duration_ = 1.0f;
    float elapsed_ = 0.0f;
    bool  active_ = false;
    EaseFunc ease_ = Easing::Linear;
};
