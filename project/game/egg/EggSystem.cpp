#include "game/egg/EggSystem.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/3d/model/Model.h"
#include <algorithm>
#include <cstdlib>

EggSystem::~EggSystem() = default; // unique_ptr<Obj3d> のため cpp 側で定義

namespace {
    // -1.0〜1.0 の簡易乱数（トレイルのばらつき用）
    float Rand11(){ return ( ( float ) std::rand() / RAND_MAX ) * 2.0f - 1.0f; }
}

void EggSystem::Initialize(){
    eggs_.clear();
    puffs_.clear();
    stomach_    = 0;
    trailTimer_ = 0.0f;
}

// 煙パフ（実体の小球）を1個出す。縮みながら消えるので加算でなくても確実に見える。
void EggSystem::SpawnPuff(const Vector3& pos, const Vector3& vel, const Vector4& color, float scale, float life){
    TrailPuff p;
    p.obj = Obj3d::Create("sphere"); // 既定カメラが自動バインド
    if ( p.obj ) {
        p.obj->SetScale({ scale, scale, scale });
        if ( p.obj->GetModel() && p.obj->GetModel()->GetMaterial() ) {
            p.obj->GetModel()->GetMaterial()->color = color;
        }
        p.obj->SetTranslation(pos);
        p.obj->Update();
    }
    p.pos = pos; p.vel = vel; p.life = life; p.maxLife = life; p.baseScale = scale;
    puffs_.push_back(std::move(p));
}

// 敵を飲み込んだ → お腹に1匹ためる（卵にするのは LayEgg）。
void EggSystem::AddToBelly(){
    if ( stomach_ < kMaxEggs ) { ++stomach_; }
}

// しゃがみ等で産む：お腹を1減らして、birthPos に卵(Held)を1個生む。
//   持っている卵が満杯なら一番古い卵を割って捨ててから生む。
bool EggSystem::LayEgg(const Vector3& birthPos){
    if ( stomach_ <= 0 ) return false;
    --stomach_;

    if ( HeldCount() >= kMaxEggs ) {
        for ( auto& e : eggs_ ) {        // 先頭から順＝一番古い保持卵を捨てる
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

    // 飛行トレイル：一定間隔で、飛んでいる卵の位置に煙パフ（白い小球）を置く。
    trailTimer_ += dt;
    bool emitTrail = false;
    if ( trailTimer_ >= 0.03f ) { trailTimer_ -= 0.03f; emitTrail = true; }

    // 各卵の状態と見た目を進める＋トレイル／割れた瞬間の殻飛び散り
    for ( auto& e : eggs_ ) {
        if ( e->IsFlying() && emitTrail ) {
            Vector3 p = e->GetPosition();
            Vector3 jitter = { Rand11() * 0.1f, Rand11() * 0.1f, Rand11() * 0.1f };
            SpawnPuff({ p.x + jitter.x, p.y + jitter.y, p.z + jitter.z },
                      { Rand11() * 0.4f, 0.4f + Rand11() * 0.2f, Rand11() * 0.4f },
                      { 0.95f, 0.97f, 1.0f, 1.0f }, 0.28f, 0.4f); // 白い煙
        }
        e->Update(dt);
        if ( e->JustBroke() ) { // 着弾／時間切れ：殻が黄＋緑に飛び散る
            Vector3 p = e->GetPosition();
            for ( int i = 0; i < 10; ++i ) {
                Vector4 col = ( i % 2 ) ? Vector4{ 1.0f, 0.9f, 0.3f, 1.0f }   // 黄身
                                        : Vector4{ 0.5f, 1.0f, 0.5f, 1.0f };  // 殻の緑
                SpawnPuff(p, { Rand11() * 3.5f, 2.0f + Rand11() * 2.0f, Rand11() * 3.5f }, col, 0.22f, 0.4f);
            }
            e->ClearJustBroke();
        }
    }

    // 煙パフの更新（移動＋軽い重力＋縮小、寿命切れで削除）
    for ( auto& p : puffs_ ) {
        p.life   -= dt;
        p.vel.y  -= 3.0f * dt;
        p.pos.x  += p.vel.x * dt;
        p.pos.y  += p.vel.y * dt;
        p.pos.z  += p.vel.z * dt;
        float r = ( p.maxLife > 0.0f ) ? ( p.life / p.maxLife ) : 0.0f;
        if ( r < 0.0f ) r = 0.0f;
        float s = p.baseScale * r;
        if ( p.obj ) {
            p.obj->SetTranslation(p.pos);
            p.obj->SetScale({ s, s, s });
            p.obj->Update();
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
        [](const TrailPuff& p){ return p.life <= 0.0f; }), puffs_.end());

    // 割れて消えてよくなった卵を後始末
    eggs_.erase(
        std::remove_if(eggs_.begin(), eggs_.end(),
            [](const std::unique_ptr<Egg>& e){ return e->IsDead(); }),
        eggs_.end());
}

void EggSystem::Draw() const{
    for ( const auto& e : eggs_ ) { e->Draw(); }
    for ( const auto& p : puffs_ ) { if ( p.obj && p.life > 0.0f ) p.obj->Draw(); } // 煙パフ
}

// 保持中の一番古い卵を指定方向へ投げる。
bool EggSystem::TryThrow(const Vector3& playerPos, const Vector3& dir, float speed){
    for ( auto& e : eggs_ ) {
        if ( !e->IsHeld() ) continue;
        Vector3 from = { playerPos.x, playerPos.y + 0.5f, playerPos.z };
        e->SetPosition(from);
        e->Throw(dir, speed);
        // 「ぽいっ」と投げる時の白い煙ひと吹き
        for ( int i = 0; i < 6; ++i ) {
            SpawnPuff(from, { Rand11() * 1.2f, 0.5f + Rand11() * 0.5f, Rand11() * 1.2f },
                      { 0.95f, 0.97f, 1.0f, 1.0f }, 0.25f, 0.35f);
        }
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
