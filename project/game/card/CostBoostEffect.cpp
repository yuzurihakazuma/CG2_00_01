#include "CostBoostEffect.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"

void CostBoostEffect::Start(const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Camera* camera, Boss* casterBoss){
    // この効果は発動元ボスを使わない
    ( void ) casterBoss;
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;


    // ==========================================
    //  継続中の「モヤモヤ」（オーラ）の設定
    // 最初の爆発がない分、少しリッチ（大きめ・多め）にします
    // ==========================================
    auraEmitter_ = std::make_unique<GPUParticleEmitter>();
    GPUParticleEmitterData auraData {};
    auraData.name = "CostBoostCore";
    auraData.burstCount = 1;                   // 毎フレーム1つ出す
    auraData.lifeTimeMin = 0.1f;               // 🌟 寿命を極端に短く（0.1秒）して、移動時に尻尾を引かないようにする
    auraData.lifeTimeMax = 0.2f;
    auraData.scaleMin = 1.0f;                  // 🌟 体の中心（お腹・胸）を覆うくらいの大きさ
    auraData.scaleMax = 1.8f;
    auraData.velocity = { 0.0f, 0.0f, 0.0f };  // 🌟 動かない（上に昇らない）
    auraData.velocitySpread = 0.0f;            // 🌟 横にも散らばらない
    auraData.startColor = { 1.0f, 0.9f, 0.4f, 0.8f }; // 黄金色で少し濃いめに光らせる
    auraData.endColor = { 1.0f, 0.6f, 0.1f, 0.0f };
    auraEmitter_->SetData(auraData);
}

void CostBoostEffect::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss, const Vector3& bossPos, const LevelData& level){
    if ( isFinished_ ) return;

    if ( isPlayerCaster_ && player && !player->IsDead() ) {

        Vector3 playerPos = player->GetPosition();

        // ==========================================
        // ★ 1. 最初の1フレーム目にバフをかける！
        // ==========================================
        if ( timer_ == 0 ) {
            player->AddMaxCost(costBoostAmount_);        // コスト上限を増やす（※後でPlayerに追加）
            player->SetCostRecoveryMultiplier(2.0f);     // 回復速度を2倍にする（※後でPlayerに追加）

            // 発動瞬間のバースト（金色の爆発）
            for ( int i = 0; i < 20; i++ ) {
                Vector3 burstPos = {
                    playerPos.x + ( float ) ( rand() % 11 - 5 ) * 0.1f,
                    playerPos.y + 0.8f,
                    playerPos.z + ( float ) ( rand() % 11 - 5 ) * 0.1f
                };
                Vector3 burstVel = {
                    ( float ) ( rand() % 11 - 5 ) * 0.22f,
                    0.4f + ( float ) ( rand() % 10 ) * 0.1f,
                    ( float ) ( rand() % 11 - 5 ) * 0.22f
                };
                GPUParticleManager::GetInstance()->Emit(burstPos, burstVel, 0.5f, 0.45f, { 1.0f, 0.85f, 0.1f, 1.0f });
            }
            // 発動瞬間の大フラッシュ
            GPUParticleManager::GetInstance()->Emit(
                { playerPos.x, playerPos.y + 0.8f, playerPos.z },
                { 0, 0, 0 }, 0.2f, 2.8f, { 1.0f, 0.95f, 0.5f, 0.8f }
            );
        }

        // --- ここにオーラなどのパーティクル演出を入れるとカッコいいです！ ---

        // 10フレームに1回、大きなグロウパルス
        if ( timer_ % 10 == 0 ) {
            GPUParticleManager::GetInstance()->Emit(
                { playerPos.x, playerPos.y + 0.8f, playerPos.z },
                { 0, 0, 0 }, 0.15f, 2.2f, { 1.0f, 0.9f, 0.4f, 0.55f }
            );
        }

        if ( auraEmitter_ ) {
            auto data = auraEmitter_->GetData();

            // 発生位置をプレイヤーの「体の中心」にする
            data.position = playerPos;
            data.position.y += 0.05f; // 🌟 1.0f くらいで腰～胸あたりになります。少し高い/低い場合はこの数値を微調整してください。

            auraEmitter_->SetData(data);
            auraEmitter_->Burst();
        }

        if ( timer_ % 3 == 0 ) {
            // 上昇スパーク（2個）
            for ( int i = 0; i < 2; i++ ) {
                Vector3 sparkPos = playerPos;
                sparkPos.x += ( float ) ( rand() % 11 - 5 ) * 0.15f;
                sparkPos.y += 0.2f + ( float ) ( rand() % 10 ) * 0.12f;
                sparkPos.z += ( float ) ( rand() % 11 - 5 ) * 0.15f;
                Vector3 sparkVel = {
                    ( float ) ( rand() % 11 - 5 ) * 0.04f,
                    0.9f + ( float ) ( rand() % 11 ) * 0.09f,
                    ( float ) ( rand() % 11 - 5 ) * 0.04f
                };
                GPUParticleManager::GetInstance()->Emit(sparkPos, sparkVel, 0.4f, 0.13f, { 1.0f, 0.92f, 0.3f, 1.0f });
            }
        }

        timer_++;

        // ==========================================
        // ★ 2. 時間切れになったらバフをはがして元に戻す！
        // ==========================================
        if ( timer_ >= duration_ ) {
            player->AddMaxCost(-costBoostAmount_);       // 増やした分を引いて元に戻す
            player->SetCostRecoveryMultiplier(1.0f);     // 回復倍率を1.0（通常）に戻す
            isFinished_ = true;
        }
    } else {
        isFinished_ = true;
    }
}