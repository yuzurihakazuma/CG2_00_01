#include "game/enemy/Boss.h"
#include "engine/camera/Camera.h"
#include "engine/math/VectorMath.h"
#include "engine/postEffect/PostEffect.h"
#include <cmath>
#include "game/card/CardDatabase.h"
#include "engine/particle/GPUParticleManager.h" 
#include "game/particle/StunEffectManager.h"
#include <algorithm>
#include <cmath>
#include <random>
using namespace VectorMath;

namespace {
	constexpr float kBossAppearDropHeight = 18.0f;
	constexpr float kBeamFiringTurnSpeed = 0.010f;

	float NormalizeAngle(float angle) {
		constexpr float pi = 3.14159265f;
		constexpr float twoPi = pi * 2.0f;

		while (angle > pi) {
			angle -= twoPi;
		}
		while (angle < -pi) {
			angle += twoPi;
		}
		return angle;
	}
}

void Boss::Initialize() {
	rot_ = { 0.0f, 0.0f, 0.0f };
	scale_ = { 2.0f, 2.0f, 2.0f };
	baseScale_ = scale_; // 通常ボスの基準サイズ


	state_ = State::Appear;

	maxHP_ = 35;
	hp_ = maxHP_;
	isDead_ = false;
	deathAnimationTimer_ = 0;
	deathStartY_ = 0.0f;

	thinkTimer_ = 0;

	isActionLocked_ = false;
	actionLockTimer_ = 0;

	isHit_ = false;
	hitTimer_ = 0;
	knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };

	cardUseRequest_ = false;
	selectedCard_ = { -1, "", 0 };
	lastUsedCardId_ = -1;

	isAttackDebuffed_ = false;

	appearTimer_ = appearDuration_;

	cardCooldownTimers_.clear();
	cardCooldownTimer_ = 0;
	castDurationCurrent_ = castTime_;

	attackIntervalTimer_ = 0;  // 共通攻撃クールタイムをリセット

	SetSpawnPosition({ 10.0f, 2.0f, 10.0f });
	InitializeBossCards();

	isSplitBehaviorEnabled_ = false;
	combatRole_ = CombatRole::Melee;
	hasPartnerPosition_ = false;
	forceMeleeMode_ = false;

	isStunned_ = false; // スタン状態リセット
	stunTimer_ = 0;     // スタン時間リセット

}

void Boss::InitializeBossCards() {
	heldCards_.clear();
	heldCards_.push_back(CardDatabase::GetCardData(101));
	heldCards_.push_back(CardDatabase::GetCardData(102));
	heldCards_.push_back(CardDatabase::GetCardData(103));
	heldCards_.push_back(CardDatabase::GetCardData(104));
	heldCards_.push_back(CardDatabase::GetCardData(105));
	heldCards_.push_back(CardDatabase::GetCardData(106));
	heldCards_.push_back(CardDatabase::GetCardData(107)); // ボス専用の槍カードを追加
}

Card Boss::SelectCardForDistance(float dist, bool isEnraged) {
	std::vector<Card> candidates;
	auto addWeightedCard = [&](int cardId, int weight) {
		if (weight <= 0) {
			return;
		}

		for (const auto& card : heldCards_) {
			if (card.id != cardId || !IsCardReady(card.id)) {
				continue;
			}

			for (int i = 0; i < weight; ++i) {
				candidates.push_back(card);
			}
			return;
		}
		};

	if (dist < 5.5f) {
		addWeightedCard(101, 5);
		addWeightedCard(102, isEnraged ? 2 : 1);
		addWeightedCard(104, isEnraged ? 3 : 2);
		addWeightedCard(103, 1);
	}
	else if (dist < 14.0f) {
		addWeightedCard(102, isEnraged ? 7 : 5);
		addWeightedCard(104, isEnraged ? 6 : 4);
		addWeightedCard(103, isEnraged ? 3 : 2);
		addWeightedCard(101, 2);
	}
	else {
		addWeightedCard(102, isEnraged ? 8 : 6);
		addWeightedCard(104, isEnraged ? 7 : 5);
		addWeightedCard(103, isEnraged ? 5 : 4);
	}

	// 連続で同じカードばかりにならないように、別カード候補がある時は前回と同じものを除外する
	bool hasDifferentCard = false;
	for (const auto& card : candidates) {
		if (card.id != lastUsedCardId_) {
			hasDifferentCard = true;
			break;
		}
	}
	if (hasDifferentCard) {
		candidates.erase(
			std::remove_if(
				candidates.begin(),
				candidates.end(),
				[&](const Card& card) { return card.id == lastUsedCardId_; }
			),
			candidates.end()
		);
	}

	if (candidates.empty()) {
		for (const auto& card : heldCards_) {
			if (IsCardReady(card.id)) {
				candidates.push_back(card);
			}
		}
	}

	if (candidates.empty()) {
		return Card{ -1, "", 0 };
	}

	static std::random_device rd;
	static std::mt19937 mt(rd());
	std::uniform_int_distribution<int> distCard(0, static_cast<int>(candidates.size()) - 1);
	return candidates[distCard(mt)];
}

int Boss::GetCastTimeForCard(int cardId, bool isEnraged) const {
	if (cardId == 104) {
		return isEnraged ? 90 : 120;
	}

	if (cardId == 103) {
		return isEnraged ? 46 : 58;
	}

	if (cardId == 101) {
		return isEnraged ? 28 : 38;
	}

	if (cardId == 102) {
		return isEnraged ? 34 : 46;
	}
	if (cardId == 105) {
		return isEnraged ? 18 : 26;
	}

	if (cardId == 106) {
		return isEnraged ? 20 : 28; // ボス蹴りは短めの溜め
	}
	if (cardId == 107) {
		return isEnraged ? 26 : 36; // ボス槍は中くらいの溜め時間
	}


	return isEnraged ? 40 : castTime_;
}

int Boss::GetRecoveryTimeForCard(int cardId, bool isEnraged) const {
	if (cardId == 104) {
		return isEnraged ? 92 : 98;
	}

	if (cardId == 103) {
		return isEnraged ? 28 : 40;
	}

	if (cardId == 102) {
		return isEnraged ? 24 : 34;
	}

	if (cardId == 101) {
		return isEnraged ? 18 : 26;
	}
	if (cardId == 105) {
		return isEnraged ? 16 : 24;
	}

	if (cardId == 106) {
		return isEnraged ? 14 : 22; // ボス蹴りは後隙も短め
	}
	if (cardId == 107) {
		return isEnraged ? 18 : 26; // ボス槍はやや短めの後隙
	}
	return isEnraged ? 18 : 26;
}

