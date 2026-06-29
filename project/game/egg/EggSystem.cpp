#include "game/egg/EggSystem.h"
#include <algorithm>

void EggSystem::Initialize(){
    eggs_.clear();
}

// 敵を飲み込んだ → Held 状態の卵を1個追加。
void EggSystem::OnSwallow(const Vector3& atPos){
    eggs_.emplace_back(atPos); // Egg のコンストラクタで Held 状態になる
}

void EggSystem::Update(const Vector3& playerPos, const Vector3& facing, float dt){
    // 各卵の状態を進める（Held=待機 / Flying=移動 / Broken=消滅猶予）
    for ( auto& e : eggs_ ) { e.Update(dt); }

    // 保持中の卵はヨッシーの「後ろ」に並べて待機させる（追従）。
    //   後ろ = facing の逆方向へ少し / 複数は少し高さをずらす（重ならないように）。
    int heldIndex = 0;
    for ( auto& e : eggs_ ) {
        if ( !e.IsHeld() ) continue;
        Vector3 behind = {
            playerPos.x - facing.x * 1.0f,
            playerPos.y + 0.5f + heldIndex * 0.35f,
            playerPos.z - facing.z * 1.0f
        };
        e.SetPosition(behind);
        ++heldIndex;
    }

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const Egg& e){ return e.IsDead(); }),
        eggs_.end());
}

// 保持中の一番古い卵を前方へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& facing){
    for ( auto& e : eggs_ ) {
        if ( !e.IsHeld() ) continue;
        e.SetPosition({ playerPos.x, playerPos.y + 0.5f, playerPos.z });
        e.Throw(facing, 12.0f); // 前方へ初速12m/s
        return true;
    }
    return false; // 保持中の卵が無い
}

int EggSystem::HeldCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e.IsHeld() ) ++n; }
    return n;
}

int EggSystem::FlyingCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e.IsFlying() ) ++n; }
    return n;
}
