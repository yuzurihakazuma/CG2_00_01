#include "StunEffectManager.h"
#include "engine/particle/GPUParticleManager.h"
#include <cmath>
#include <random>

void StunEffectManager::Update(Vector3& pos, Vector3& rot, int stunTimer, float headHeight){
    if ( stunTimer <= 0 ) return;

    rot.x = 0.5f;

    // ① 星 上段（3つ・120度間隔で回転）
    float angle = static_cast< float >( stunTimer ) * 0.18f;
    float radius = 1.0f;
    for ( int i = 0; i < 3; i++ ) {
        float a = angle + ( 3.14159f * 2.0f / 3.0f ) * i;
        Vector3 starPos = {
            pos.x + std::cosf(a) * radius,
            pos.y + headHeight,
            pos.z + std::sinf(a) * radius
        };
        GPUParticleManager::GetInstance()->Emit(starPos, { 0, 0.03f, 0 }, 0.2f, 0.5f, { 1.0f, 0.95f, 0.1f, 1.0f });
    }

    // ② 星 下段（2つ・逆回転・少し低い）
    float angle2 = -static_cast< float >( stunTimer ) * 0.12f;
    for ( int i = 0; i < 2; i++ ) {
        float a = angle2 + 3.14159f * i;
        Vector3 starPos = {
            pos.x + std::cosf(a) * 0.65f,
            pos.y + headHeight - 0.5f,
            pos.z + std::sinf(a) * 0.65f
        };
        GPUParticleManager::GetInstance()->Emit(starPos, { 0, 0, 0 }, 0.15f, 0.35f, { 1.0f, 0.7f, 0.0f, 1.0f });
    }

    // ③ ビリビリ（毎フレーム3個・広範囲・速い）
    for ( int i = 0; i < 3; i++ ) {
        Vector3 sparkPos = {
            pos.x + ( rand() % 11 - 5 ) * 0.2f,
            pos.y + 0.4f + ( rand() % 11 - 5 ) * 0.2f,
            pos.z + ( rand() % 11 - 5 ) * 0.2f
        };
        GPUParticleManager::GetInstance()->Emit(
            sparkPos,
            { ( float ) ( rand() % 11 - 5 ) * 0.12f, ( float ) ( rand() % 11 - 5 ) * 0.12f, ( float ) ( rand() % 11 - 5 ) * 0.12f },
            0.12f, 0.55f, { 1.0f, 1.0f, 0.6f, 1.0f }
        );
    }

    // ④ ふわっと上昇する星くず（3フレームに1回）
    if ( stunTimer % 3 == 0 ) {
        Vector3 driftPos = {
            pos.x + ( rand() % 9 - 4 ) * 0.25f,
            pos.y + 0.5f,
            pos.z + ( rand() % 9 - 4 ) * 0.25f
        };
        GPUParticleManager::GetInstance()->Emit(
            driftPos, { 0.0f, 0.06f, 0.0f },
            0.5f, 0.4f, { 1.0f, 0.85f, 0.3f, 0.7f }
        );
    }

    // ⑤ 大きなフラッシュ（8フレームに1回・瞬き感）
    if ( stunTimer % 8 == 0 ) {
        Vector3 flashPos = { pos.x, pos.y + headHeight - 0.2f, pos.z };
        GPUParticleManager::GetInstance()->Emit(flashPos, { 0, 0, 0 }, 0.12f, 1.2f, { 1.0f, 1.0f, 0.8f, 0.9f });
    }
}