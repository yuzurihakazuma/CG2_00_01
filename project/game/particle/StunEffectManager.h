#pragma once
#include "engine/math/VectorMath.h"

// スタン演出だけを請け負う専門家クラス
class StunEffectManager{
public:
    // この関数を呼ぶだけで、星とビリビリと姿勢制御が全部終わる
    static void Update(Vector3& pos, Vector3& rot, int stunTimer, float headHeight = 2.0f);
};