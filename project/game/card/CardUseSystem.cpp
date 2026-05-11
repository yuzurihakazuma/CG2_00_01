#include "game/card/CardUseSystem.h"

#include <cmath>

#include "Engine/Camera/Camera.h"
#include "Engine/3D/Obj/Obj3d.h"
#include "game/player/Player.h"
#include "game/enemy/Enemy.h"
#include "game/enemy/Boss.h"
#include "game/enemy/EnemyManager.h"
#include "engine/math/VectorMath.h"
#include "game/card/FireballEffect.h"
#include "game/card/FistEffect.h"
#include "game/card/HealEffect.h"
#include "game/card/SpeedBuffEffect.h"
#include  "game/card/ShieldEffect.h"
#include "game/card/IceBulletEffect.h"
#include "game/card/FangEffect.h"
#include "game/card/DecoyEffect.h"
#include "game/card/AttackDebuffEffect.h"
#include "game/card/ClawEffect.h"
#include "game/card/BossClawEffect.h"
#include "game/card/BossFierEffect.h"
#include "game/card/RuinBeamEffect.h"
#include "game/card/BossSummonEffect.h"
#include "game/card/MapOpen.h"
#include "game/card/CostBoostEffect.h"
#include "game/card/KickEffect.h"


using namespace VectorMath;

// 初期化
void CardUseSystem::Initialize(Camera* camera) {

	// 受け取ったカメラを変数にほぞんしておく
	camera_ = camera;

	

	effectFactory_[1] = [](const Card &c) { return std::make_unique<FistEffect>(c.effectValue); };
	effectFactory_[2] = [](const Card &c) { return std::make_unique<FireballEffect>(c.effectValue); };
	effectFactory_[3] = [](const Card &c) { return std::make_unique<HealEffect>(5); };           
	effectFactory_[4] = [](const Card &c) { return std::make_unique<SpeedBuffEffect>(1.5f); };   
	effectFactory_[5] = [](const Card &c) { return std::make_unique<ShieldEffect>(); };       
	effectFactory_[6] = [](const Card &c) { return std::make_unique<IceBulletEffect>(); };
	effectFactory_[7] = [](const Card &c) { return std::make_unique<FangEffect>(c.effectValue); };
	effectFactory_[8] = [](const Card &c) { return std::make_unique<DecoyEffect>(300); };
	effectFactory_[9] = [](const Card &c) { return std::make_unique<AttackDebuffEffect>(300); };
	effectFactory_[10] = [](const Card &c) {return std::make_unique<ClawEffect>(c.effectValue); };
	effectFactory_[11] = [this](const Card &card) {return std::make_unique<MapOpen>(minimap_);};
	effectFactory_[12]= [](const Card &c) { return std::make_unique<CostBoostEffect>(c.effectValue); };
	effectFactory_[13] = [](const Card &c) { return std::make_unique<KickEffect>(c.effectValue); };

	// ID:101 ボスクロー（前回作ったもの）
	effectFactory_[101] = [](const Card &c) { return std::make_unique<BossClawEffect>(c.effectValue); };

	// ID:102 ボスファイヤー（今回作ったもの）★これを追加！
	effectFactory_[102] = [](const Card &c) { return std::make_unique<BossFierEffect>(c.effectValue); };
	effectFactory_[104] = [](const Card &c) { return std::make_unique<RuinBeamEffect>(c.effectValue); };

	// ボス召喚エフェクトの引数（召喚数）もCSVから受け取る想定
	effectFactory_[103] = [](const Card &c) { return std::make_unique<BossSummonEffect>(c.effectValue); };

	Reset();
}

