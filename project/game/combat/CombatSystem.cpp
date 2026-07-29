#include "game/combat/CombatSystem.h"

#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "game/egg/EggSystem.h"
#include "game/combat/HitFeel.h"
#include "engine/audio/AudioManager.h"
#include "engine/math/VectorMath.h"
#include "engine/sdf/SDFVolumeObject.h"

#include <algorithm>

using namespace VectorMath;

namespace {
    // --- 敵のSDF消滅演出の定数 ---
    const int   kDissolvePool  = 3;     // 同時に溶かせる敵の数（超えたら一番古いのを使い回す）
    const float kDissolveTime  = 0.45f; // 溶けきるまでの秒数
    const float kDissolveFade  = 0.35f; // この割合を過ぎたら透明化も並行（0〜1）
    const Vector4 kDissolveColor = { 0.9f, 0.35f, 0.3f, 1.0f }; // カラーボリュームが無い時の予備色
}

CombatSystem::CombatSystem() = default;
CombatSystem::~CombatSystem() = default;

void CombatSystem::Initialize(uint32_t circleTexSrv, uint32_t envTexSrv){
    circleTex_ = circleTexSrv;
    envTex_    = envTexSrv;
    stompEffects_.clear();
}

// 敵のSDF消滅演出のセットアップ（enemyBall.sdf3d＋カラーボリュームをプールぶん先読み）
void CombatSystem::InitializeDissolveFx(ID3D12GraphicsCommandList* commandList){
    dissolves_.clear();
    for ( int i = 0; i < kDissolvePool; ++i ) {
        DissolveFx dissolveFx;
        dissolveFx.vol = std::make_unique<SDFVolumeObject>();
        if ( !dissolveFx.vol->Load("resources/sdf3d/enemyBall.sdf3d", commandList) ) {
            dissolves_.clear(); // 読めなければ演出なし（従来のポン消えに自動フォールバック）
            return;
        }
        dissolveFx.vol->SetColor(kDissolveColor);
        dissolves_.push_back(std::move(dissolveFx));
    }
}

// エフェクトの事前生成：ロード画面のうちに Obj3d 一式（専用モデル4種の登録込み）を作ってプールへ。
//   プールにあるものは Update/Draw の対象外なので、位置はどこでもよい
void CombatSystem::Prewarm(Camera* camera, int count){
    while ( ( int ) stompPool_.size() < count ) {
        auto stompEffect = std::make_unique<StompEffect>();
        stompEffect->Initialize({ 0.0f, -10000.0f, 0.0f }, camera, circleTex_, envTex_, StompEffectType::Stomp);
        stompPool_.push_back(std::move(stompEffect));
    }
}

void CombatSystem::SpawnStompEffect(const Vector3& pos, Camera* camera, StompEffectType type){
    // プールに使い終わったものがあれば使い回す（Initialize は Obj3d を作り直さず設定だけリセットする）
    std::unique_ptr<StompEffect> stompEffect;
    if ( !stompPool_.empty() ) {
        stompEffect = std::move(stompPool_.back());
        stompPool_.pop_back();
    } else {
        stompEffect = std::make_unique<StompEffect>();
    }
    stompEffect->Initialize(pos, camera, circleTex_, envTex_, type);
    stompEffects_.push_back(std::move(stompEffect));
}

