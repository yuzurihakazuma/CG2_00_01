#pragma once
#include "engine/math/struct.h"
#include "StompEffect.h"
#include <list>
#include <vector>
#include <memory>
#include <cstdint>
#include <d3d12.h>

class Player;
class EnemyManager;
class EggSystem;
class HitFeel;
class Camera;
class SDFVolumeObject;

// =====================================================================
//  CombatSystem：プレイヤーと敵の戦闘判定＋ヒット演出（StompEffect）の管理。
//   ・踏みつけ：空中から敵の上に接触 → 敵を倒して跳ね返る＋演出一式
//   ・卵の命中：飛行中の卵 × 敵 → 敵を倒して卵を割る＋演出
//   ・敵のSDF消滅演出：倒れた敵の位置に enemyBall.sdf3d を重ね、
//     エロージョンで「芯まで溶けて消える」（メッシュのポン消え防止）
//  シーンから分離した理由：判定と演出生成が対になった独立の関心事で、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class CombatSystem {
public:
    CombatSystem();
    ~CombatSystem(); // unique_ptr<SDFVolumeObject> のため cpp 側で定義

    // エフェクト生成に使うテクスチャ（circle / 環境マップ）の SRV インデックスを受け取る
    void Initialize(uint32_t circleTexSrv, uint32_t envTexSrv);

    // 敵のSDF消滅演出のセットアップ：enemyBall.sdf3d をプールぶん先読みする
    //   （フレーム途中のロードはデバッグレイヤーが嫌うのでシーンの LoadResources から呼ぶ）
    void InitializeDissolveFx(ID3D12GraphicsCommandList* commandList);

    // エフェクトの事前生成（シーンロード時に呼ぶ）。StompEffect 1体=Obj3d×23個
    //   （GPUリソース確保46回）なので、初撃破のフレームで作るとゲームが止まる。
    //   ここで count 体ぶん作ってプールに積んでおき、実戦では使い回すだけにする。
    //   ※ Initialize（テクスチャ受け取り）の後に呼ぶこと
    void Prewarm(Camera* camera, int count);

    // 踏みつけ＋卵vs敵の当たり判定（Play中に毎フレーム呼ぶ）
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel, Camera* camera);

    void UpdateEffects(float dt); // StompEffect＋SDF消滅の進行（モード問わず毎フレーム）
    void Draw();                  // StompEffect の描画
    // SDF消滅演出の描画（専用PSOへ切り替えるため、シーンMRTパスの最後で呼ぶこと）
    void DrawDissolveFx(ID3D12GraphicsCommandList* commandList);
    void ClearEffects();          // モード切替時など（SDF消滅も止める）

    // ★HITCH-DIAG: 撃破エフェクトを要素別に単体発火させる計測用（原因特定後に削除）
    void DebugSpawnKillFx(const Vector3& pos, Camera* camera, bool stomp, bool dissolve){
        if ( stomp )    { SpawnStompEffect(pos, camera, StompEffectType::Stomp); }
        if ( dissolve ) { SpawnEnemyDissolve(pos, 0.5f); }
    }

private:
    void SpawnStompEffect(const Vector3& pos, Camera* camera, StompEffectType type);

    // 倒れた敵の位置でSDFボールを溶かし始める（プールの空きが無ければ一番古いのを使い回す）
    void SpawnEnemyDissolve(const Vector3& pos, float radius);

    std::list<std::unique_ptr<StompEffect>> stompEffects_;
    // 使い終わったエフェクトの置き場（Obj3d×23個/体の作り直しを避ける。敵を倒した瞬間のカクつき対策）
    std::vector<std::unique_ptr<StompEffect>> stompPool_;
    uint32_t circleTex_ = 0;
    uint32_t envTex_    = 0;

    // --- 敵のSDF消滅演出（同時数ぶんの固定プール）---
    struct DissolveFx {
        std::unique_ptr<SDFVolumeObject> vol;
        float timer = -1.0f;   // 経過秒。負=未使用
        float scale = 1.2f;    // ボールの見た目サイズ（敵の直径に合わせる）
    };
    std::vector<DissolveFx> dissolves_;
};