// 更新
void CardUseSystem::Update(Player* player, EnemyManager* enemyManager, Boss* boss,
	const Vector3& playerPos, const Vector3& enemyPos, const Vector3& bossPos, const LevelData& level) {

	// システム（クラス化した魔法）の更新・お掃除処理
	for (auto it = activeEffects_.begin(); it != activeEffects_.end(); ) {
		// リストの中の魔法を更新
		(*it)->Update(player, enemyManager, boss,  bossPos, level);

		// もし終わっていたらリストから削除
		if ((*it)->IsFinished()) {
			it = activeEffects_.erase(it);
		} else {
			++it;
		}
	}

	// 詠唱更新
	if (isCasting_) {
		castTimer_--;

		if (castTimer_ <= 0) {
			isCasting_ = false;
			ExecuteCard(castingCard_, castPos_, castYaw_, isPlayerCasting_, castingPlayer_);
		}
	}

	
}

// 描画
void CardUseSystem::Draw() {

	// システムの描画
	for (const auto &effect : activeEffects_) {
		effect->Draw();
	}


	
}

// カード使用（詠唱開始）
void CardUseSystem::UseCard(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player) {

	// 敵（isPlayerCaster が false）の場合は、詠唱を待たずに即発動！
	if (!isPlayerCaster) {
		ExecuteCard(card, casterPos, casterYaw, isPlayerCaster, player);
		return;
	}

	// すでに詠唱中なら新しく使わない
	if (isCasting_) {
		return;
	}

	isCasting_ = true;
	castingCard_ = card;
	castPos_ = casterPos;
	castYaw_ = casterYaw;
	isPlayerCasting_ = isPlayerCaster;
	castingPlayer_ = player;
	castTimer_ = GetCastTime(card);

	// プレイヤーが使った場合のみ詠唱中ロック
	if (isPlayerCaster && player) {
		player->LockAction(castTimer_);
		player->PlayCardUsePose(castTimer_ / 2); // 詠唱開始時にカード使用ポーズへ入る
	}
}

// 実際の発動処理
void CardUseSystem::ExecuteCard(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player) {

	// 1. 辞書の中に、使われたカードのIDが登録されているかチェック
	if (effectFactory_.count(card.id)) {

		// 2. 辞書から魔法を生み出す！（switch文の代わり）
		auto newEffect = effectFactory_[card.id](card);

		// 3. 発生位置などの初期設定をして、リストに追加
		if (newEffect) {
			newEffect->Start(casterPos, casterYaw, isPlayerCaster, camera_);
			activeEffects_.push_back(std::move(newEffect));
		}
	}

	// プレイヤー発動後は通常姿勢へ戻す
	if (isPlayerCaster && player) {
		player->PlayIdlePose(8);
	}
}

// カードの詠唱時間
int CardUseSystem::GetCastTime(const Card& card) const {
	return kCommonCastTime_;
}

// リセット
void CardUseSystem::Reset() {
	// 発動中の効果を全削除
	activeEffects_.clear();

	// 詠唱状態を初期化
	isCasting_ = false;
	castingCard_ = { -1, "", 0 };
	castPos_ = { 0.0f, 0.0f, 0.0f };
	castYaw_ = 0.0f;
	isPlayerCasting_ = true;
	castTimer_ = 0;
	castingPlayer_ = nullptr;
}

void CardUseSystem::CancelCasting() {
	isCasting_ = false;
	castTimer_ = 0;
	castingCard_ = { -1, "", 0 };
	castPos_ = { 0.0f, 0.0f, 0.0f };
	castYaw_ = 0.0f;
	isPlayerCasting_ = true;
	castingPlayer_ = nullptr;
}

bool CardUseSystem::IsDecoyActive() const {
	for (const auto &effect : activeEffects_) {
		// dynamic_cast で「この魔法は DecoyEffect か？」をチェックする
		if (dynamic_cast<DecoyEffect *>(effect.get())) {
			return true; // デコイが見つかった！
		}
	}
	return false; // 見つからなかった
}

Vector3 CardUseSystem::GetDecoyPosition() const {
	for (const auto &effect : activeEffects_) {
		// デコイだったら、その座標を取り出して返す
		if (auto decoy = dynamic_cast<DecoyEffect *>(effect.get())) {
			return decoy->GetPosition();
		}
	}
	return { 0.0f, 0.0f, 0.0f }; // デコイが無い場合はとりあえず原点を返す
}







