#include "CostBoostEffect.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"

void CostBoostEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
    // この効果は発動元ボスを使わない
    (void)casterBoss;
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

void CostBoostEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) {
    if (isFinished_) return;

    if (isPlayerCaster_ && player && !player->IsDead()) {

        Vector3 playerPos = player->GetPosition();

        // ==========================================
        // ★ 1. 最初の1フレーム目にバフをかける！
        // ==========================================
        if (timer_ == 0) {
            player->AddMaxCost(costBoostAmount_);        // コスト上限を増やす（※後でPlayerに追加）
            player->SetCostRecoveryMultiplier(2.0f);     // 回復速度を2倍にする（※後でPlayerに追加）
        }

        // --- ここにオーラなどのパーティクル演出を入れるとカッコいいです！ ---

        if ( auraEmitter_ ) {
            auto data = auraEmitter_->GetData();

            // 発生位置をプレイヤーの「体の中心」にする
            data.position = playerPos;
            data.position.y += 0.05f; // 🌟 1.0f くらいで腰～胸あたりになります。少し高い/低い場合はこの数値を微調整してください。

            auraEmitter_->SetData(data);
            auraEmitter_->Burst();
        }

        if ( timer_ % 3 == 0 ) { // 3フレームに1回（多すぎず少なすぎず）
            // プレイヤーの周囲（-0.5f 〜 +0.5f）にランダムな発生座標を作る
            Vector3 sparkPos = playerPos;
            sparkPos.x += static_cast< float >( rand() % 11 - 5 ) * 0.1f;
            sparkPos.y += 0.2f + static_cast< float >( rand() % 11 ) * 0.1f; // 足元〜胸のランダムな高さ
            sparkPos.z += static_cast< float >( rand() % 11 - 5 ) * 0.1f;

            // 上に向かってフワッと昇る速度
            Vector3 sparkVel = {
                static_cast< float >( rand() % 11 - 5 ) * 0.05f, // 左右の揺らぎ
                0.8f + static_cast< float >( rand() % 11 ) * 0.1f, // 上昇する速度
                static_cast< float >( rand() % 11 - 5 ) * 0.05f
            };

            // 明るい黄色で、サイズは小さめ
            Vector4 sparkColor = { 1.0f, 0.9f, 0.4f, 1.0f };
            float sparkScale = 0.1f + static_cast< float >( rand() % 5 ) * 0.02f;

            // 直接 Manager を叩いて火の粉を発生！
            GPUParticleManager::GetInstance()->Emit(sparkPos, sparkVel, 0.4f, sparkScale, sparkColor);
        }

        timer_++;

        // ==========================================
        // ★ 2. 時間切れになったらバフをはがして元に戻す！
        // ==========================================
        if (timer_ >= duration_) {
            player->AddMaxCost(-costBoostAmount_);       // 増やした分を引いて元に戻す
            player->SetCostRecoveryMultiplier(1.0f);     // 回復倍率を1.0（通常）に戻す
            isFinished_ = true;
        }
    } else {
        isFinished_ = true;
    }
}
