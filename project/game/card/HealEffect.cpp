#include "HealEffect.h"
#include "game/player/Player.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/camera/Camera.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>


void HealEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;

	isPlayerCaster_ = isPlayerCaster;
	isFinished_ = false;
	effectTimer_ = 0;          // タイマーをリセット
	currentPos_ = casterPos;   // 最初の位置を記憶
	camera_ = camera;

	hasHealed_ = false;      // 回復処理はまだ行っていない状態にリセット


}

void HealEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {
	if (isFinished_) {
		return;
	}

	// プレイヤーが使った場合のみ回復する
	if ( !hasHealed_ && isPlayerCaster_ && player != nullptr && !player->IsDead()) {
		// 最大HPの半分を回復（レベルアップで最大HPが増えても自動で追従）
		// Player::Heal() 内で maxHp_ 上限クランプ済みなので超過の心配はない
		int healAmount = player->GetMaxHP() / 2;
		if ( healAmount < 1 ) healAmount = 1; // 最低1は回復する
		player->Heal(healAmount);
		hasHealed_ = true; // 回復処理を行ったフラグを立てる
	}

	if ( isPlayerCaster_ && player != nullptr ) {
		currentPos_ = player->GetPosition();

		// プレイヤー位置をスクリーンUVに変換してグロー中心を設定
		if ( camera_ ) {
			const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
			Vector3 wp = { currentPos_.x, currentPos_.y + 1.0f, currentPos_.z };
			float cx = wp.x*vp.m[0][0] + wp.y*vp.m[1][0] + wp.z*vp.m[2][0] + vp.m[3][0];
			float cy = wp.x*vp.m[0][1] + wp.y*vp.m[1][1] + wp.z*vp.m[2][1] + vp.m[3][1];
			float cw = wp.x*vp.m[0][3] + wp.y*vp.m[1][3] + wp.z*vp.m[2][3] + vp.m[3][3];
			if ( std::abs(cw) > 0.0001f ) {
				PostEffect::GetInstance()->SetPlayerScreenUV(
					cx/cw * 0.5f + 0.5f, -cy/cw * 0.5f + 0.5f, 0.20f);
				PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedGlow, true);
			}
		}
	}


	// --- パーティクル放出処理 ---
	// 40個を一気に出すのではなく、20フレームかけて毎フレーム2個ずつ出す
	int particlesPerFrame = 2;
	float headHeight = 2.5f;
	float radius = 1.2f;

	for ( int i = 0; i < particlesPerFrame; i++ ){
		float angle = static_cast< float >(rand() % 628) * 0.01f;

		// 常に最新の currentPos_ の頭上に出現させる
		Vector3 emitPos = {
			currentPos_.x + std::cosf(angle) * radius,
			currentPos_.y + headHeight,
			currentPos_.z + std::sinf(angle) * radius
		};

		Vector3 healVel = {
			std::cosf(angle) * 0.1f,
			-1.8f - static_cast< float >( rand() % 5 ) * 0.2f,
			std::sinf(angle) * 0.1f
		};

		Vector4 color = { 0.2f, 1.0f, 0.4f, 1.0f };
		float lifeTime = 0.4f + static_cast< float >( rand() % 3 ) * 0.1f;
		float scale = 0.3f + static_cast< float >( rand() % 3 ) * 0.1f;

		GPUParticleManager::GetInstance()->Emit(emitPos, healVel, lifeTime, scale, color);
	}

	// タイマーを進める
	effectTimer_++;

	// 指定フレーム経過したらエフェクト終了
	if ( effectTimer_ >= kEffectDuration ) {
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedGlow, false);
		isFinished_ = true;
	}

	

}

void HealEffect::Draw() {

}