void Boss::ApplyCastingPose(float normalizedTime) {
	float t = std::clamp(normalizedTime, 0.0f, 1.0f);
	float settle = t * t * (3.0f - 2.0f * t);
	float sway = std::sinf(t * 6.28318f) * 0.08f;

	rot_.x = 0.10f + settle * 0.22f;
	rot_.z = sway;

	// 破壊光線は必殺技らしく、腰を落としてためてから前へ押し出す構えにする
	if (selectedCard_.id == 104) {
		float charge = std::sinf(t * 3.14159f);
		float pulse = 0.5f + 0.5f * std::sinf(t * 18.84954f);
		float hold = t * t * (3.0f - 2.0f * t);

		rot_.x = 0.22f + hold * 0.55f;
		rot_.z = std::sinf(t * 12.56636f) * 0.08f;

		scale_ = {
	baseScale_.x + charge * 0.25f + pulse * 0.04f,
	baseScale_.y - charge * 0.22f,
	baseScale_.z + hold * 1.25f + pulse * 0.08f
		};


		if (t > 0.72f) {
			float finisher = (t - 0.72f) / 0.28f;
			finisher = std::clamp(finisher, 0.0f, 1.0f);

			rot_.x += finisher * 0.18f;
			scale_.z += finisher * 0.45f;
			scale_.y -= finisher * 0.06f;
		}
		return;
	}



	if (selectedCard_.id == 103) {
		scale_ = {
	baseScale_.x + 0.10f + settle * 0.15f,
	baseScale_.y + settle * 0.35f,
	baseScale_.z + 0.10f + settle * 0.15f
		};

		return;
	}

	scale_ = {
	baseScale_.x + settle * 0.22f,
	baseScale_.y + settle * 0.18f,
	baseScale_.z + settle * 0.22f
	};

}

void Boss::ApplyPreBattlePose(float normalizedTime) {
	float t = std::clamp(normalizedTime, 0.0f, 1.0f);
	float ease = t * t * (3.0f - 2.0f * t);

	rot_.x = 0.10f + ease * 0.22f;
	rot_.z = std::sinf(t * 3.14159f) * 0.05f;

	scale_ = {
	baseScale_.x + ease * 0.10f,
	baseScale_.y - ease * 0.06f,
	baseScale_.z + ease * 0.18f
	};

}

void Boss::ResetPose() {
	rot_.x = 0.0f;
	rot_.z = 0.0f;
	scale_ = baseScale_; // 個体ごとの基準サイズに戻す

}

void Boss::Update() {
	if (isDead_) {
		state_ = State::Dead;
		// 死亡時はマスクエフェクトを無効化
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedDistortion, false);
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedSepia, false);
		UpdateDeathAnimation();
		return;
	}

	animationTimer_++; // 常にカウントアップ

	// スタン状態の更新
	if (isStunned_) {
		stunTimer_--;
		if (stunTimer_ <= 0) {
			isStunned_ = false;
			// 向き・ポーズを復元（StunEffect が rot_.x を上書きしていた分を戻す）
			rot_.y = preStunYaw_;
			ResetPose(); // rot_.x / rot_.z / scale_ をリセット
		}

		StunEffectManager::Update(pos_, rot_, stunTimer_, 4.5f);

		return; // スタン中は以下の AI 更新や移動を全てスキップ
	}

	cardUseRequest_ = false;

	if (state_ == State::Appear) {
		UpdateAppear();
		return;
	}

	if (cardCooldownTimer_ > 0) {
		cardCooldownTimer_--;
	}

	if (attackIntervalTimer_ > 0) {
		attackIntervalTimer_--; // 共通攻撃待ち時間を減らす
	}

	for (auto& [cardId, timer] : cardCooldownTimers_) {
		if (timer > 0) {
			timer--;
		}
	}

	if (isHit_) {
		pos_ += knockbackVelocity_;
		knockbackVelocity_ *= 0.85f;

		hitTimer_--;
		if (hitTimer_ <= 0) {
			isHit_ = false;
			knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
		}
		return;
	}

	if (isActionLocked_) {
		if (selectedCard_.id == 104) {
			Vector3 dir = {
				playerPos_.x - pos_.x,
				0.0f,
				playerPos_.z - pos_.z
			};
			if (Length(dir) > 0.01f) {
				const float targetYaw = std::atan2f(dir.x, dir.z);
				const float yawDiff = std::clamp(
					NormalizeAngle(targetYaw - rot_.y),
					-kBeamFiringTurnSpeed,
					kBeamFiringTurnSpeed
				);
				rot_.y += yawDiff;
				rot_.x = 0.0f;
				rot_.z = 0.0f;
			}
		}

		actionLockTimer_--;
		if (actionLockTimer_ <= 0) {
			isActionLocked_ = false;
		}
		return;
	}

	if (thinkTimer_ > 0) {
		thinkTimer_--;
	}

	if (thinkTimer_ <= 0) {
		DecideNextState();
	}

	UpdateEnrageEffect();

	switch (state_) {
	case State::Idle:
		UpdateIdle();
		break;

	case State::Chase:
		UpdateChase();
		break;

	case State::UseCard:
		UpdateUseCard();
		break;

	case State::Appear:
	case State::Dead:
		break;
	}

	// ---- マスクポストエフェクト（ボス位置をスクリーンUVに変換して渡す） ----
	if ( camera_ && state_ != State::Appear ) {
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		// ワールド座標 → クリップ空間（行ベクトル×行列）
		float cx = pos_.x * vp.m[0][0] + pos_.y * vp.m[1][0] + pos_.z * vp.m[2][0] + vp.m[3][0];
		float cy = pos_.x * vp.m[0][1] + pos_.y * vp.m[1][1] + pos_.z * vp.m[2][1] + vp.m[3][1];
		float cw = pos_.x * vp.m[0][3] + pos_.y * vp.m[1][3] + pos_.z * vp.m[2][3] + vp.m[3][3];
		if ( cw > 0.001f ) {
			float uvX = cx / cw * 0.5f + 0.5f;
			float uvY = -cy / cw * 0.5f + 0.5f;
			PostEffect::GetInstance()->SetBossScreenUV(uvX, uvY, 0.18f, 0.35f);
		}
		// 歪み：カード命中時にイベント駆動でON（常時ではない）/ セピア：無効化
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedSepia, false);
	} else if ( state_ == State::Appear ) {
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedDistortion, false);
		PostEffect::GetInstance()->SetEffectActive(PostEffectType::MaskedSepia, false);
	}
}

