#include "DecoyEffect.h"
#include <cmath>
#include "engine/particle/GPUParticleManager.h"
#include "engine/collision/Collision.h"

using namespace VectorMath;

void DecoyEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterBoss;
	isFinished_ = false;
	timer_ = duration_;

	casterPos_ = casterPos;

	Vector3 forward = { std::sinf(casterYaw), 0.0f, std::cosf(casterYaw) };
	pos_ = { casterPos.x + forward.x * 2.0f, casterPos.y, casterPos.z + forward.z * 2.0f };

	// プレイヤー本体と色設定を分離するため、デコイ専用のモデル名で生成する
	model_ = SkinnedObj3d::Create("playerDecoy", "resources/player", "player.gltf");
	if (model_) {
		model_->SetName("Decoy");
		model_->SetCamera(camera);
		model_->SetLoopAnimation(true);
		model_->SetIsWalking(false);
		model_->SetScale(scale_);
		model_->SetRotation({ 0.0f, casterYaw, 0.0f });
		model_->SetTranslation(pos_);

		// 本体と見分けやすいように、少し青白くして軽く発光させる
		if (model_->GetModel()) {
			model_->GetModel()->SetColor({ 0.70f, 0.90f, 1.25f, 0.95f });
			if (model_->GetModel()->GetMaterial()) {
				model_->GetModel()->GetMaterial()->emissive = 1.6f;
			}
		}

		model_->Update();
	}

}

void DecoyEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {
	(void)player;
	(void)enemyManager;
	(void)boss;
	(void)bossPos;
	(void)level;

	if (isFinished_) {
		return;
	}

	// ==========================================
	// 最初の1フレーム目だけ壁判定を行う
	// ==========================================
	if (timer_ == duration_) {

		bool isWall = Collision::CheckBlockCollision(pos_, 0.5f, level); 

		if (isWall) {
			// 目の前が壁だったら、保存しておいたプレイヤーの足元に書き換える
			pos_ = casterPos_;

			// モデルの座標を即座に上書きしてチラつきを防ぐ
			if (model_) {
				model_->SetTranslation(pos_);
				model_->Update();
			}
		}
	}

	timer_--;
	if (timer_ <= 0) {
		isFinished_ = true;
	}

	if (model_) {
		model_->SetIsWalking(false);
		model_->SetTranslation(pos_);
		model_->Update();

		for ( int i = 0; i < 3; i++ ) {
			float angle = static_cast< float >(rand() % 628) * 0.01f;
			float radius = 0.3f + static_cast< float >(rand() % 5) * 0.1f;

			Vector3 emitPos = {
				pos_.x + std::cosf(angle) * radius,
				pos_.y + 0.5f + static_cast< float >(rand() % 10) * 0.2f, // 高さランダム
				pos_.z + std::sinf(angle) * radius
			};
			Vector3 vel = {
				std::cosf(angle) * 0.05f,
				0.1f + static_cast< float >( rand() % 5 ) * 0.05f, // ふわっと上に
				std::sinf(angle) * 0.05f
			};

			// デコイの色（青白）に合わせた色
			Vector4 color = { 0.5f, 0.8f, 1.0f, 0.8f };
			float life = 0.3f + static_cast< float >( rand() % 3 ) * 0.1f;
			float scale = 0.1f + static_cast< float >( rand() % 3 ) * 0.05f;

			GPUParticleManager::GetInstance()->Emit(emitPos, vel, life, scale, color);
		}

	}
}

void DecoyEffect::Draw() {
	if (!isFinished_ && model_) {
		model_->Draw();
	}
}
