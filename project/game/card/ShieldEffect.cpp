#include "ShieldEffect.h"
#include "game/player/Player.h"
#include "game/enemy/EnemyManager.h"
#include "engine/particle/GPUParticleManager.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>


void ShieldEffect::Start(const Vector3 &casterPos, float casterYaw, bool isPlayerCaster, Camera *camera, Boss* casterBoss) {
	// この効果は発動元ボスを使わない
	(void)casterYaw;
	(void)casterBoss;
	// 誰が使ったか（プレイヤーか敵か）を保存
	isPlayerCaster_ = isPlayerCaster;

	// 演出終了フラグをリセットし、タイマーを初期化
	isFinished_ = false;
	isFirstFrame_ = true;
	rotAngle_ = 0.0f;

	CreateShieldObject(casterPos, camera);
}

void ShieldEffect::RestoreVisual(const Vector3 &casterPos, Camera *camera) {
	isPlayerCaster_ = true;
	isFinished_ = false;
	isFirstFrame_ = false;
	rotAngle_ = 0.0f;

	CreateShieldObject(casterPos, camera);
}

void ShieldEffect::CreateShieldObject(const Vector3 &casterPos, Camera *camera) {
	// シールド用オブジェクト生成
	obj_ = Obj3d::Create("shield_sphere");
	if (obj_) {
		obj_->SetCamera(camera);
		obj_->SetScale(scale_);
		obj_->SetTranslation(casterPos);
		obj_->Update();


		Model* model = obj_->GetModel();

		if ( model ){


			model->SetTexture("resources/white1x1.png"); // ← 任意のシールド画像に変えてください

			Model::Material* material = model->GetMaterial();
			if ( material ){



				material->color = { 0.5f, 0.8f, 1.0f, 1.0f }; // 水色

				material->emissive = 1.8f; // Bloomに乗る強さまで自己発光を上げる


			}

		}


	}
}

