#include "game/combat/CombatSystem.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "game/combat/HitFeel.h"
#include "engine/audio/AudioManager.h"
#include "engine/math/VectorMath.h"

using namespace VectorMath;

void CombatSystem::Initialize(uint32_t circleTexSrv, uint32_t envTexSrv){
    circleTex_ = circleTexSrv;
    envTex_    = envTexSrv;
    stompEffects_.clear();
}

void CombatSystem::SpawnStompEffect(const Vector3& pos, Camera* camera, StompEffectType type){
    auto fx = std::make_unique<StompEffect>();
    fx->Initialize(pos, camera, circleTex_, envTex_, type);
    stompEffects_.push_back(std::move(fx));
}

void CombatSystem::Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel, Camera* camera){
    // --- 踏みつけ：空中から敵の上に接触 → 倒して跳ね返る ---
    Vector3 playerPos = player.GetPosition();
    const float playerRadius = 0.5f; // プレイヤーの球体当たり判定半径

    for ( auto& enemy : enemies.GetEnemies() ) {
        if ( !enemy->IsAlive() ) continue;

        Vector3 enemyPos = enemy->GetPosition();
        float enemyRadius = enemy->GetRadius();
        float dist = Length(playerPos - enemyPos);
        if ( dist >= ( playerRadius + enemyRadius ) ) continue; // 接触してなければスキップ

        // 踏みつけ成立：1.接地していない（空中）かつ 2.プレイヤーが敵より上
        if ( !player.IsGrounded() && playerPos.y > enemyPos.y + 0.1f ) {
            enemy->Defeat();
            player.Bounce();
            hitFeel.Trigger(0.06f, 0.28f);      // 一瞬停止＋カメラ揺れ
            hitFeel.TriggerImpactFx(enemyPos);  // 踏んだ点中心のポストエフェクト起動
            SpawnStompEffect(enemyPos, camera, StompEffectType::Stomp);
        }
    }

    // --- 吐き出した敵ボール × 敵：ぶつけて倒す（弾も消える）---
    eggs.ResolveSpitHits([&](const Vector3& ballPos, float ballR) -> bool {
        for ( auto& e : enemies.GetEnemies() ) {
            if ( !e->IsAlive() ) continue;
            if ( Length(ballPos - e->GetPosition()) <= ballR + e->GetRadius() ) {
                Vector3 ep = e->GetPosition();
                e->Defeat();
                hitFeel.Trigger(0.05f, 0.2f); // 命中の手応え
                SpawnStompEffect(ep, camera, StompEffectType::EggHit);
                AudioManager::GetInstance()->PlayWave("resources/se/eggHit.wav", false, 0.7f);
                return true; // 弾も消える
            }
        }
        return false;
    });

    // --- 飛行中の卵 × 敵：当たったら敵を倒して卵を割る（割れ演出は卵の Update が出す）---
    eggs.ResolveHits([&](const Vector3& eggPos, float eggR) -> bool {
        for ( auto& e : enemies.GetEnemies() ) {
            if ( !e->IsAlive() ) continue;
            float reach = eggR + e->GetRadius();
            if ( Length(eggPos - e->GetPosition()) <= reach ) {
                Vector3 ep = e->GetPosition();
                e->Defeat();
                hitFeel.Trigger(0.05f, 0.2f); // 命中の手応え
                eggs.SpawnHitFx(ep);          // 黄＆オレンジが鋭く飛び散る（踏みつけのリングとは別物）
                SpawnStompEffect(ep, camera, StompEffectType::EggHit); // 立体の着弾エフェクト
                AudioManager::GetInstance()->PlayWave("resources/se/eggHit.wav", false, 0.7f); // 命中音
                return true; // 命中（殻の緑＋黄身は卵の割れ演出が別に出す）
            }
        }
        return false;
    });
}

void CombatSystem::UpdateEffects(float dt){
    for ( auto& effect : stompEffects_ ) { effect->Update(dt); }
    stompEffects_.remove_if([](const std::unique_ptr<StompEffect>& e){ return e->IsDead(); });
}

void CombatSystem::Draw(){
    for ( auto& effect : stompEffects_ ) { effect->Draw(); }
}
