#pragma once
// =============================================================
//  Easing : イージング関数群（ヘッダオンリー・依存なし）
//   入力 t は 0.0〜1.0 を想定（範囲外でも壊れないようにクランプ）。
//   返り値も基本 0.0〜1.0（Back/Elastic は一時的に外れることがある）。
//
//   使い方:
//     float k = Easing::EaseOutCubic(t);
//     pos = Easing::Lerp(start, end, Easing::EaseInOutQuad(t));
// =============================================================
#include <cmath>

namespace Easing{

    inline float Clamp01(float t){ return ( t < 0.0f ) ? 0.0f : ( t > 1.0f ? 1.0f : t ); }

    // 線形補間（スカラー）
    inline float Lerp(float a, float b, float t){ return a + ( b - a ) * t; }

    constexpr float kPi = 3.14159265358979323846f;

    // --- Linear ---
    inline float Linear(float t){ return Clamp01(t); }

    // --- Sine ---
    inline float EaseInSine(float t){ t = Clamp01(t); return 1.0f - std::cos(( t * kPi ) * 0.5f); }
    inline float EaseOutSine(float t){ t = Clamp01(t); return std::sin(( t * kPi ) * 0.5f); }
    inline float EaseInOutSine(float t){ t = Clamp01(t); return -( std::cos(kPi * t) - 1.0f ) * 0.5f; }

    // --- Quad ---
    inline float EaseInQuad(float t){ t = Clamp01(t); return t * t; }
    inline float EaseOutQuad(float t){ t = Clamp01(t); return 1.0f - ( 1.0f - t ) * ( 1.0f - t ); }
    inline float EaseInOutQuad(float t){ t = Clamp01(t); return ( t < 0.5f ) ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f; }

    // --- Cubic ---
    inline float EaseInCubic(float t){ t = Clamp01(t); return t * t * t; }
    inline float EaseOutCubic(float t){ t = Clamp01(t); float u = 1.0f - t; return 1.0f - u * u * u; }
    inline float EaseInOutCubic(float t){ t = Clamp01(t); return ( t < 0.5f ) ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f; }

    // --- Quart ---
    inline float EaseInQuart(float t){ t = Clamp01(t); return t * t * t * t; }
    inline float EaseOutQuart(float t){ t = Clamp01(t); float u = 1.0f - t; return 1.0f - u * u * u * u; }
    inline float EaseInOutQuart(float t){ t = Clamp01(t); return ( t < 0.5f ) ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) * 0.5f; }

    // --- Expo ---
    inline float EaseInExpo(float t){ t = Clamp01(t); return ( t <= 0.0f ) ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f); }
    inline float EaseOutExpo(float t){ t = Clamp01(t); return ( t >= 1.0f ) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
    inline float EaseInOutExpo(float t){
        t = Clamp01(t);
        if ( t <= 0.0f ) return 0.0f;
        if ( t >= 1.0f ) return 1.0f;
        return ( t < 0.5f ) ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
                            : ( 2.0f - std::pow(2.0f, -20.0f * t + 10.0f) ) * 0.5f;
    }

    // --- Back（行き過ぎて戻る）---
    inline float EaseInBack(float t){ t = Clamp01(t); const float c1 = 1.70158f, c3 = c1 + 1.0f; return c3 * t * t * t - c1 * t * t; }
    inline float EaseOutBack(float t){ t = Clamp01(t); const float c1 = 1.70158f, c3 = c1 + 1.0f; float u = t - 1.0f; return 1.0f + c3 * u * u * u + c1 * u * u; }
    inline float EaseInOutBack(float t){
        t = Clamp01(t);
        const float c1 = 1.70158f, c2 = c1 * 1.525f;
        return ( t < 0.5f )
            ? ( std::pow(2.0f * t, 2.0f) * ( ( c2 + 1.0f ) * 2.0f * t - c2 ) ) * 0.5f
            : ( std::pow(2.0f * t - 2.0f, 2.0f) * ( ( c2 + 1.0f ) * ( t * 2.0f - 2.0f ) + c2 ) + 2.0f ) * 0.5f;
    }

    // --- Elastic（ばね）---
    inline float EaseOutElastic(float t){
        t = Clamp01(t);
        if ( t <= 0.0f ) return 0.0f;
        if ( t >= 1.0f ) return 1.0f;
        const float c4 = ( 2.0f * kPi ) / 3.0f;
        return std::pow(2.0f, -10.0f * t) * std::sin(( t * 10.0f - 0.75f ) * c4) + 1.0f;
    }

    // --- Bounce（跳ねる）---
    inline float EaseOutBounce(float t){
        t = Clamp01(t);
        const float n1 = 7.5625f, d1 = 2.75f;
        if ( t < 1.0f / d1 )       return n1 * t * t;
        else if ( t < 2.0f / d1 ){ t -= 1.5f / d1;  return n1 * t * t + 0.75f; }
        else if ( t < 2.5f / d1 ){ t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
        else                     { t -= 2.625f / d1; return n1 * t * t + 0.984375f; }
    }
    inline float EaseInBounce(float t){ return 1.0f - EaseOutBounce(1.0f - t); }
}