void Boss::UpdateAppear() {
	if (appearTimer_ > 0) {
		appearTimer_--;
	}

	float t = 1.0f - (static_cast<float>(appearTimer_) / static_cast<float>(appearDuration_));
	t = std::clamp(t, 0.0f, 1.0f);

	// 落下は速く、着地前で少しためる
	float eased = 1.0f - std::pow(1.0f - t, 4.0f);

	pos_.y = appearStartY_ + (appearTargetY_ - appearStartY_) * eased;

	// 落下中の前傾とひねり
	rot_.x = (1.0f - t) * 0.45f;
	rot_.y += 0.18f;
	rot_.z = std::sinf(t * 9.42477f) * 0.12f;

	// 着地前後の膨らみ
	float impact = std::sinf(t * 3.14159f);
	scale_ = {
	baseScale_.x + impact * 0.12f,
	baseScale_.y - impact * 0.08f,
	baseScale_.z + impact * 0.12f
	};

	if (appearTimer_ > 0) {
		// ボスの巨体に合わせて、毎フレーム大量の炎を出す
		for (int i = 0; i < 15; i++) {
			Vector3 trailPos = {
				pos_.x + (rand() % 31 - 15) * 0.1f, // ボスの体幅に合わせて散らす
				pos_.y + (rand() % 21 - 10) * 0.1f,
				pos_.z + (rand() % 31 - 15) * 0.1f
			};

			// 落下スピードを強調するため、パーティクルを「上方向」に勢いよく流す
			Vector3 trailVel = {
				(rand() % 11 - 5) * 0.02f,
				0.3f + (rand() % 10) * 0.05f, // 🌟 上へ向かって尾を引く！
				(rand() % 11 - 5) * 0.02f
			};

			// 大気摩擦で燃えているような 濃いオレンジ と 赤色
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 1.0f, 0.3f, 0.0f, 0.8f } : Vector4{ 1.0f, 0.1f, 0.0f, 0.8f };

			float scale = 0.8f + (rand() % 10) * 0.1f;

			// 少し長めに画面に残して、綺麗な軌跡（線）を作る
			GPUParticleManager::GetInstance()->Emit(trailPos, trailVel, 0.8f, scale, color);
		}
	}

	if (appearTimer_ <= 0) {
		pos_.y = appearTargetY_;
		rot_.x = 0.0f;
		rot_.z = 0.0f;

		Vector3 dir = {
			playerPos_.x - pos_.x,
			0.0f,
			playerPos_.z - pos_.z
		};

		if (Length(dir) > 0.01f) {
			rot_.y = std::atan2f(dir.x, dir.z);
		}

		Vector3 landPos = pos_;
		landPos.y = appearTargetY_ - 1.5f;

		for (int i = 0; i < 120; i++) {
			float angle = static_cast<float>(rand()) / RAND_MAX * 3.141592f * 2.0f;
			float speed = 0.1f + (rand() % 10) * 0.015f;
			Vector3 smokeVel = {
				std::sinf(angle) * speed,
				0.02f + (rand() % 10) * 0.01f, // 少しだけ上に浮かせる
				std::cosf(angle) * speed
			};
			// 暗い床でもハッキリ見える「明るいグレーと白」の土埃
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 0.8f, 0.8f, 0.8f, 0.8f } : Vector4{ 0.6f, 0.6f, 0.6f, 0.8f };
			// サイズを元の2〜3倍に超巨大化！
			float scale = 3.0f + (rand() % 20) * 0.1f;

			// 寿命を長く（1.5秒）してしっかり見せる
			GPUParticleManager::GetInstance()->Emit(landPos, smokeVel, 1.5f, scale, color);
		}

		for (int i = 0; i < 60; i++) {
			float angle = (3.141592f * 2.0f / 60.0f) * i; // 綺麗な円に並べる
			float speed = 0.4f; // 光の輪は速く広げる
			Vector3 ringVel = {
				std::sinf(angle) * speed,
				0.0f,
				std::cosf(angle) * speed
			};
			GPUParticleManager::GetInstance()->Emit(landPos, ringVel, 0.8f, 1.5f, { 1.0f, 1.0f, 0.8f, 1.0f });
		}
		for (int i = 0; i < 40; i++) {
			Vector3 sparkVel = {
				(rand() % 21 - 10) * 0.05f,
				(rand() % 10) * 0.1f + 0.1f, // 上に強く跳ねる
				(rand() % 21 - 10) * 0.05f
			};
			float scale = 0.5f + (rand() % 5) * 0.1f;
			GPUParticleManager::GetInstance()->Emit(landPos, sparkVel, 1.0f, scale, { 1.0f, 0.6f, 0.1f, 1.0f });
		}
	
		state_ = State::Idle;
		thinkTimer_ = 20;
	}
}
void Boss::SetStun(int durationFrames) {
	// スタン中は新たなスタンを受け付けない（無限スタン防止）
	if ( isStunned_ || durationFrames <= 0 ) return;

	// ---- 攻撃を完全中断 ----
	isCasting_       = false;
	castTimer_       = 0;
	isActionLocked_  = false;  // ビーム追跡など「行動ロック」も解除
	actionLockTimer_ = 0;
	cardUseRequest_  = false;  // 発動リクエストが残っていても無効化

	// UseCard ステートのまま解除されると再発動するので Chase に戻す
	if ( state_ == State::UseCard ) {
		state_       = State::Chase;
		thinkTimer_  = 15;       // 少し待ってから次の行動を選ぶ
	}

	// ---- ポーズ・スケールをリセット ----
	ResetPose();                 // rot_.x / rot_.z / scale_ を基準値に戻す
	preStunYaw_ = rot_.y;        // 現在の向きを保存（解除後に復元）

	isStunned_  = true;
	stunTimer_  = durationFrames;
}
void Boss::DecideNextState() {
	if (state_ == State::Appear) {
		return;
	}

	if (state_ == State::UseCard && isCasting_) {
		return;
	}

	Vector3 toPlayer = {
		playerPos_.x - pos_.x,
		0.0f,
		playerPos_.z - pos_.z
	};

	float playerDist = Length(toPlayer);

	if (state_ == State::UseCard) {
		return;
	}

	if (playerDist <= chaseRange_) {
		state_ = State::Chase;
	}
	else {
		state_ = State::Idle;
	}
}

void Boss::UpdateIdle() {
	Vector3 dir = {
		playerPos_.x - pos_.x,
		0.0f,
		playerPos_.z - pos_.z
	};

	float dist = Length(dir);
	if (thinkTimer_ > 0) {
		return;
	}

	if (dist <= chaseRange_) {
		state_ = State::Chase;
		thinkTimer_ = 0;
	}
}