void ShieldEffect::Update(Player *player, EnemyManager *enemyManager, Boss *boss, Boss *extraBoss,  const Vector3 &bossPos, const LevelData &level) {

	// すでに演出が終わっている場合は何もせずに返す
	if (isFinished_) {
		return;
	}
	if ( isPlayerCaster_ && player != nullptr && !player->IsDead() ) {

		// 最初の１フレーム目だけ、プレイヤー本体に「3回防ぐシールド」を付与
		if ( isFirstFrame_ ) {
			player->AddShieldHits(3);
			isFirstFrame_ = false;
		}

		// 今のシールド残り回数を取得
		int hits = player->GetShieldHits();

		// シールド破壊時（0回）の爆発処理
		if ( hits <= 0 ) {
			
			Vector3 centerPos = player->GetPosition();
			centerPos.y += 1.0f; // 回る玉と同じ高さにする
			
			int burstCount = 150;

			// 四方八方にパーティクルを散らして「パキィッ！」という破砕感を出す
			for ( int i = 0; i < burstCount; i++ ) {

				float angleX = static_cast< float >(rand() % 628) * 0.01f;
				float angleY = static_cast< float >(rand() % 628) * 0.01f;
				
				float distance = static_cast< float >(rand() % 15) * 0.1f;

				// 発生位置（中心からのズレ）
				Vector3 emitPos = {
					centerPos.x + std::cosf(angleX) * std::cosf(angleY) * distance,
					centerPos.y + std::sinf(angleY) * distance,
					centerPos.z + std::sinf(angleX) * std::cosf(angleY) * distance
				};

				// 速度（中心から外に向かうベクトルを少し速めにする）
				float speed = 1.5f + static_cast< float >( rand() % 20 ) * 0.1f;

				Vector3 burstVel = {
					std::cosf(angleX) * std::cosf(angleY) * speed,
					std::sinf(angleY) * speed,
					std::sinf(angleX) * std::cosf(angleY) * speed
				};

				if ( rand() % 100 < 20 ) {
					// 【火花（スパーク）】速い、小さい、白い光
					Vector4 sparkColor = { 1.0f, 1.0f, 1.0f, 1.0f }; // ピカッと光る白
					float sparkLife = 0.2f + static_cast< float >( rand() % 3 ) * 0.1f;
					float sparkScale = 0.1f + static_cast< float >( rand() % 2 ) * 0.1f;

					// 火花はさらに1.5倍の速度でシュパッと飛ばす
					Vector3 sparkVel = { burstVel.x * 1.5f, burstVel.y * 1.5f, burstVel.z * 1.5f };

					GPUParticleManager::GetInstance()->Emit(centerPos, sparkVel, sparkLife, sparkScale, sparkColor);
				} else {
					// 【破片（シャード）】大きめ、少し長持ち、バリアの色
					// 割れる直前は赤（危険状態）なので、赤〜オレンジ系の色にする
					Vector4 shardColor = { 1.0f, 0.3f, 0.1f, 1.0f };

					// 寿命を少し伸ばして余韻を出す（0.3〜0.7）
					float shardLife = 0.3f + static_cast< float >( rand() % 5 ) * 0.1f;

					// 大きさも少しアップさせる（0.3〜0.6）
					float shardScale = 0.3f + static_cast< float >( rand() % 4 ) * 0.1f;

					GPUParticleManager::GetInstance()->Emit(centerPos, burstVel, shardLife, shardScale, shardColor);
				}
			}

			// シールド破壊：被弾ビネットをプレイヤー経由で発動
			player->TriggerHitFlash();

			isFinished_ = true;
			return;
		}


		//  ここで「ベースとなる色」を残り回数に応じて決定する
		Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 0.4f }; // デフォルト
		if ( hits == 3 ) {
			baseColor = { 0.2f, 0.8f, 1.0f, 0.4f }; // 青（余裕）
		} else if ( hits == 2 ) {
			baseColor = { 1.0f, 0.8f, 0.2f, 0.4f }; // 黄（注意）
		} else if ( hits == 1 ) {
			baseColor = { 1.0f, 0.2f, 0.2f, 0.4f }; // 赤（危険！）
		}


		// 1. バリアモデル（球）の色と発光を更新
		if ( obj_ && obj_->GetModel() ){
			Model::Material* material = obj_->GetModel()->GetMaterial();
			if ( material ){
				material->color = baseColor;  // 上で決めた色を入れる
				material->emissive = 0.8f;    // 発光を少し強めにして目立たせる
			}
		}


		// 2. 回る光の玉（パーティクル）の処理
		rotAngle_ += 0.1f;
		int kOrbCount = hits; // 💡 変更：玉の数を残り回数（hits）と全く同じにする！
		float radius = 1.2f;

		for ( int i = 0; i < kOrbCount; i++ ) {
			// 玉を均等な間隔で配置
			float angle = rotAngle_ + ( 3.141592f * 2.0f / kOrbCount ) * i;

			Vector3 emitPos = {
				player->GetPosition().x + std::cosf(angle) * radius,
				player->GetPosition().y + 1.0f,
				player->GetPosition().z + std::sinf(angle) * radius
			};

			Vector3 vel = { 0.0f, 0.05f, 0.0f };

			// 💡 変更：玉の色をバリア本体の色(baseColor)と合わせる！
			// パーティクルなので透明度（W）だけは 1.0f（不透明）にしてクッキリさせる
			Vector4 particleColor = { baseColor.x, baseColor.y, baseColor.z, 1.0f };

			float lifeTime = 0.2f;
			float scale = 0.3f;

			GPUParticleManager::GetInstance()->Emit(emitPos, vel, lifeTime, scale, particleColor);
		}

		// 3. シールドのモデルを、常にプレイヤーの現在位置にピッタリ追従させる
		if ( obj_ ) {
			obj_->SetTranslation(player->GetPosition());
			obj_->Update();
		}
	}


}

void ShieldEffect::Draw() {
	// まだ演出が終わっておらず、モデルがちゃんと作られていれば描画する
	if (!isFinished_ && obj_) {
		obj_->Draw();
	}
}
