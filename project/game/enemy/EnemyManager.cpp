#include "game/enemy/EnemyManager.h"

#include "engine/rail/SplineRail.h"
#include <algorithm>

// 配置テンプレートから敵を再構築する
void EnemyManager::Spawn(const std::vector<EnemySpawnData>& spawns, const std::vector<SplineRail>& rails){
    enemies_.clear();
    for ( const auto& spawn : spawns ) {
        if ( spawn.railIndex < 0 || spawn.railIndex >= ( int ) rails.size() ) continue;
        if ( rails[spawn.railIndex].nodes.size() < 2 ) continue;

        auto enemy = std::make_unique<Enemy>();
        enemy->Initialize(spawn.type, spawn.railIndex, spawn.distance, spawn.patrol);
        // dt=0 で Update を呼び、レール上の初期位置を即座に確定させる（原点に巨大球が出るのを防ぐ）
        enemy->Update(rails, 0.0f);
        enemies_.push_back(std::move(enemy));
    }
}

// 移動＋吸い込みTick＋消化（完了で onConsumed を呼んで削除）
void EnemyManager::Update(const std::vector<SplineRail>& rails, const Vector3& playerPos, float dt,
                          const std::function<void(const Vector3&)>& onConsumed){
    for ( auto& e : enemies_ ) {
        if ( e->IsSwallowing() ) { e->TickSwallow(playerPos, dt); } // 縮みながらプレイヤーへ吸い込まれる
        else { e->Update(rails, dt); }
    }
    // 吸い込み完了 → 通知（お腹+1・演出はシーン側）してから消す
    for ( auto& e : enemies_ ) {
        if ( e->IsConsumed() && onConsumed ) { onConsumed(e->GetPosition()); }
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
        [](const std::unique_ptr<Enemy>& e){ return e->IsConsumed(); }), enemies_.end());
}

void EnemyManager::Draw(){
    for ( auto& e : enemies_ ) { e->Draw(); }
}
