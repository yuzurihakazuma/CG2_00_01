#include "game/player/SwallowAbility.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "game/combat/HitFeel.h"
#include "engine/base/Input.h"
#include "engine/math/VectorMath.h"

#include <cmath>

using namespace VectorMath;

// E=飲み込み開始 / 左Ctrl=産卵。
//   敵を知るのは EnemyManager、卵を知るのは EggSystem。ここは入力と対象選択だけを行う。
void SwallowAbility::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel){
    Input* input = Input::GetInstance();
    Vector3 playerPos = player.GetPosition();

    // --- E：飲み込み開始（縮小吸い込みアニメ。卵にはまだしない）---
    if ( input->Triggerkey(DIK_E) ) {
        Enemy* target = nullptr;
        float bestDist = swallowReach_; // 舌の届く範囲（ノードエディタから調整可）
        for ( auto& e : enemies.GetEnemies() ) {
            if ( !e->IsAlive() ) continue;
            float d = Length(playerPos - e->GetPosition());
            if ( d < bestDist ) { bestDist = d; target = e.get(); }
        }
        if ( target ) {
            target->StartSwallow();        // 縮みながらプレイヤーへ（完了で EnemyManager がお腹+1へ通知）
            hitFeel.Trigger(0.03f, 0.1f);  // 軽い手応え
        }
    }

    // --- 左Ctrl（しゃがみ）：お腹の敵を1匹、後ろに卵として産む ---
    if ( input->Triggerkey(DIK_LCONTROL) ) {
        float yaw = player.GetRotation().y;
        Vector3 behind = { playerPos.x - std::sin(yaw) * 0.8f, playerPos.y + 0.3f, playerPos.z - std::cos(yaw) * 0.8f };
        if ( eggs.LayEgg(behind) ) {
            eggs.SpawnLayFx(behind);        // 産まれた合図（白＆緑がぽわっと）
            hitFeel.Trigger(0.03f, 0.08f);
        }
    }
}