void Boss::UpdateChase() {
	if (isCasting_) {
		isCasting_ = false;
		scale_ = baseScale_; // 分裂ボスでも元サイズに戻す

		rot_.x = 0.0f;
		rot_.z = 0.0f;
	}

	Vector3 dir = {
		playerPos_.x - pos_.x,
		0.0f,
		playerPos_.z - pos_.z
	};

	float dist = Length(dir);

	// 相方ボスと重ならないように一定距離を保つ
	Vector3 separation{ 0.0f, 0.0f, 0.0f };
	if (isSplitBehaviorEnabled_ && hasPartnerPosition_) {
		Vector3 toPartner = {
			pos_.x - partnerPos_.x,
			0.0f,
			pos_.z - partnerPos_.z
		};

		float partnerDist = Length(toPartner);
		const float keepDistance = 4.0f; // ボス同士の最低間隔

		if (partnerDist > 0.001f && partnerDist < keepDistance) {
			Vector3 away = Normalize(toPartner);
			float pushPower = (keepDistance - partnerDist) * 0.08f;
			separation = away * pushPower;
		}
	}


	const bool isEnraged = hp_ <= (maxHP_ / 2);
	const float moveSpeed = isEnraged ? 0.11f : 0.08f;

	float cardStartRange = isEnraged ? 28.0f : 24.0f;
	float stopRange = isEnraged ? 2.4f : 3.0f;

	if (isSplitBehaviorEnabled_) {
		const bool useMeleeRole = forceMeleeMode_ || combatRole_ == CombatRole::Melee;
		cardStartRange = useMeleeRole ? (isEnraged ? 14.0f : 12.0f) : (isEnraged ? 30.0f : 26.0f);
		stopRange = useMeleeRole ? (isEnraged ? 2.2f : 2.8f) : (isEnraged ? 8.0f : 9.5f);
	}


	if (dist > 0.01f) {
		Vector3 normDir = Normalize(dir);
		rot_.y = std::atan2f(normDir.x, normDir.z);
	}

	// 中距離から技に入れるようにして、追いかけるだけの時間を短くする。
	if (attackIntervalTimer_ <= 0 && cardCooldownTimer_ <= 0 && dist <= cardStartRange) {
		state_ = State::UseCard;
		thinkTimer_ = 0;
		return;
	}

	if (dist > stopRange) {
		Vector3 normDir = Normalize(dir);
		pos_ += normDir * moveSpeed;
	}

	// 相方との重なり防止を最後に足す
	pos_ += separation;

}