// 倒れた敵の位置でSDFボールを溶かし始める
void CombatSystem::SpawnEnemyDissolve(const Vector3& pos, float radius){
    if ( dissolves_.empty() ) return;
    // 未使用スロット優先。無ければ一番進んだ（=一番古い）演出を使い回す
    DissolveFx* slot = nullptr;
    for ( auto& dissolve : dissolves_ ) {
        if ( dissolve.timer < 0.0f ) { slot = &dissolve; break; }
    }
    if ( !slot ) {
        slot = &dissolves_[0];
        for ( auto& dissolve : dissolves_ ) { if ( dissolve.timer > slot->timer ) slot = &dissolve; }
    }
    slot->timer = 0.0f;
    slot->scale = radius * 2.0f; // 敵の見た目（直径）と同じ大きさで重ねる
    slot->vol->SetScale(slot->scale);
    slot->vol->SetTranslation(pos);
    slot->vol->SetErode(0.0f);
    slot->vol->SetColor(kDissolveColor);
    slot->vol->Update();
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
            SpawnEnemyDissolve(enemyPos, enemy->GetRadius()); // 敵はSDFで芯まで溶けて消える
        }
    }

    // --- 吐き出した敵ボール × 敵：ぶつけて倒す（弾も消える）---
    eggs.ResolveSpitHits([&](const Vector3& ballPos, float ballRadius) -> bool {
        for ( auto& enemy : enemies.GetEnemies() ) {
            if ( !enemy->IsAlive() ) continue;
            if ( Length(ballPos - enemy->GetPosition()) <= ballRadius + enemy->GetRadius() ) {
                Vector3 enemyPos = enemy->GetPosition();
                float enemyRadius = enemy->GetRadius();
                enemy->Defeat();
                hitFeel.Trigger(0.05f, 0.2f); // 命中の手応え
                SpawnStompEffect(enemyPos, camera, StompEffectType::EggHit);
                SpawnEnemyDissolve(enemyPos, enemyRadius);   // 敵はSDFで溶けて消える
                AudioManager::GetInstance()->PlayWave("resources/se/eggHit.wav", false, 0.7f);
                return true; // 弾も消える
            }
        }
        return false;
    });

    // --- 飛行中の卵 × 敵：当たったら敵を倒して卵を割る（割れ演出は卵の Update が出す）---
    eggs.ResolveHits([&](const Vector3& eggPos, float eggRadius) -> bool {
        for ( auto& enemy : enemies.GetEnemies() ) {
            if ( !enemy->IsAlive() ) continue;
            float reach = eggRadius + enemy->GetRadius();
            if ( Length(eggPos - enemy->GetPosition()) <= reach ) {
                Vector3 enemyPos = enemy->GetPosition();
                float enemyRadius = enemy->GetRadius();
                enemy->Defeat();
                hitFeel.Trigger(0.05f, 0.2f); // 命中の手応え
                eggs.SpawnHitFx(enemyPos);          // 黄＆オレンジが鋭く飛び散る（踏みつけのリングとは別物）
                SpawnStompEffect(enemyPos, camera, StompEffectType::EggHit); // 立体の着弾エフェクト
                SpawnEnemyDissolve(enemyPos, enemyRadius);   // 敵はSDFで溶けて消える
                AudioManager::GetInstance()->PlayWave("resources/se/eggHit.wav", false, 0.7f); // 命中音
                return true; // 命中（殻の緑＋黄身は卵の割れ演出が別に出す）
            }
        }
        return false;
    });
}

void CombatSystem::UpdateEffects(float dt){
    for ( auto& effect : stompEffects_ ) { effect->Update(dt); }
    // 消滅したエフェクトは破棄せずプールへ戻す（次の発生で使い回す）
    for ( auto it = stompEffects_.begin(); it != stompEffects_.end(); ) {
        if ( ( *it )->IsDead() ) {
            if ( stompPool_.size() < 8 ) { stompPool_.push_back(std::move(*it)); }
            it = stompEffects_.erase(it);
        } else {
            ++it;
        }
    }

    // --- 敵のSDF消滅：エロージョンを進めて芯まで溶かす（終盤は透明化も並行）---
    for ( auto& dissolve : dissolves_ ) {
        if ( dissolve.timer < 0.0f ) continue;
        dissolve.timer += dt;
        float t = dissolve.timer / kDissolveTime;
        if ( t >= 1.0f ) { dissolve.timer = -1.0f; continue; } // 溶けきった→スロットを空ける

        float melt = t * t; // 2次イン（最初はゆっくり崩れ、後半一気に消える）
        dissolve.vol->SetErode(0.5f * dissolve.scale * melt); // 0.5*scale で完全消滅（birthFx と同じ換算）
        Vector4 color = kDissolveColor;
        color.w = 1.0f - std::clamp(( t - kDissolveFade ) / ( 1.0f - kDissolveFade ), 0.0f, 1.0f);
        dissolve.vol->SetColor(color);
        dissolve.vol->Update(); // カメラ行列＋CBを毎フレーム焼き直す
    }
}

void CombatSystem::Draw(){
    for ( auto& effect : stompEffects_ ) { effect->Draw(); }
}

// SDF消滅演出の描画（専用PSOのためMRTパスの最後で呼ばれる）
void CombatSystem::DrawDissolveFx(ID3D12GraphicsCommandList* commandList){
    for ( auto& dissolve : dissolves_ ) {
        if ( dissolve.timer >= 0.0f && dissolve.vol ) { dissolve.vol->Draw(commandList); }
    }
}

void CombatSystem::ClearEffects(){
    // 表示中のものもプールへ戻してから空にする（破棄→再生成のコストを避ける）
    for ( auto& effect : stompEffects_ ) {
        if ( stompPool_.size() < 8 ) { stompPool_.push_back(std::move(effect)); }
    }
    stompEffects_.clear();
    for ( auto& dissolve : dissolves_ ) { dissolve.timer = -1.0f; } // 溶かし中の演出も止める
}
