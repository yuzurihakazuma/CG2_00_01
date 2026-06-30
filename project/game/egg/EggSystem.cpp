#include "game/egg/EggSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include "engine/particle/ParticleManager.h" // 煙・星のパーティクル
#include <algorithm>

// パーティクルのグループ名（実体は GamePlayScene::LoadResources で生成・設定）。
namespace {
    const char* kSmokeGroup = "EggSmoke"; // 飛行中の煙トレイル
    const char* kStarGroup  = "EggStar";  // 投げ・着弾の星（ぱっと弾ける）
}

void EggSystem::Initialize(){
    eggs_.clear();
}

// 敵を飲み込んだ → プレイヤー位置に卵が生まれる（実体つき・Held 状態）。
//   お腹が一杯なら「一番古い保持卵を捨てて(割って)」から新しい卵を作る（常に飲み込める）。
bool EggSystem::OnSwallow(const Vector3& birthPos){
    if ( HeldCount() >= kMaxEggs ) {
        for ( auto& e : eggs_ ) {        // 先頭から順＝一番古い保持卵を探して捨てる
            if ( e->IsHeld() ) { e->Break(); break; }
        }
    }

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

    // 各卵の状態と見た目を進める＋飛行トレイル／割れた瞬間の星
    ParticleManager* pm = ParticleManager::GetInstance();
    for ( auto& e : eggs_ ) {
        if ( e->IsFlying() ) {
            pm->Emit(kSmokeGroup, e->GetPosition(), 2); // 飛んでる間は煙をもくもく
        }
        e->Update(dt);
        if ( e->JustBroke() ) {
            pm->Emit(kStarGroup, e->GetPosition(), 18); // 着弾/時間切れで星がぱっと弾ける
            e->ClearJustBroke();
        }
    }

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const std::unique_ptr<Egg>& e){ return e->IsDead(); }),
        eggs_.end());
}

void EggSystem::Draw() const{
    for ( const auto& e : eggs_ ) { e->Draw(); }
}

// 保持中の一番古い卵を指定方向へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& dir, float speed){
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        Vector3 from = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
        e->SetPosition(from);
        e->Throw(dir, speed);
        ParticleManager::GetInstance()->Emit(kStarGroup, from, 8); // 「ぽいっ」と投げる時の星
        return true;
    }
    return false;
}

// 飛行中の卵を当たり判定にかけ、当たった卵を割る（敵側の処理は onHit 内でシーンが行う）。
void EggSystem::ResolveHits(const std::function<bool(const Vector3&, float)>& onHit){
    for ( auto& e : eggs_ ) {
        if ( !e->IsFlying() ) continue;
        if ( onHit(e->GetPosition(), e->GetRadius()) ) {
            e->Break(); // 命中 → 割れる（星は次の Update が JustBroke を拾って出す）
        }
    }
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