void Boss::UpdateUseCard() {
	// HPが半分以下なら怒り状態
	const bool isEnraged = hp_ <= (maxHP_ / 2);

	const bool useMeleeRole = forceMeleeMode_ || combatRole_ == CombatRole::Melee;

	// =========================================================
	// 1. 詠唱開始前（ここで「どのカードを使うか」を抽選で決める！）
	// =========================================================
	if (!isCasting_) {
		Vector3 startDir = {
			playerPos_.x - pos_.x,
			0.0f,
			playerPos_.z - pos_.z
		};
		float dist = Length(startDir);

		std::vector<Card> candidates;
		auto addWeightedCard = [&](int cardId, int weight) {
			if (weight <= 0) return;
			for (const auto& card : heldCards_) {
				if (card.id != cardId || !IsCardReady(card.id)) continue;
				for (int i = 0; i < weight; ++i) {
					candidates.push_back(card);
				}
				return;
			}
			};

		if (!isSplitBehaviorEnabled_) {
			// 5階など通常ボスは今まで通り
			if (dist < 5.5f) {
				addWeightedCard(101, 5);
				addWeightedCard(102, isEnraged ? 2 : 1);
				if (isEnraged) addWeightedCard(104, 3);
				addWeightedCard(103, 1);
			} else if (dist < 14.0f) {
				addWeightedCard(102, isEnraged ? 7 : 5);
				if (isEnraged) addWeightedCard(104, 6);
				addWeightedCard(103, isEnraged ? 3 : 2);
				addWeightedCard(101, 2);
			} else {
				addWeightedCard(102, isEnraged ? 8 : 6);
				if (isEnraged) addWeightedCard(104, 7);
				addWeightedCard(103, isEnraged ? 5 : 4);
			}
		} else {
			// 10階の分裂ボスだけ役割付き重み付け
			if (dist < 5.5f) {
				if (useMeleeRole) {
					addWeightedCard(107, isEnraged ? 2 : 1);
					addWeightedCard(105, isEnraged ? 2 : 1);
				} else {
					addWeightedCard(106, isEnraged ? 2 : 1);
				}
			} else if (dist < 14.0f) {
				if (useMeleeRole) {
					addWeightedCard(107, isEnraged ? 2 : 1);
					addWeightedCard(105, isEnraged ? 5 : 4);
				} else {
					addWeightedCard(101, 1);
					addWeightedCard(102, isEnraged ? 7 : 6);
					addWeightedCard(104, isEnraged ? 6 : 5);
				}
			} else {
				if (useMeleeRole) {
					addWeightedCard(107, isEnraged ? 2 : 1);
					addWeightedCard(105, 1);
				} else {
					addWeightedCard(102, isEnraged ? 8 : 7);
					addWeightedCard(104, isEnraged ? 7 : 6);
				}
			}
		}



		// 連続で同じカードにならないようにする処理
		bool hasDifferentCard = false;
		for (const auto& card : candidates) {
			if (card.id != lastUsedCardId_) {
				hasDifferentCard = true;
				break;
			}
		}
		if (hasDifferentCard) {
			candidates.erase(
				std::remove_if(candidates.begin(), candidates.end(),
					[&](const Card& card) { return card.id == lastUsedCardId_; }
				),
				candidates.end()
			);
		}

		// 候補が空なら、使えるものをとにかく入れる
		if (candidates.empty()) {
			for (const auto& card : heldCards_) {
				if (IsCardReady(card.id)) candidates.push_back(card);
			}
		}

		// それでも使えるカードがない場合はチェイス(追跡)に戻る
		if (candidates.empty()) {
			state_ = State::Chase;
			thinkTimer_ = 20;
			scale_ = baseScale_; // 個体ごとの元サイズに戻す

			rot_.x = 0.0f;
			rot_.z = 0.0f;
			return;
		}

		// ★ 確率の配列の中からランダムで1つ決定！
		static std::random_device rd;
		static std::mt19937 mt(rd());
		std::uniform_int_distribution<int> distCard(0, static_cast<int>(candidates.size()) - 1);

		selectedCard_ = candidates[distCard(mt)];

		// 詠唱タイマーをセットして詠唱開始
		isCasting_ = true;
		castDurationCurrent_ = GetCastTimeForCard(selectedCard_.id, isEnraged);
		castTimer_ = castDurationCurrent_;
		return;
	}

	// =========================================================
	// 2. 詠唱中の処理（プレイヤーの方を向いてポーズを取る）
	// =========================================================
	if (castTimer_ > 0) {
		castTimer_--;

		const bool isBeamCard = selectedCard_.id == 104;
		const int beamAimLockFrames = isEnraged ? 35 : 45;
		if (!isBeamCard || castTimer_ > beamAimLockFrames) {
			Vector3 dir = {
				playerPos_.x - pos_.x,
				0.0f,
				playerPos_.z - pos_.z
			};
			if (Length(dir) > 0.01f) {
				rot_.y = std::atan2f(dir.x, dir.z);
			}
		}

		float t = 1.0f - static_cast<float>(castTimer_) / static_cast<float>(castDurationCurrent_);
		ApplyCastingPose(t);

		if (selectedCard_.id == 104) {
			// コア位置（ボスの胸元）
			Vector3 corePos = { pos_.x, pos_.y + 2.0f, pos_.z };
			int elapsedFrames = castDurationCurrent_ - castTimer_;

			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			// ① コアに溜まる光球（その場でほぼ静止・詠唱が進むほど大きく輝く）
			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			{
				int coreCount = 2 + static_cast<int>(t * 7.0f); // 2〜9個に増える
				for (int i = 0; i < coreCount; i++) {
					Vector3 clusterPos = {
						corePos.x + (rand() % 9 - 4) * 0.1f,
						corePos.y + (rand() % 9 - 4) * 0.1f,
						corePos.z + (rand() % 9 - 4) * 0.1f
					};
					Vector3 clusterVel = {
						(rand() % 5 - 2) * 0.008f,
						(rand() % 5 - 2) * 0.008f,
						(rand() % 5 - 2) * 0.008f
					};
					float clusterScale = 0.6f + t * 1.6f; // 0.6 → 2.2 に成長
					float alpha = 0.55f + t * 0.45f;
					Vector4 color = (rand() % 2 == 0)
						? Vector4{ 1.0f, 1.0f, 0.65f, alpha }
						: Vector4{ 1.0f, 1.0f, 1.0f,  alpha };
					GPUParticleManager::GetInstance()->Emit(clusterPos, clusterVel, 0.14f, clusterScale, color);
				}
			}

			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			// ② 螺旋状にコアへ引き込まれるパーティクル（渦巻き回収）
			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			{
				const int spiralCount = 4;
				float orbitRadius = 4.5f * (1.0f - t * 0.75f); // 4.5 → 1.1 に縮む
				for (int i = 0; i < spiralCount; i++) {
					float angle = static_cast<float>(elapsedFrames) * 0.18f
						+ (3.14159f * 2.0f / spiralCount) * i;
					Vector3 orbitPos = {
						corePos.x + std::cosf(angle) * orbitRadius,
						corePos.y + (rand() % 5 - 2) * 0.18f,
						corePos.z + std::sinf(angle) * orbitRadius
					};
					// 接線方向（旋回）＋コア方向（収束）の合成速度
					float tanSpeed = 0.10f;
					float inSpeed  = 0.06f + t * 0.10f;
					Vector3 orbitVel = {
						-std::sinf(angle) * tanSpeed + (corePos.x - orbitPos.x) * inSpeed,
						0.01f,
						 std::cosf(angle) * tanSpeed + (corePos.z - orbitPos.z) * inSpeed
					};
					float orbitScale = 0.3f + t * 0.5f;
					GPUParticleManager::GetInstance()->Emit(orbitPos, orbitVel, 0.16f, orbitScale, { 1.0f, 1.0f, 0.4f, 0.9f });
				}
			}

			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			// ③ 遠方から吸い込まれるパーティクル（詠唱が進むほど数が増える）
			// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
			{
				int suckCount = 3 + static_cast<int>(t * 5.0f); // 3〜8個
				for (int i = 0; i < suckCount; i++) {
					float angleY = static_cast<float>(rand()) / RAND_MAX * 3.141592f * 2.0f;
					float angleX = static_cast<float>(rand()) / RAND_MAX * 3.141592f * 2.0f;
					float radius = 6.0f + (rand() % 60) * 0.1f;

					Vector3 spawnPos = {
						corePos.x + std::sinf(angleY) * std::cosf(angleX) * radius,
						corePos.y + std::sinf(angleX) * radius,
						corePos.z + std::cosf(angleY) * std::cosf(angleX) * radius
					};

					Vector3 toCore = {
						corePos.x - spawnPos.x,
						corePos.y - spawnPos.y,
						corePos.z - spawnPos.z
					};
					// 速度を少し速くして確実にコアに到達させる
					Vector3 particleVel = { toCore.x * 0.055f, toCore.y * 0.055f, toCore.z * 0.055f };

					Vector4 color = (rand() % 2 == 0)
						? Vector4{ 1.0f, 1.0f, 0.5f, 1.0f }
						: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
					float scale = 0.2f + (t * 0.6f) + (rand() % 5) * 0.1f;
					GPUParticleManager::GetInstance()->Emit(spawnPos, particleVel, 0.32f, scale, color);
				}
			}
		} else {
			// ======== 魔法陣リングパーティクル（ビーム以外のカード詠唱中） ========
			// カード距離タイプに応じた色を決める (近=土茶 / 中=黄 / 遠=青)
			Vector4 circleColor;
			switch (selectedCard_.attackRangeType) {
			case CardAttackRangeType::Melee: circleColor = { 0.6f, 0.35f, 0.1f, 0.9f }; break; // 近距離: 土・茶
			case CardAttackRangeType::Mid:   circleColor = { 1.0f, 0.85f, 0.1f, 0.9f }; break; // 中距離: 黄
			case CardAttackRangeType::Long:  circleColor = { 0.2f, 0.55f, 1.0f, 0.9f }; break; // 遠距離: 青
			default:                         circleColor = { 0.5f, 0.20f, 0.9f, 0.9f }; break;
			}

			// 詠唱の進捗に合わせて魔法陣が広がる (半径 0.5 → 3.0)
			float ringRadius = 0.5f + t * 2.5f;
			float baseY = pos_.y + 0.1f;
			int elapsedFrames = castDurationCurrent_ - castTimer_;

			// 3粒子を等間隔に、時間とともに回転させながら配置
			for (int i = 0; i < 3; i++) {
				float angle = (3.14159f * 2.0f / 3.0f) * static_cast<float>(i)
					+ static_cast<float>(elapsedFrames) * 0.15f;
				Vector3 ringPos = {
					pos_.x + std::cosf(angle) * ringRadius,
					baseY,
					pos_.z + std::sinf(angle) * ringRadius
				};
				Vector3 ringVel = {
					(rand() % 7 - 3) * 0.01f,
					0.02f + (rand() % 4) * 0.01f,
					(rand() % 7 - 3) * 0.01f
				};
				float circleScale = 0.3f + t * 0.5f;
				GPUParticleManager::GetInstance()->Emit(ringPos, ringVel, 0.25f, circleScale, circleColor);
			}
		}

		return;
	}

	// =========================================================
	// 3. 詠唱完了・カード発動処理
	// =========================================================
	scale_ = baseScale_; // 分裂ボスでも正しいサイズに戻す

	rot_.x = 0.0f;
	rot_.z = 0.0f;

	if (selectedCard_.id != 104) {
		Vector3 dir = {
			playerPos_.x - pos_.x,
			0.0f,
			playerPos_.z - pos_.z
		};
		if (Length(dir) > 0.01f) {
			rot_.y = std::atan2f(dir.x, dir.z);
		}
	}

	lastUsedCardId_ = selectedCard_.id;
	cardUseRequest_ = true;

	// ボスごとに持っている最小〜最大の範囲で次の攻撃待ち時間を決める
	static std::random_device rd;
	static std::mt19937 mt(rd());
	std::uniform_int_distribution<int> attackIntervalDist(
		attackIntervalMinFrames_,
		attackIntervalMaxFrames_
	);
	attackIntervalTimer_ = attackIntervalDist(mt);

	// クールダウン設定
	if (selectedCard_.id == 101) {
		cardCooldownTimer_ = isEnraged ? 28 : 38;
		StartCardCooldown(101, isEnraged ? 45 : 60);
	}
	else if (selectedCard_.id == 102) {
		cardCooldownTimer_ = isEnraged ? 24 : 34;
		StartCardCooldown(102, isEnraged ? 45 : 65);
	}
	else if (selectedCard_.id == 104) {
		// ビームのクールダウン
		cardCooldownTimer_ = isEnraged ? 42 : 56;
		StartCardCooldown(104, isEnraged ? 80 : 110);
	}
	else if (selectedCard_.id == 103) {
		cardCooldownTimer_ = isEnraged ? 48 : 64;
		StartCardCooldown(103, isEnraged ? 90 : 120);
	}
	else if (selectedCard_.id == 105) {
		cardCooldownTimer_ = isEnraged ? 20 : 30; // 10階近接カードの個別待ち時間
		StartCardCooldown(105, isEnraged ? 35 : 50); // 同じカードの連打を防ぐ
	}
	else if (selectedCard_.id == 106) {
		cardCooldownTimer_ = isEnraged ? 18 : 28; // ボス蹴りの個別待ち時間
		StartCardCooldown(106, isEnraged ? 35 : 50); // 同じ蹴りの連打を防ぐ
	}
	else if (selectedCard_.id == 107) {
		cardCooldownTimer_ = isEnraged ? 24 : 34; // ボス槍の個別待ち時間
		StartCardCooldown(107, isEnraged ? 45 : 60); // 同じ槍の連打を防ぐ
	}
	// 発動後の硬直とチェイスへの復帰
	state_ = State::Chase;
	thinkTimer_ = isEnraged ? 10 : 18;

	isActionLocked_ = true;
	actionLockTimer_ = GetRecoveryTimeForCard(selectedCard_.id, isEnraged);

	isCasting_ = false;
}

