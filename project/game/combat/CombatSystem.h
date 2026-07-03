#pragma once
#include "engine/math/struct.h"
#include "StompEffect.h"
#include <list>
#include <memory>
#include <cstdint>

class Player;
class EnemyManager;
class EggSystem;
class HitFeel;
class Camera;

// =====================================================================
//  CombatSystem：プレイヤーと敵の戦闘判定＋ヒット演出（StompEffect）の管理。
//   ・踏みつけ：空中から敵の上に接触 → 敵を倒して跳ね返る＋演出一式
//   ・卵の命中：飛行中の卵 × 敵 → 敵を倒して卵を割る＋演出
//  シーンから分離した理由：判定と演出生成が対になった独立の関心事で、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class CombatSystem {
public:
    // エフェクト生成に使うテクスチャ（circle / 環境マップ）の SRV インデックスを受け取る
    void Initialize(uint32_t circleTexSrv, uint32_t envTexSrv);

    // 踏みつけ＋卵vs敵の当たり判定（Play中に毎フレーム呼ぶ）
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel, Camera* camera);

    void UpdateEffects(float dt); // StompEffect の進行（モード問わず毎フレーム）
    void Draw();                  // StompEffect の描画
    void ClearEffects(){ stompEffects_.clear(); } // モード切替時など

private:
    void SpawnStompEffect(const Vector3& pos, Camera* camera, StompEffectType type);

    std::list<std::unique_ptr<StompEffect>> stompEffects_;
    uint32_t circleTex_ = 0;
    uint32_t envTex_    = 0;
};
