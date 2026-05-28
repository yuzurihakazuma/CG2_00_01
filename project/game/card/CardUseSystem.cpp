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
#include "game/card/SwordEffect.h"
#include "game/card/HammerEffect.h"
#include "game/card/SpearEffect.h"
#include "game/card/BossChargeEffect.h"
#include "game/card/BossKickEffect.h"
#include "game/card/BossSpearEffect.h" // ボス専用の槍攻撃
#include "game/audio/GameSE.h"
#include "engine/particle/GPUParticleManager.h"

using namespace VectorMath;

namespace {
bool IsMagicUseCard(int cardId) {
	switch (cardId) {
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 8:
	case 9:
	case 11:
	case 12:
	case 102:
	case 103:
	case 104:
		return true;
	default:
		return false;
	}
}
}

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
	effectFactory_[14] = [](const Card &c) {return std::make_unique<SwordEffect>(c.effectValue); };
	effectFactory_[15] = [](const Card &c) {return std::make_unique<HammerEffect>(c.effectValue); };
	effectFactory_[16] = [](const Card &c) {return std::make_unique<SpearEffect>(c.effectValue); };

	// ID:101 ボスクロー（前回作ったもの）
	effectFactory_[101] = [](const Card &c) { return std::make_unique<BossClawEffect>(c.effectValue); };

	// ID:102 ボスファイヤー（今回作ったもの）★これを追加！
	effectFactory_[102] = [](const Card &c) { return std::make_unique<BossFierEffect>(c.effectValue); };
	effectFactory_[104] = [](const Card &c) { return std::make_unique<RuinBeamEffect>(c.effectValue); };

	// ボス召喚エフェクトの引数（召喚数）もCSVから受け取る想定
	effectFactory_[103] = [](const Card &c) { return std::make_unique<BossSummonEffect>(c.effectValue); };
	// ID:105 ボス突進攻撃
	effectFactory_[105] = [](const Card& c) { return std::make_unique<BossChargeEffect>(c.effectValue); };
	// ID:106 ボス蹴り攻撃
	effectFactory_[106] = [](const Card& c) { return std::make_unique<BossKickEffect>(c.effectValue); };
	// ID:107 ボス槍攻撃
	effectFactory_[107] = [](const Card& c) { return std::make_unique<BossSpearEffect>(c.effectValue); };
	Reset();
}

// 更新
void CardUseSystem::Update(Player* player, EnemyManager* enemyManager, Boss* boss, Boss* extraBoss,
	const Vector3& playerPos, const Vector3& enemyPos, const Vector3& bossPos, const LevelData& level) {

	// システム（クラス化した魔法）の更新・お掃除処理
	for (auto it = activeEffects_.begin(); it != activeEffects_.end(); ) {
		// リストの中の魔法を更新
		(*it)->Update(player, enemyManager, boss, extraBoss, bossPos, level);

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

		// 収束先：プレイヤーの胸の高さ
		Vector3 gatherTo = { castPos_.x, castPos_.y + 1.0f, castPos_.z };

		// 詠唱進行度（0→1、後半ほど速く・派手に）
		float chargeRatio = 1.0f - static_cast<float>(castTimer_) / 20.0f;
		// タイマーに連動して回転する基準角度（渦巻き感を出す）
		float baseAngle = static_cast<float>(20 - castTimer_) * 0.45f;

		// 6方向の螺旋アームがプレイヤーへ収束する
		const int outerCount = 6;
		for (int i = 0; i < outerCount; i++) {
			float angle = baseAngle + (3.14159f * 2.0f / outerCount) * static_cast<float>(i);
			float radius = 2.0f + static_cast<float>(rand() % 6) * 0.2f;
			Vector3 emitPos = {
				castPos_.x + std::sinf(angle) * radius,
				castPos_.y + 1.0f + static_cast<float>(rand() % 7 - 3) * 0.15f,
				castPos_.z + std::cosf(angle) * radius
			};
			float dx = gatherTo.x - emitPos.x;
			float dy = gatherTo.y - emitPos.y;
			float dz = gatherTo.z - emitPos.z;
			float dist = std::sqrtf(dx * dx + dy * dy + dz * dz);
			if (dist > 0.01f) {
				// 詠唱後半ほど速くなって「吸い込まれる加速感」を出す
				float speed = 6.0f + chargeRatio * 2.0f;
				float lifeTime = dist / speed + 0.03f;
				// 接線成分を強めにして螺旋の弧が見える
				float sideAngle = angle + 1.57f;
				Vector3 vel = {
					dx / dist * speed + std::sinf(sideAngle) * 0.9f,
					dy / dist * speed,
					dz / dist * speed + std::cosf(sideAngle) * 0.9f
				};
				float sc = 0.22f + chargeRatio * 0.06f;
				GPUParticleManager::GetInstance()->Emit(emitPos, vel, lifeTime, sc, { 1.0f, 0.85f, 0.2f, 0.35f });
			}
		}

		// 中心輝き（極小・控えめ）
		float glowScale = 0.18f + chargeRatio * 0.18f;
		GPUParticleManager::GetInstance()->Emit(gatherTo, { 0,0,0 }, 0.08f, glowScale, { 1.0f, 0.95f, 0.5f, 0.3f });

		if (castTimer_ <= 0) {
			isCasting_ = false;

			// 詠唱開始時に保持していた発射元ボスも一緒に渡して実行する
			ExecuteCard(
				castingCard_,
				castPos_,
				castYaw_,
				isPlayerCasting_,
				castingPlayer_,
				castingBoss_
			);
		}

	}

	
}