void Boss::UpdateEnrageEffect(){
	// 死んでいる時、登場中、またはHPが半分より多い場合はエフェクトを出さない
	if ( isDead_ || state_ == State::Appear || hp_ > ( maxHP_ / 2 ) ) {
		return;
	}

	// 激怒突入の瞬間（1回だけ）
	if ( !hasEnrageTriggered_ ) {
		hasEnrageTriggered_ = true;

		if ( isSplitBehaviorEnabled_ ) {
			// ===== 分裂ボス専用：青白電撃系 =====
			// ① 電撃の柱（青白）
			for ( int i = 0; i < 50; i++ ) {
				Vector3 p = {
					pos_.x + ( rand() % 9 - 4 ) * 0.2f,
					pos_.y + ( rand() % 5 ) * 0.3f,
					pos_.z + ( rand() % 9 - 4 ) * 0.2f
				};
				Vector3 v = {
					( rand() % 7 - 3 ) * 0.04f,
					0.5f + ( rand() % 20 ) * 0.08f,
					( rand() % 7 - 3 ) * 0.04f
				};
				Vector4 c = ( rand() % 2 == 0 )
					? Vector4{ 0.4f, 0.8f, 1.0f, 1.0f }
					: Vector4{ 0.7f, 0.95f, 1.0f, 1.0f };
				GPUParticleManager::GetInstance()->Emit(p, v, 0.7f, 1.0f + ( rand() % 8 ) * 0.2f, c);
			}
			// ② 衝撃波リング 3段（青）
			for ( int ring = 0; ring < 3; ring++ ) {
				float height = 0.3f + ring * 1.2f;
				float speed = 0.7f + ring * 0.15f;
				float size = 2.0f - ring * 0.3f;
				for ( int i = 0; i < 24; i++ ) {
					float a = ( 3.14159f * 2.0f / 24.0f ) * i;
					Vector3 rv = { std::sinf(a) * speed, 0.0f, std::cosf(a) * speed };
					GPUParticleManager::GetInstance()->Emit(
						{ pos_.x, pos_.y + height, pos_.z }, rv, 0.45f, size, { 0.2f, 0.6f, 1.0f, 1.0f });
				}
			}
			// ③ 超大フラッシュ（白→青の2連）
			GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 2.5f, pos_.z }, { 0,0,0 }, 0.12f, 8.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
			GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 2.5f, pos_.z }, { 0,0,0 }, 0.5f,  6.0f, { 0.2f, 0.5f, 1.0f, 0.75f });
			// ④ 冷気の霧（余韻）
			for ( int i = 0; i < 25; i++ ) {
				Vector3 sp = {
					pos_.x + ( rand() % 21 - 10 ) * 0.18f,
					pos_.y,
					pos_.z + ( rand() % 21 - 10 ) * 0.18f
				};
				Vector3 sv = { ( rand() % 11 - 5 ) * 0.04f, 0.12f + ( rand() % 10 ) * 0.04f, ( rand() % 11 - 5 ) * 0.04f };
				GPUParticleManager::GetInstance()->Emit(sp, sv, 1.5f, 1.4f, { 0.1f, 0.2f, 0.5f, 0.75f });
			}
		} else {
			// ===== 通常ボス専用：赤炎系（既存） =====
			// ① 真上に吹き上がる炎の柱
			for ( int i = 0; i < 60; i++ ) {
				Vector3 pillarPos = {
					pos_.x + ( rand() % 9 - 4 ) * 0.2f,
					pos_.y + ( rand() % 5 ) * 0.3f,
					pos_.z + ( rand() % 9 - 4 ) * 0.2f
				};
				Vector3 pillarVel = {
					( rand() % 7 - 3 ) * 0.04f,
					0.5f + ( rand() % 20 ) * 0.08f,
					( rand() % 7 - 3 ) * 0.04f
				};
				Vector4 color = ( rand() % 2 == 0 )
					? Vector4{ 1.0f, 0.15f, 0.0f, 1.0f }
					: Vector4{ 1.0f, 0.55f, 0.0f, 1.0f };
				float scale = 1.0f + ( rand() % 8 ) * 0.2f;
				GPUParticleManager::GetInstance()->Emit(pillarPos, pillarVel, 0.8f, scale, color);
			}
			// ② 衝撃波リング 3段
			for ( int ring = 0; ring < 3; ring++ ) {
				float height = 0.3f + ring * 1.2f;
				float speed = 0.7f + ring * 0.15f;
				float size = 2.2f - ring * 0.3f;
				for ( int i = 0; i < 24; i++ ) {
					float ringAngle = ( 3.14159f * 2.0f / 24.0f ) * i;
					Vector3 ringVel = { std::sinf(ringAngle) * speed, 0.0f, std::cosf(ringAngle) * speed };
					GPUParticleManager::GetInstance()->Emit(
						{ pos_.x, pos_.y + height, pos_.z }, ringVel, 0.45f, size, { 1.0f, 0.08f, 0.0f, 1.0f });
				}
			}
			// ③ 超大フラッシュ（白→赤）
			GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 2.5f, pos_.z }, { 0,0,0 }, 0.12f, 8.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
			GPUParticleManager::GetInstance()->Emit({ pos_.x, pos_.y + 2.5f, pos_.z }, { 0,0,0 }, 0.5f,  6.0f, { 1.0f, 0.05f, 0.0f, 0.75f });
			// ④ 足元から這い上がる黒煙
			for ( int i = 0; i < 25; i++ ) {
				Vector3 smokePos = {
					pos_.x + ( rand() % 21 - 10 ) * 0.18f,
					pos_.y,
					pos_.z + ( rand() % 21 - 10 ) * 0.18f
				};
				Vector3 smokeVel = { ( rand() % 11 - 5 ) * 0.04f, 0.12f + ( rand() % 10 ) * 0.04f, ( rand() % 11 - 5 ) * 0.04f };
				GPUParticleManager::GetInstance()->Emit(smokePos, smokeVel, 1.5f, 1.4f, { 0.1f, 0.0f, 0.0f, 0.85f });
			}
		}
	}

	// 3フレームに1回だけオーラを出す（GPU負荷軽減）
	if ( animationTimer_ % 3 != 0 ) return;
	for ( int i = 0; i < 3; i++ ) {
		Vector3 auraPos = {
			pos_.x + ( rand() % 31 - 15 ) * 0.1f,
			pos_.y + ( rand() % 21 ) * 0.1f,
			pos_.z + ( rand() % 31 - 15 ) * 0.1f
		};
		Vector3 auraVel = {
			( rand() % 11 - 5 ) * 0.01f,
			0.05f + ( rand() % 10 ) * 0.01f,
			( rand() % 11 - 5 ) * 0.01f
		};
		Vector4 color;
		if ( isSplitBehaviorEnabled_ ) {
			// 分裂ボス：青白電光オーラ
			color = ( rand() % 2 == 0 )
				? Vector4{ 0.3f, 0.7f, 1.0f, 0.55f }
				: Vector4{ 0.6f, 0.9f, 1.0f, 0.35f };
		} else {
			// 通常ボス：赤炎オーラ（既存）
			color = ( rand() % 2 == 0 )
				? Vector4{ 0.8f, 0.0f, 0.0f, 0.6f }
				: Vector4{ 1.0f, 0.2f, 0.0f, 0.4f };
		}
		float scale = 0.8f + ( rand() % 5 ) * 0.1f;
		GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.8f, scale, color);
	}
}

