#include "CostBoostEffect.h"
#include "game/player/Player.h"


void CostBoostEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
    // この効果は発動元ボスを使わない
    (void)casterBoss;
    isPlayerCaster_ = isPlayerCaster;
    isFinished_ = false;
    timer_ = 0;
}

void CostBoostEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss, const Vector3 &bossPos, const LevelData &level) {
    if (isFinished_) return;

    if (isPlayerCaster_ && player && !player->IsDead()) {

        // ==========================================
        // ★ 1. 最初の1フレーム目にバフをかける！
        // ==========================================
        if (timer_ == 0) {
            player->AddMaxCost(costBoostAmount_);        // コスト上限を増やす（※後でPlayerに追加）
            player->SetCostRecoveryMultiplier(2.0f);     // 回復速度を2倍にする（※後でPlayerに追加）
        }

        // --- ここにオーラなどのパーティクル演出を入れるとカッコいいです！ ---

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
