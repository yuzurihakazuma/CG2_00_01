#include "game/egg/EggSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include <algorithm>

void EggSystem::Initialize(){
    eggs_.clear();
}

// 敵を飲み込んだ → プレイヤー位置に卵が生まれる（実体つき・Held 状態）。お腹一杯なら false。
bool EggSystem::OnSwallow(const Vector3& birthPos){
    if ( HeldCount() >= kMaxEggs ) return false; // お腹が一杯

    auto egg = std::make_unique<Egg>(birthPos);

    // 実体（見た目）：球モデルを卵っぽく縦長＋淡い緑にする。
    const Vector3 baseScale = { 0.35f, 0.45f, 0.35f };
    if ( auto obj = Obj3d::Create("sphere") ) { // 既定カメラが自動でバインドされる
        obj->SetScale(baseScale);
        if ( obj->GetModel() && obj->GetModel()->GetMaterial() ) {
            obj->GetModel()->GetMaterial()->color = { 0.6f, 1.0f, 0.6f, 1.0f };
        }
        egg->AttachVisual(std::move(obj), baseScale);
    }
    eggs_.push_back(std::move(egg));
    return true;
}

void EggSystem::Update(const Vector3& playerPos, const Vector3& facing, float dt){
    // 保持中の卵に「後ろのスロット」を割り当てる（増えるほど後ろへ一列に並ぶ）。
    //   後ろ = facing の逆方向。i 番目ほど遠くに置く → 各卵が自分でそこへ寄っていく（整列＆追従）。
    int held = 0;
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        Vector3 slot = {
            playerPos.x - facing.x * ( 0.8f + held * 0.6f ),
            playerPos.y + 0.3f,
            playerPos.z - facing.z * ( 0.8f + held * 0.6f )
        };
        e->SetTarget(slot);
        ++held;
    }

    // 各卵の状態と見た目を進める
    for ( auto& e : eggs_ ) { e->Update(dt); }

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const std::unique_ptr<Egg>& e){ return e->IsDead(); }),
        eggs_.end());
}

void EggSystem::Draw() const{
    for ( const auto& e : eggs_ ) { e->Draw(); }
}

// 保持中の一番古い卵を前方へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& facing){
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        e->SetPosition({ playerPos.x, playerPos.y + 0.5f, playerPos.z });
        e->Throw(facing, 12.0f);
        return true;
    }
    return false;
}

int EggSystem::HeldCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e->IsHeld() ) ++n; }
    return n;
}

int EggSystem::FlyingCount() const{
    int n = 0;
    for ( const auto& e : eggs_ ) { if ( e->IsFlying() ) ++n; }
    return n;
}
