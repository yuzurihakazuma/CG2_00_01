#pragma once

#include "engine/math/struct.h"
#include "game/enemy/Boss.h"

namespace BossVisualColor {

inline bool IsSplitRanged(const Boss* boss) {
	return boss &&
		boss->IsSplitBehaviorEnabled() &&
		boss->GetCombatRole() == Boss::CombatRole::Ranged;
}

inline bool IsSplitMelee(const Boss* boss) {
	return boss &&
		boss->IsSplitBehaviorEnabled() &&
		boss->GetCombatRole() == Boss::CombatRole::Melee;
}

inline Vector4 Primary(const Boss* boss, float alpha = 1.0f) {
	if (IsSplitRanged(boss)) {
		return { 0.10f, 0.55f, 1.0f, alpha };
	}
	if (IsSplitMelee(boss)) {
		return { 1.0f, 0.10f, 0.12f, alpha };
	}
	return { 0.70f, 0.20f, 1.0f, alpha };
}

inline Vector4 Secondary(const Boss* boss, float alpha = 1.0f) {
	if (IsSplitRanged(boss)) {
		return { 0.45f, 0.85f, 1.0f, alpha };
	}
	if (IsSplitMelee(boss)) {
		return { 1.0f, 0.34f, 0.12f, alpha };
	}
	return { 0.92f, 0.42f, 1.0f, alpha };
}

inline Vector4 BeamPrimary(const Boss* boss, float alpha = 1.0f) {
	if (boss && boss->IsSplitBehaviorEnabled()) {
		return { 0.10f, 0.55f, 1.0f, alpha };
	}
	return Primary(boss, alpha);
}

inline Vector4 BeamSecondary(const Boss* boss, float alpha = 1.0f) {
	if (boss && boss->IsSplitBehaviorEnabled()) {
		return { 0.45f, 0.85f, 1.0f, alpha };
	}
	return Secondary(boss, alpha);
}

inline Vector4 DarkSmoke(const Boss* boss, float alpha = 0.85f) {
	if (IsSplitRanged(boss)) {
		return { 0.03f, 0.08f, 0.24f, alpha };
	}
	if (IsSplitMelee(boss)) {
		return { 0.28f, 0.02f, 0.03f, alpha };
	}
	return { 0.18f, 0.02f, 0.26f, alpha };
}

}
