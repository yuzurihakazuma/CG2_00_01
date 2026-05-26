#include "SpeedBuffEffect.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/postEffect/PostEffect.h"

void SpeedBuffEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPLayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isPlayerCaster_ = isPLayerCaster;
	isFinished_ = false;
	currentPos_ = casterPos;
    timer_ = 0; //  タイマーリセット

}

void SpeedBuffEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {

	if (isFinished_) {
		return;
	}

	if ( isPlayerCaster_ && player ) {
        Vector3 playerPos = player->GetPosition();
        currentPos_ = playerPos;

        // 発動瞬間（0〜3フレーム）：速度感を演出するラジアルブラー
        if ( timer_ == 0 ) {
            PostEffect::GetInstance()->SetRadialBlurStrength(0.75f);
            PostEffect::GetInstance()->SetEffectActive(PostEffectType::RadialBlur, true);
        } else if ( timer_ == 4 ) {
            PostEffect::GetInstance()->SetEffectActive(PostEffectType::RadialBlur, false);
        }


        if ( timer_ % 2 == 0 ) { // 2フレームに1回（疾走感を出すため高頻度）
            // プレイヤーの周囲（-0.4f 〜 +0.4f）にランダムな発生座標を作る
            Vector3 sparkPos = playerPos;
            sparkPos.x += static_cast< float >( rand() % 9 - 4 ) * 0.1f;
            sparkPos.y += 0.1f + static_cast< float >( rand() % 15 ) * 0.1f; // 足元〜頭の高さ
            sparkPos.z += static_cast< float >( rand() % 9 - 4 ) * 0.1f;

            // 上に向かってシュバッと昇る速度
            Vector3 sparkVel = {
                static_cast< float >( rand() % 11 - 5 ) * 0.04f, // 左右の微細な揺れ
                1.2f + static_cast< float >( rand() % 11 ) * 0.1f, // 🌟 速めに上昇させてスピード感を出す
                static_cast< float >( rand() % 11 - 5 ) * 0.04f
            };

            // スピード感のある水色
            Vector4 sparkColor = { 0.4f, 0.8f, 1.0f, 1.0f };
            // サイズは小さくしてスタイリッシュに
            float sparkScale = 0.08f + static_cast< float >( rand() % 5 ) * 0.02f;

            // 直接 Manager を叩いて火の粉を発生！
            GPUParticleManager::GetInstance()->Emit(sparkPos, sparkVel, 0.4f, sparkScale, sparkColor);
        }

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


    timer_++;

    // タイマー減算
    durationTimer_--;
    if ( durationTimer_ <= 0 ) {
        PostEffect::GetInstance()->SetEffectActive(PostEffectType::RadialBlur, false);
        isFinished_ = true;
    }

}

void SpeedBuffEffect::Draw() {
}
