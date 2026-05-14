#include "SpeedBuffEffect.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"

void SpeedBuffEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPLayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isPlayerCaster_ = isPLayerCaster;
	isFinished_ = false;
	currentPos_ = casterPos;

}

void SpeedBuffEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss,  const Vector3 &bossPos, const LevelData &level) {

	if (isFinished_) {
		return;
	}

	if ( isPlayerCaster_ && player ) {
		currentPos_ = player->GetPosition();

        if ( durationTimer_ == 300 ) {
            player->ApplySpeedBuff(multiplier_, durationTimer_);
        }
	}

    // 毎フレーム1〜2個出すことで「常にまとっている」感を出します
    for ( int i = 0; i < 2; i++ ) {
        // 足元付近にランダムに配置
        float angle = static_cast< float >(rand() % 628) * 0.01f;
        float radius = 0.5f;

        Vector3 emitPos = {
            currentPos_.x + std::cosf(angle) * radius,
            currentPos_.y + 0.1f, // 足元
            currentPos_.z + std::sinf(angle) * radius
        };

        // 速度：少しだけ上に昇りつつ、プレイヤーが動くと自然に後ろに残るように
        // もし「進行方向の逆に飛ばしたい」なら、playerの速度ベクトルを引くとより疾走感が出ます
        Vector3 vel = {
            ( rand() % 10 - 5 ) * 0.01f,
            0.2f + ( rand() % 5 ) * 0.05f, // ゆっくり上昇
            ( rand() % 10 - 5 ) * 0.01f
        };

        // 色：スピード感のある「青」や「水色」
        Vector4 color = { 0.3f, 0.6f, 1.0f, 0.8f };

        float lifeTime = 0.5f + ( rand() % 5 ) * 0.1f;
        float scale = 0.2f + ( rand() % 3 ) * 0.1f;

        GPUParticleManager::GetInstance()->Emit(emitPos, vel, lifeTime, scale, color);
    }

    // タイマー減算
    durationTimer_--;
    if ( durationTimer_ <= 0 ) {
        isFinished_ = true;
    }

}

void SpeedBuffEffect::Draw() {
}
