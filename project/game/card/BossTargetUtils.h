#pragma once

#include <limits>

#include "engine/math/VectorMath.h"
#include "game/enemy/Boss.h"

namespace BossTargetUtils {

inline Boss* FindClosestAliveBossInRange(
    const Vector3& point,
    float radius,
    Boss* primaryBoss,
    Boss* secondaryBoss
) {
    Boss* closestBoss = nullptr;
    float closestDistance = std::numeric_limits<float>::max();

    Boss* candidates[2] = { primaryBoss, secondaryBoss };
    for (Boss* candidate : candidates) {
        if (!candidate || candidate->IsDead()) {
            continue;
        }

        Vector3 diff = {
            candidate->GetPosition().x - point.x,
            0.0f,
            candidate->GetPosition().z - point.z
        };
        float distance = VectorMath::Length(diff);
        if (distance <= radius && distance < closestDistance) {
            closestDistance = distance;
            closestBoss = candidate;
        }
    }

    return closestBoss;
}

inline void ApplyAttackDebuffToAliveBosses(int durationFrames, Boss* primaryBoss, Boss* secondaryBoss) {
    Boss* candidates[2] = { primaryBoss, secondaryBoss };
    for (Boss* candidate : candidates) {
        if (!candidate || candidate->IsDead()) {
            continue;
        }

        candidate->ApplyAttackDebuff(durationFrames);
    }
}

} // namespace BossTargetUtils