// 描画
void CardUseSystem::Draw() {

	// 呼び出し元で3D用パイプラインが設定されている前提で各エフェクトを描画する
	for (const auto& effect : activeEffects_) {
		effect->Draw();
	}
}

// カード使用開始
// ボス使用時は casterBoss に発動元ボス本人が入る
void CardUseSystem::UseCard(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player, Boss* casterBoss) {



	// ボス使用時は詠唱待ちせず即実行する
// この時に発動元ボスもそのまま effect 側へ渡す
	if (!isPlayerCaster) {
		ExecuteCard(card, casterPos, casterYaw, isPlayerCaster, player, casterBoss);
		return;
	}


	// すでに詠唱中なら新しく使わない
	if (isCasting_) {
		return;
	}

	// プレイヤー使用時は詠唱情報を保持し、完了後に ExecuteCard する
	isCasting_ = true;
	castingCard_ = card;
	castPos_ = casterPos;
	castYaw_ = casterYaw;
	isPlayerCasting_ = isPlayerCaster;
	castingPlayer_ = player;

	// 発動元ボスも保持しておく
	// 今は主にボス用だが、経路を統一するためここで保存する
	castingBoss_ = casterBoss;

	castTimer_ = GetCastTime(card);



	// プレイヤーが使った場合のみ詠唱中ロック
	if (isPlayerCaster && player) {
		player->LockAction(castTimer_);
		player->PlayCardUsePose(castTimer_ / 2); // 詠唱開始時にカード使用ポーズへ入る
	}
}

// 実際のカード発動処理
void CardUseSystem::UseCardImmediately(const Card& card, const Vector3& casterPos, float casterYaw, bool isPlayerCaster, Player* player, Boss* casterBoss, bool restoreIdlePose) {
	ExecuteCard(card, casterPos, casterYaw, isPlayerCaster, player, casterBoss, restoreIdlePose);
}