void Boss::UpdateDeathAnimation() {
	if (deathAnimationTimer_ <= 0) {
		return;
	}

	deathAnimationTimer_--;

	float t = 1.0f - static_cast<float>(deathAnimationTimer_) / static_cast<float>(deathAnimationDuration_);
	t = std::clamp(t, 0.0f, 1.0f);
	float ease = 1.0f - std::pow(1.0f - t, 3.0f);
	float shake = std::sinf(t * 56.0f) * (1.0f - t);

	rot_.x = ease * 1.25f;
	rot_.z = shake * 0.18f;
	pos_.y = deathStartY_ - ease * 0.45f;

	float shrink = 1.0f - ease * 0.28f;
	scale_ = {
		baseScale_.x * (1.0f + std::sinf(t * 3.14159f) * 0.18f),
		baseScale_.y * shrink,
		baseScale_.z * shrink
	};

	if (deathAnimationTimer_ % 4 == 0) {
		for (int i = 0; i < 8; i++) {
			Vector3 smokePos = {
				pos_.x + (rand() % 41 - 20) * 0.08f,
				pos_.y + 0.8f + (rand() % 21) * 0.05f,
				pos_.z + (rand() % 41 - 20) * 0.08f
			};
			Vector3 smokeVel = {
				(rand() % 11 - 5) * 0.03f,
				0.04f + (rand() % 10) * 0.01f,
				(rand() % 11 - 5) * 0.03f
			};
			GPUParticleManager::GetInstance()->Emit(smokePos, smokeVel, 0.6f, 0.7f, { 1.0f, 0.45f, 0.15f, 0.85f });
		}
	}
}
Card Boss::GetRandomDropCard() const {
	if (heldCards_.empty()) {
		return Card{ -1, "", 0 };
	}

	static std::random_device rd;
	static std::mt19937 mt(rd());
	std::uniform_int_distribution<int> distCard(0, static_cast<int>(heldCards_.size()) - 1);

	return heldCards_[distCard(mt)];
}

