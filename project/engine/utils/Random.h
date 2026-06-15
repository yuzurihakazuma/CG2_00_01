#pragma once
// =============================================================
//  Random : 乱数ユーティリティ（ヘッダオンリー）
//   std::mt19937 を内部に1つ持つシングルトン的アクセス。
//   使い方:
//     float a = Random::Float(0.0f, 1.0f);
//     int   n = Random::Int(1, 6);
//     Vector3 p = Random::InSphere(2.0f);
// =============================================================
#include <random>
#include "engine/math/struct.h"

namespace Random{

    // 内部の乱数エンジン（最初の呼び出し時に random_device で種を作る）
    inline std::mt19937& Engine(){
        static std::mt19937 engine{ std::random_device{}() };
        return engine;
    }

    // 種を固定したい時（リプレイ・テスト用）
    inline void Seed(unsigned int seed){ Engine().seed(seed); }

    // [min, max] の実数
    inline float Float(float minV, float maxV){
        std::uniform_real_distribution<float> dist(minV, maxV);
        return dist(Engine());
    }

    // [min, max] の整数（両端含む）
    inline int Int(int minV, int maxV){
        if ( minV > maxV ) { int t = minV; minV = maxV; maxV = t; }
        std::uniform_int_distribution<int> dist(minV, maxV);
        return dist(Engine());
    }

    // 確率 p (0〜1) で true
    inline bool Chance(float p){ return Float(0.0f, 1.0f) < p; }

    // 各成分が [min, max] の Vector3
    inline Vector3 Vec3(float minV, float maxV){
        return { Float(minV, maxV), Float(minV, maxV), Float(minV, maxV) };
    }
    inline Vector3 Vec3(const Vector3& minV, const Vector3& maxV){
        return { Float(minV.x, maxV.x), Float(minV.y, maxV.y), Float(minV.z, maxV.z) };
    }

    // 半径 radius の球の内部にランダムな点（一様分布）
    inline Vector3 InSphere(float radius){
        for ( int i = 0; i < 32; ++i ) {
            Vector3 p{ Float(-1.0f, 1.0f), Float(-1.0f, 1.0f), Float(-1.0f, 1.0f) };
            if ( p.x * p.x + p.y * p.y + p.z * p.z <= 1.0f ) {
                return { p.x * radius, p.y * radius, p.z * radius };
            }
        }
        return { 0.0f, 0.0f, 0.0f };
    }

    // 単位方向ベクトル（球面上の一様ランダム）
    inline Vector3 Direction(){
        Vector3 d = InSphere(1.0f);
        float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if ( len < 1e-6f ) return { 0.0f, 1.0f, 0.0f };
        return { d.x / len, d.y / len, d.z / len };
    }
}
