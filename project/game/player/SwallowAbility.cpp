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
void SwallowAbility::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel, float dt){
    Input* input = Input::GetInstance();
    Vector3 playerPos = player.GetPosition();

    // クールタイムの経過（成功した時だけ充填される）
    if ( cooldownTimer_ > 0.0f ) { cooldownTimer_ -= dt; }

    // --- E：飲み込み開始（縮小吸い込みアニメ。卵にはまだしない）---
    //   成立条件：クールタイム明け ＆ 届く距離 ＆ プレイヤーの「前方」にいる敵だけ。
    //   （以前は全方位の最近傍だったため、背後の敵まで吸えて違和感があった）
    if ( input->Triggerkey(DIK_E) && cooldownTimer_ <= 0.0f ) {
        float yaw = player.GetRotation().y;
        Vector3 facing = { std::sin(yaw), 0.0f, std::cos(yaw) }; // 向いている水平方向

        Enemy* target = nullptr;
        float bestDist = swallowReach_; // 舌の届く範囲（ノードエディタから調整可）
        for ( auto& e : enemies.GetEnemies() ) {
            if ( !e->IsAlive() ) continue;
            Vector3 to = e->GetPosition() - playerPos;
            float d = Length(to);
            if ( d >= bestDist ) continue;

            // 前方チェック：水平方向の内積で「ほぼ横〜後ろ」を弾く（cos≒0.2 → 前方約±78°）
            float horiz = std::sqrt(to.x * to.x + to.z * to.z);
            if ( horiz > 1e-4f && ( to.x * facing.x + to.z * facing.z ) / horiz < 0.2f ) continue;

            bestDist = d; target = e.get();
        }
        if ( target ) {
            target->StartSwallow();          // 縮みながらプレイヤーへ（完了で EnemyManager がお腹+1へ通知）
            hitFeel.Trigger(0.03f, 0.1f);    // 軽い手応え
            cooldownTimer_ = swallowCooldown_; // 成功した時だけクールタイム開始（空振りは即再入力OK）
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