void CardUseSystem::ExecuteCard(const Card& card,const Vector3& casterPos,float casterYaw,bool isPlayerCaster,Player* player,Boss* casterBoss, bool restoreIdlePose) {

	// カード発動時：収束した光が一気に解放されるバーストエフェクト
	for (int i = 0; i < 30; i++) {
		float angle = static_cast<float>(rand() % 628) * 0.01f;
		float elevation = static_cast<float>(rand() % 628) * 0.01f;
		float speed = 0.4f + static_cast<float>(rand() % 10) * 0.1f;
		Vector3 vel = {
			std::sinf(angle) * std::cosf(elevation) * speed,
			std::sinf(elevation) * speed + 0.1f,
			std::cosf(angle) * std::cosf(elevation) * speed
		};
		GPUParticleManager::GetInstance()->Emit(
			{ casterPos.x, casterPos.y + 1.3f, casterPos.z },
			vel, 0.3f, 0.35f, { 1.0f, 0.9f, 0.3f, 1.0f });
	}

	// 1. 辞書の中に、使われたカードのIDが登録されているかチェック
	if (effectFactory_.count(card.id)) {
		// プレイヤー・敵・ボス問わず、使用するカードに応じたSEを再生
		switch (card.id) {
		case 1:  // SE: 殴りの音を再生
			GameSE::Fist(); break;
		case 2:  // SE: ファイヤーボールの音を再生
			GameSE::Fireball(); break;
		case 3:  // SE: ポーションの音を再生
			GameSE::Potion(); break;
		case 4:  // SE: スピードアップの音を再生
			GameSE::SpeedUp(); break;
		case 5:  // SE: シールドの音を再生
			GameSE::Shield(); break;
		case 6:  // SE: アイスの音を再生
			GameSE::Ice(); break;
		case 7:  // SE: トゲの音を再生
			GameSE::Fang(); break;
		case 8:  // SE: 身代わりの音を再生
			GameSE::Decoy(); break;
		case 9:  // SE: 攻撃力低下の音を再生
			GameSE::AtkDown(); break;
		case 10: // SE: クローの音を再生
			GameSE::Claw(); break;
		case 11: // SE: マップ表示の音を再生
			GameSE::Scanner(); break;
		case 12: // SE: コストブーストの音を再生
			GameSE::CostBoost(); break;
		case 13: // SE: 蹴りの音を再生
			GameSE::Kick(); break;
		case 14: // SE: 剣の音を再生
			GameSE::Sword(); break;
		case 15: // SE: ハンマーの音を再生
			GameSE::Hammer(); break;
		case 16: // SE: 槍の音を再生
			GameSE::Spear(); break;
		// ボス用SE
		case 103: // SE: ボス召喚の音を再生
			GameSE::BossSummon(); break;
		case 104: // SE: ボスビームの音を再生
			GameSE::BossBeam(); break;
		case 105: // SE: ボス突進の音を再生
			GameSE::BossCharge(); break;
		default:
			if (IsMagicUseCard(card.id)) {
				GameSE::CardUseMagic();
			} else {
				GameSE::CardUseAttack();
			}
			break;
		}

		// ID:102（ボス炎弾）はボス使用時のみ全方位6発に展開する（全体攻撃）
		if (card.id == 102 && !isPlayerCaster) {
			const int kFireCount = 6; // 360°を6等分
			int reducedDamage = card.effectValue > 1 ? card.effectValue - 1 : 1; // ダメージを1減らす
			for (int fi = 0; fi < kFireCount; fi++) {
				float spreadYaw = casterYaw + (3.14159f * 2.0f / kFireCount) * fi;
				auto fireEffect = std::make_unique<BossFierEffect>(reducedDamage);
				fireEffect->Start(casterPos, spreadYaw, isPlayerCaster, camera_, casterBoss);
				activeEffects_.push_back(std::move(fireEffect));
			}
		} else {
			// 2. 通常：辞書から魔法を生み出す！（switch文の代わり）
			auto newEffect = effectFactory_[card.id](card);

			if (newEffect) {
				// エフェクト開始時に発射元ボスも渡す
				// 分裂ボス時に、どのボス本人が撃った攻撃かを各 effect が保持できるようにする
				newEffect->Start(casterPos, casterYaw, isPlayerCaster, camera_, casterBoss);
				activeEffects_.push_back(std::move(newEffect));
			}
		}

	}

	// プレイヤー発動後は通常姿勢へ戻す
	if (restoreIdlePose && isPlayerCaster && player) {
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
	// 詠唱元ボスの保持もリセットする
	castingBoss_ = nullptr;
}

void CardUseSystem::EnsureShieldVisual(Player* player) {
	if (!player || player->GetShieldHits() <= 0) {
		return;
	}

	for (const auto& effect : activeEffects_) {
		if (dynamic_cast<ShieldEffect*>(effect.get()) && !effect->IsFinished()) {
			return;
		}
	}

	auto shieldEffect = std::make_unique<ShieldEffect>();
	shieldEffect->RestoreVisual(player->GetPosition(), camera_);
	activeEffects_.push_back(std::move(shieldEffect));
}

void CardUseSystem::CancelCasting() {
	isCasting_ = false;
	castTimer_ = 0;
	castingCard_ = { -1, "", 0 };
	castPos_ = { 0.0f, 0.0f, 0.0f };
	castYaw_ = 0.0f;
	isPlayerCasting_ = true;
	castingPlayer_ = nullptr;
	// キャンセル時も発射元ボス情報を破棄する
	castingBoss_ = nullptr;
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







