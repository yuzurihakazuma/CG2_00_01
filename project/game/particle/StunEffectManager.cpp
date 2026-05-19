#include "StunEffectManager.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <random>

void StunEffectManager::Update(Vector3& pos, Vector3& rot, int stunTimer){
    if ( stunTimer <= 0 ) return;

    // 姿勢をうなだれる（ここで rot を直接書き換える）
    rot.x = 0.5f;

    // ① 星
    float angle = static_cast< float >( stunTimer ) * 0.15f;
    float radius = 0.8f;
    float headHeight = 2.0f;

    Vector3 starPos1 = { pos.x + std::cosf(angle) * radius, pos.y + headHeight, pos.z + std::sinf(angle) * radius };
    Vector3 starPos2 = { pos.x + std::cosf(angle + 3.14159f) * radius, pos.y + headHeight, pos.z + std::sinf(angle + 3.14159f) * radius };

    GPUParticleManager::GetInstance()->Emit(starPos1, { 0,0,0 }, 0.15f, 0.3f, { 1.0f, 0.9f, 0.2f, 1.0f });
    GPUParticleManager::GetInstance()->Emit(starPos2, { 0,0,0 }, 0.15f, 0.3f, { 1.0f, 0.9f, 0.2f, 1.0f });

    // ② ビリビリ
    if ( stunTimer % 2 == 0 ) {
        Vector3 sparkPos = { pos.x + ( rand() % 11 - 5 ) * 0.15f, pos.y + 0.5f + ( rand() % 11 - 5 ) * 0.15f, pos.z + ( rand() % 11 - 5 ) * 0.15f };
        GPUParticleManager::GetInstance()->Emit(sparkPos, { ( float ) ( rand() % 11 - 5 ) * 0.08f, ( float ) ( rand() % 11 - 5 ) * 0.08f, ( float ) ( rand() % 11 - 5 ) * 0.08f }, 0.1f, 0.5f, { 1.0f, 1.0f, 0.5f, 1.0f });
    }
}