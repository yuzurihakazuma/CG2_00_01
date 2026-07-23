#include "CoinSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/rail/SplineRail.h"
#include <algorithm>
#include <cmath>

namespace {
    const float kCoinRadius  = 0.9f;  // 取得判定の半径(m)
    const float kSpinSpeed   = 2.5f;  // 回転速度(rad/s)
    const float kBobAmount   = 0.08f; // 上下のふわふわ(m)
}

void CoinSystem::Sync(const std::vector<CoinData>& coinDatas, const std::vector<SplineRail>& rails){
    coins_.clear();
    collected_ = 0;
    for ( const auto& data : coinDatas ) {
        if ( data.rail < 0 || data.rail >= ( int ) rails.size() ) continue;
        const SplineRail& rail = rails[data.rail];
        if ( rail.GetLength() <= 0.0f ) continue;
        Coin coin;
        Vector3 p = rail.GetPositionByDistance(std::clamp(data.dist, 0.0f, rail.GetLength()));
        coin.pos = { p.x, p.y + data.height, p.z };
        coin.obj = Obj3d::Create("coin");
        if ( coin.obj ) {
            coin.obj->SetTranslation(coin.pos);
            coin.obj->SetScale({ 0.32f, 0.32f, 0.10f }); // 平たい円盤＝コインらしく
            coin.obj->Update();
        }
        coins_.push_back(std::move(coin));
    }
}

void CoinSystem::ResetPlay(){
    collected_ = 0;
    for ( auto& coin : coins_ ) { coin.taken = false; }
}

void CoinSystem::Update(const Vector3& playerPos, bool playMode, float dt){
    spinT_ += dt;
    for ( auto& coin : coins_ ) {
        if ( coin.taken || !coin.obj ) continue;
        // くるくる回転＋ふわふわ上下（拾えるものだと一目で分かる動き）
        coin.obj->SetRotation({ 0.0f, spinT_ * kSpinSpeed, 0.0f });
        coin.obj->SetTranslation({ coin.pos.x,
                                   coin.pos.y + std::sin(spinT_ * 2.0f + coin.pos.x) * kBobAmount,
                                   coin.pos.z });
        coin.obj->Update();
        if ( playMode ) {
            float dx = playerPos.x - coin.pos.x;
            float dy = playerPos.y - coin.pos.y;
            float dz = playerPos.z - coin.pos.z;
            if ( dx * dx + dy * dy + dz * dz < kCoinRadius * kCoinRadius ) {
                coin.taken = true;
                ++collected_;
            }
        }
    }
}

void CoinSystem::Draw() const{
    for ( const auto& coin : coins_ ) {
        if ( !coin.taken && coin.obj ) { coin.obj->Draw(); }
    }
}