void Boss::TakeDamage(int damage) {
	if (isDead_) {
		return;
	}

	// 詠唱中に殴られたらキャンセルする
	if (state_ == State::UseCard) {
		cardUseRequest_ = false;
		isActionLocked_ = false;
		actionLockTimer_ = 0;
		state_ = State::Chase;
		thinkTimer_ = 15;
		isCasting_ = false;
		scale_ = baseScale_; // ダメージで中断しても元サイズに戻す

		rot_.x = 0.0f;
		rot_.z = 0.0f;
	}

	hp_ -= damage;

	// ボス被弾時のカメラシェイク（プレイヤー被弾より小さめ）
	if ( camera_ ) {
		camera_->TriggerShake(0.06f, 6);
	}

	Vector3 bossHitCenter = { pos_.x, pos_.y + 1.2f, pos_.z };

	// 中心フラッシュ
	GPUParticleManager::GetInstance()->Emit(bossHitCenter, { 0, 0, 0 }, 0.08f, 4.5f, { 1.0f, 1.0f, 0.9f, 1.0f });
	GPUParticleManager::GetInstance()->Emit(bossHitCenter, { 0, 0, 0 }, 0.15f, 2.8f, { 1.0f, 0.8f, 0.2f, 1.0f });

	// 衝撃波リング
	for (int i = 0; i < 24; i++) {
		float angle = (3.141592f * 2.0f / 24.0f) * i;
		float speed = 0.6f + (rand() % 5) * 0.05f;
		Vector3 ringVel = { std::sinf(angle) * speed, 0.02f, std::cosf(angle) * speed };
		GPUParticleManager::GetInstance()->Emit(bossHitCenter, ringVel, 0.2f, 1.4f, { 1.0f, 1.0f, 0.7f, 1.0f });
	}

	// 飛び散りスパーク（ボスは広範囲）
	for (int i = 0; i < 30; i++) {
		Vector3 sparkVel = {
			(rand() % 11 - 5) * 0.18f,
			(rand() % 10) * 0.08f + 0.1f,
			(rand() % 11 - 5) * 0.18f
		};
		Vector3 sparkPos = {
			pos_.x + (rand() % 11 - 5) * 0.12f,
			pos_.y + 0.8f + (rand() % 10) * 0.12f,
			pos_.z + (rand() % 11 - 5) * 0.12f
		};
		float scale = 0.4f + (rand() % 5) * 0.09f;
		float orange = 0.35f + (rand() % 8) * 0.06f;
		GPUParticleManager::GetInstance()->Emit(sparkPos, sparkVel, 0.4f, scale, { 1.0f, orange, 0.05f, 1.0f });
	}

	isHit_ = true;
	hitTimer_ = hitDuration_;

	Vector3 hitDir = {
		pos_.x - playerPos_.x,
		0.0f,
		pos_.z - playerPos_.z
	};

	if (Length(hitDir) > 0.01f) {
		hitDir = Normalize(hitDir);
		knockbackVelocity_ = hitDir * 0.15f;
	}

	if (hp_ <= 0) {
		hp_ = 0;
		isDead_ = true;
		state_ = State::Dead;
		isHit_ = false;
		hitTimer_ = 0;
		knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
		deathAnimationTimer_ = deathAnimationDuration_;
		deathStartY_ = pos_.y;

		for (int i = 0; i < 80; i++) {
			Vector3 expPos = {
				pos_.x + (rand() % 41 - 20) * 0.1f,
				pos_.y + (rand() % 41) * 0.1f,
				pos_.z + (rand() % 41 - 20) * 0.1f
			};
			Vector3 expVel = {
				(rand() % 21 - 10) * 0.2f,
				(rand() % 21 - 10) * 0.2f + 0.3f,
				(rand() % 21 - 10) * 0.2f
			};
			Vector4 color = (rand() % 2 == 0) ? Vector4{ 1.0f, 0.2f, 0.0f, 1.0f } : Vector4{ 1.0f, 0.6f, 0.0f, 1.0f };

			// サイズを 1.5~3.0 -> 0.8~1.3 に縮小
			float scale = 0.8f + (rand() % 6) * 0.1f;
			// 寿命も少し短くしてサッと消えるように (1.5f -> 1.0f)
			GPUParticleManager::GetInstance()->Emit(expPos, expVel, 1.0f, scale, color);
		}

		for (int i = 0; i < 40; i++) {
			Vector3 smokePos = {
				pos_.x + (rand() % 21 - 10) * 0.2f,
				pos_.y + (rand() % 21) * 0.2f,
				pos_.z + (rand() % 21 - 10) * 0.2f
			};
			Vector3 smokeVel = {
				(rand() % 21 - 10) * 0.2f,
				(rand() % 11) * 0.2f,
				(rand() % 21 - 10) * 0.2f
			};
			float scale = 1.0f + (rand() % 6) * 0.1f;
			GPUParticleManager::GetInstance()->Emit(smokePos, smokeVel, 1.5f, scale, { 0.2f, 0.2f, 0.2f, 0.8f });
		}
	}
}
void Boss::SetActionLock(int frame) {
	isActionLocked_ = true;
	actionLockTimer_ = frame;
}

bool Boss::IsVisible() const {
	if (IsDeathAnimationPlaying()) {
		return true;
	}

	if (!isHit_) {
		return true;
	}

	return (hitTimer_ % 2) == 0;
}

bool Boss::IsCardReady(int cardId) const {
	auto it = cardCooldownTimers_.find(cardId);
	if (it == cardCooldownTimers_.end()) {
		return true;
	}
	return it->second <= 0;
}

void Boss::StartCardCooldown(int cardId, int time) {
	cardCooldownTimers_[cardId] = time;
}

int Boss::GetCardCooldownTime(int cardId) const {
	auto it = cardCooldownTimers_.find(cardId);
	if (it == cardCooldownTimers_.end()) {
		return 0;
	}
	return it->second;
}

void Boss::SetSpawnPosition(const Vector3& pos) {
	appearTargetY_ = pos.y;
	appearStartY_ = pos.y + kBossAppearDropHeight;
	pos_ = { pos.x, appearStartY_, pos.z };
}

void Boss::SetMaxHP(int maxHp) {
	// 最大HPが1未満にならないようにする
	if (maxHp < 1) {
		maxHp = 1;
	}

	maxHP_ = maxHp;

	// 現在HPが最大HPを超えていたら合わせる
	if (hp_ > maxHP_) {
		hp_ = maxHP_;
	}
}

void Boss::SetHP(int hp) {
	// 0未満にならないようにする
	if (hp < 0) {
		hp = 0;
	}

	// 最大HPを超えないようにする
	if (hp > maxHP_) {
		hp = maxHP_;
	}

	hp_ = hp;

	// 0なら死亡状態にしておく
	if (hp_ <= 0) {
		isDead_ = true;
		state_ = State::Dead;
	}
}
