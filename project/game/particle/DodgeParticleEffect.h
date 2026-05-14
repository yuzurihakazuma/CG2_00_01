#pragma once
#include <memory>
#include "engine/math/VectorMath.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "engine/particle/GPUParticleEmitterData.h"

// 回避時のパーティクル演出を管理する専用クラス
class DodgeParticleEffect{
public:
    // 初期化（ここで色や数を設定する）
    void Initialize();

    // 回避スタート時の「ドカン！」を出す
    void EmitBurst(const Vector3& pos);

    // 転がっている最中の「キラキラ軌跡」を出す
    void EmitTrail(const Vector3& pos, const Vector3& dodgeDirection);

private:
    std::unique_ptr<GPUParticleEmitter> burstEmitter_ = nullptr; // 爆発用
    std::unique_ptr<GPUParticleEmitter> trailEmitter_ = nullptr; // 軌跡用

    std::unique_ptr<GPUParticleEmitter> coreEmitter_ = nullptr;
};