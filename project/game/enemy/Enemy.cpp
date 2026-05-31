#include "game/enemy/Enemy.h"
#include "engine/math/VectorMath.h"
#include "game/card/CardDatabase.h"
#include <cmath>
#include <random>
#include "game/audio/GameSE.h"
#include "engine/particle/GPUParticleManager.h"
#include "game/particle/StunEffectManager.h"

using namespace VectorMath;

namespace{
	float RandomRange(float minValue, float maxValue){
		static std::random_device rd;
		static std::mt19937 mt(rd());
		std::uniform_real_distribution<float> dist(minValue, maxValue);
		return dist(mt);
	}

	struct EnemyTypeSetting{
		int baseCardId;
		int hp;
		int expReward;
		float speed;
		float chaseSpeed;
		Vector3 scale;
	};

	const EnemyTypeSetting& GetEnemyTypeSetting(Enemy::Type type){
		static const EnemyTypeSetting normal { 1, 3, 1, 0.07f, 0.12f, { 1.0f, 1.0f, 1.0f } };
		static const EnemyTypeSetting fast { 10, 2, 1, 0.09f, 0.15f, { 0.85f, 0.85f, 0.85f } };
		static const EnemyTypeSetting ranged { 2, 2, 2, 0.06f, 0.10f, { 0.95f, 0.95f, 0.95f } };
		static const EnemyTypeSetting heavy { 15, 5, 2, 0.045f, 0.085f, { 1.22f, 1.22f, 1.22f } };

		switch ( type ) {
		case Enemy::Type::Fast:
			return fast;
		case Enemy::Type::Ranged:
			return ranged;
		case Enemy::Type::Heavy:
			return heavy;
		case Enemy::Type::Normal:
		default:
			return normal;
		}
	}
}

void Enemy::Initialize(){
	pos_ = { 5.0f, 0.0f, 5.0f };
	rot_ = { 0.0f, 0.0f, 0.0f };
	scale_ = { 1.0f, 1.0f, 1.0f };

	startX_ = pos_.x;
	state_ = State::Patrol;
	baseCard_ = CardDatabase::GetCardData(1); // 固定の基本カード
	hasPickupCard_ = false;
	pickupCard_ = { -1, "", 0 };
	currentUseCard_ = { -1, "", 0 };

	hp_ = 3;                 // 敵HP初期化
	isDead_ = false;         // 死亡状態リセット
	isActionLocked_ = false; // 行動ロック解除
	actionLockTimer_ = 0;    // ロック残り時間
	thinkTimer_ = 0;         // 思考タイマー初期化

	isHit_ = false;                           // ヒット状態リセット
	hitTimer_ = 0;                            // ヒット時間リセット
	hitStunTimer_ = 0;                        // 被弾硬直時間リセット
	knockbackVelocity_ = { 0.0f, 0.0f, 0.0f }; // ノックバック速度初期化

	cardUseRequest_ = false; // カード使用発生フラグ初期化
	isCasting_ = false;
	castTimer_ = 0;
	strafeDirection_ = 1;
	cardCooldownTimer_ = 0; // カード使用クールダウン初期化

	hasTargetCard_ = false; // 目標カードなし
	canSeePlayer_ = false;
	hasNavigationTarget_ = false;
	navigationTargetPos_ = pos_;
	prevPos_ = pos_;        // 前フレーム位置初期化
	stuckTimer_ = 0;        // 詰まりタイマー初期化

	isFrozen_ = false; // 凍結状態リセット
	freezeTimer_ = 0;  // 凍結時間リセット

	// 巡回と索敵の初期値
	patrolAnchor_ = pos_;
	patrolTarget_ = pos_;
	patrolWaitTimer_ = 0;
	investigateTimer_ = 0;
	lastKnownPlayerPos_ = pos_;
	isBossRoom_ = false;
	SetType(Type::Normal);

	isStunned_ = false; // スタン状態リセット
	stunTimer_ = 0;     // スタン時間リセット
}

void Enemy::SetType(Type type){
	type_ = type;

	const EnemyTypeSetting& setting = GetEnemyTypeSetting(type_);
	baseCard_ = CardDatabase::GetCardData(setting.baseCardId);
	hp_ = setting.hp;
	expReward_ = setting.expReward;
	speed_ = setting.speed;
	chaseSpeed_ = setting.chaseSpeed;
	scale_ = setting.scale;
}

void Enemy::Update(){
	if ( isDead_ ) return; // 死亡していたら何もしない

	animationTimer_++; // 常にカウントアップ

	UpdateStatusEffects();

	// ★ スタンタイマーの更新
	if ( isStunned_ ) {
		stunTimer_--;

		//  たった一行で全演出が完了！
		StunEffectManager::Update(pos_, rot_, stunTimer_);

		if ( stunTimer_ <= 0 ) {
			isStunned_ = false;
			rot_.x = 0.0f;
		}
		return; // スタン中は以下の AI 思考や移動を全て飛ばす
	}

	// UseCard 以外へ遷移していたら詠唱は切る
	if ( state_ != State::UseCard && isCasting_ ) {
		isCasting_ = false;
		castTimer_ = 0;
	}

	cardUseRequest_ = false; // 毎フレーム使用要求は初期化

	// 拾ったカードは初回使用でタイマーが始まり、その間は使い続けられる
	if ( isActionLocked_ ) {
		actionLockTimer_--; // 残り時間を減らす
		if ( actionLockTimer_ <= 0 ) {
			isActionLocked_ = false; // 0になったら行動ロック解除
		}
		return; // ロック中は他の更新をしない
	}

	if ( cardCooldownTimer_ > 0 ) {
		cardCooldownTimer_--; // カード使用クールダウン減少
	}

	if ( isHit_ ) {
		pos_ += knockbackVelocity_;  // ノックバック移動
		knockbackVelocity_ *= 0.85f; // 徐々に減衰

		hitTimer_--; // ヒット表示残り時間減少
		if ( hitTimer_ <= 0 ) {
			isHit_ = false; // ヒット表示終了
		}
	}

	if ( hitStunTimer_ > 0 ) {
		hitStunTimer_--; // 被弾硬直時間減少

		if ( hitStunTimer_ <= 0 ) {
			knockbackVelocity_ = { 0.0f, 0.0f, 0.0f }; // 硬直終了時に速度初期化
		}

		prevPos_ = pos_; // 現在位置を保存
		return;          // 硬直中はAI更新しない
	}

	bool isPatrolWaiting = ( state_ == State::Patrol && patrolWaitTimer_ > 0 );
	if ( isPatrolWaiting ) {
		// 巡回先を決める待機は意図した停止なので、詰まり扱いしない
		stuckTimer_ = 0;
	} else if ( Length(pos_ - prevPos_) < stuckDistanceThreshold_ ) {
		stuckTimer_++; // ほとんど動いていなければ加算
	} else {
		stuckTimer_ = 0;
	}
	prevPos_ = pos_;

	if ( thinkTimer_ > 0 ) {
		thinkTimer_--; // 次の思考更新まで減らす
	}

	if ( thinkTimer_ <= 0 ) {
		DecideNextState();
		thinkTimer_ = 10; // 次の状態決定まで少し待つ
	}

	if ( isFrozen_ ) {
		freezeTimer_--; // 凍結残り時間減少
		if ( freezeTimer_ <= 0 ) {
			isFrozen_ = false; // 時間切れで凍結解除
		}
		return; // 凍結中は行動しない
	}

	switch ( state_ ) {
	case State::Patrol:
		UpdatePatrol();
		break;

	case State::MoveToCard:
		UpdateMoveToCard();
		break;

	case State::ChasePlayer:
		UpdateChasePlayer();
		break;

	case State::Investigate:
		UpdateInvestigate();
		break;

	case State::UseCard:
		UpdateUseCard();
		break;

	case State::Retreat:
		UpdateRetreat();
		break;
	}

	// デバフタイマー更新
	if ( isAttackDebuffed_ ) {
		attackDebuffTimer_--;
		if ( attackDebuffTimer_ <= 0 ) {
			isAttackDebuffed_ = false;
		}
	}
}

void Enemy::Freeze(int durationFrames){
	isFrozen_ = true;
	freezeTimer_ = durationFrames;
}

void Enemy::SetStun(int durationFrames){
	// スタン中は新たなスタンを受け付けない（無限スタン防止）
	if ( isStunned_ || durationFrames <= 0 ) return;

	isStunned_ = true;
	stunTimer_ = durationFrames;

	isCasting_ = false;
	castTimer_ = 0;
}

// 次の状態を決める
void Enemy::DecideNextState(){
	if ( isCasting_ ) {
		stuckTimer_ = 0; // 詠唱中は詰まり扱いしない
		return;
	}

	Vector3 toPlayer = {
		playerPos_.x - pos_.x,
		0.0f,
		playerPos_.z - pos_.z
	};

	float playerDist = Length(toPlayer);          // プレイヤーとの距離
	const float activeChaseRange = isBossRoom_ ? bossRoomChaseRange_ : chaseRange_;
	const int investigateFrames = isBossRoom_ ? bossRoomInvestigateFrames_ : 150;
	bool shouldKeepDistance = ShouldKeepDistanceForCurrentCard();
	float useRange = GetUseRangeForCurrentCard(); // 現在カードの射程
	float exitRange = useRange + 1.5f;            // 使用状態を維持する余裕距離
	float retreatEnter = GetRetreatEnterRangeForCurrentCard(); // 近すぎる判定距離

	// プレイヤーが追跡範囲にいる間は、最後に見た位置を更新しておく
	if ( canSeePlayer_ && playerDist <= activeChaseRange ) {
		lastKnownPlayerPos_ = playerPos_;
		investigateTimer_ = investigateFrames;
	} else if ( investigateTimer_ > 0 ) {
		investigateTimer_--;
	}

	// 詰まっていたら、その時の目的に応じて立て直す
	if ( IsStuck() ) {
		if ( state_ == State::Retreat ) {
			strafeDirection_ *= -1; // 離脱方向を反転
			state_ = State::Retreat;
		} else if ( state_ == State::ChasePlayer || state_ == State::Investigate ) {
			strafeDirection_ *= -1;
			state_ = State::Investigate; // 追跡が詰まったら最後の位置を調べ直す
		} else {
			patrolTarget_ = pos_;
			patrolWaitTimer_ = 0;
			state_ = State::Patrol;
		}
		thinkTimer_ = 20;
		stuckTimer_ = 0;
		return;
	}

	// 拾ったカードがない時は、初期カードの射程タイプに関係なくカードを拾いに行く
	if ( !HasUsablePickupCard() && hasTargetCard_ ) {
		state_ = State::MoveToCard;
		return;
	}

	// 拾いカードを持っている時の分岐
	if ( shouldKeepDistance ) {
		if ( !canSeePlayer_ ) {
			if ( state_ == State::UseCard || state_ == State::Retreat ) {
				isCasting_ = false;
				castTimer_ = 0;
			}
			state_ = ( investigateTimer_ > 0 ) ? State::Investigate : State::Patrol;
			return;
		}

		if ( state_ == State::UseCard ) {
			if ( playerDist < retreatEnter ) {
				state_ = State::Retreat;
			} else if ( playerDist > exitRange ) {
				state_ = ( investigateTimer_ > 0 ) ? State::Investigate : State::ChasePlayer;
			}
			return;
		}

		if ( state_ == State::Retreat ) {
			if ( playerDist >= retreatExitRange_ ) {
				state_ = State::UseCard;
			}
			return;
		}

		if ( playerDist < retreatEnter ) {
			state_ = State::Retreat;
		} else if ( playerDist <= useRange ) {
			state_ = State::UseCard;
		} else if ( canSeePlayer_ && playerDist <= activeChaseRange ) {
			state_ = State::ChasePlayer;
		} else if ( investigateTimer_ > 0 ) {
			state_ = State::Investigate;
		} else {
			state_ = State::Patrol;
		}

		return;
	}

	// プレイヤーとの距離に応じた分岐
	if ( canSeePlayer_ && playerDist <= useRange ) {
		state_ = State::UseCard;
	} else if ( canSeePlayer_ && playerDist <= activeChaseRange ) {
		state_ = State::ChasePlayer;
	} else if ( investigateTimer_ > 0 ) {
		state_ = State::Investigate;
	} else {
		state_ = State::Patrol;
	}
}

// 詰まり判定
bool Enemy::IsStuck() const{
	return stuckTimer_ >= stuckThreshold_; // 一定時間ほぼ動いていなければ詰まり
}

void Enemy::UpdatePatrol(){
	// 単純な左右往復だと不自然なので、近場の目標点を順番に歩かせる
	if ( patrolWaitTimer_ > 0 ) {
		patrolWaitTimer_--;
		return;
	}

	Vector3 toTarget = {
		patrolTarget_.x - pos_.x,
		0.0f,
		patrolTarget_.z - pos_.z
	};

	// 到着済みか、まだ目標が決まっていない時は新しい巡回先を決める
	if ( Length(toTarget) < 0.25f ) {
		// 今いる場所を新しい巡回基準にして、少しずつ別エリアにも流れていけるようにする
		patrolAnchor_ = pos_;

		Vector3 nextTarget = patrolAnchor_;
		for ( int i = 0; i < 6; i++ ) {
			nextTarget = {
				patrolAnchor_.x + RandomRange(-patrolTargetRadius_, patrolTargetRadius_),
				pos_.y,
				patrolAnchor_.z + RandomRange(-patrolTargetRadius_, patrolTargetRadius_)
			};

			if ( Length(nextTarget - patrolAnchor_) >= patrolTargetMinDistance_ ) {
				break;
			}
		}

		patrolTarget_ = nextTarget;
		patrolWaitTimer_ = 10;
		return;
	}

	Vector3 dir = Normalize(toTarget);
	pos_ += dir * speed_;
	rot_.y = std::atan2f(dir.x, dir.z);
}

void Enemy::UpdateMoveToCard(){
	Vector3 dir = {
		targetCardPos_.x - pos_.x,
		0.0f,
		targetCardPos_.z - pos_.z
	};

	if ( Length(dir) > 0.01f ) {
		dir = Normalize(dir);
		pos_ += dir * chaseSpeed_;           // カードへ移動
		rot_.y = std::atan2f(dir.x, dir.z);  // 進行方向を向く
	}
}

void Enemy::UpdateChasePlayer(){
	Vector3 targetPos = hasNavigationTarget_ ? navigationTargetPos_ : playerPos_;
	const float activeChaseRange = isBossRoom_ ? bossRoomChaseRange_ : chaseRange_;

	// 視界から外れた直後は、最後に見た位置の方へ寄る
	if ( Length(playerPos_ - pos_) > activeChaseRange && investigateTimer_ > 0 ) {
		targetPos = lastKnownPlayerPos_;
	}

	Vector3 dir = {
		targetPos.x - pos_.x,
		0.0f,
		targetPos.z - pos_.z
	};

	if ( Length(dir) > 0.01f ) {
		dir = Normalize(dir);
		pos_ += dir * chaseSpeed_;           // 追跡移動
		rot_.y = std::atan2f(dir.x, dir.z);  // 目標方向を向く
	}
}

void Enemy::UpdateInvestigate(){
	Vector3 dir = {
		lastKnownPlayerPos_.x - pos_.x,
		0.0f,
		lastKnownPlayerPos_.z - pos_.z
	};

	float dist = Length(dir);
	if ( dist < 0.4f ) {
		patrolWaitTimer_ = 15;
		state_ = State::Patrol;
		return;
	}

	if ( dist > 0.01f ) {
		dir = Normalize(dir);
		pos_ += dir * ( chaseSpeed_ * 0.85f ); // 追跡より少し慎重に移動
		rot_.y = std::atan2f(dir.x, dir.z);
	}
}

void Enemy::UpdateUseCard(){
	Vector3 dir = {
		playerPos_.x - pos_.x,
		0.0f,
		playerPos_.z - pos_.z
	};

	float dist = Length(dir);                    // プレイヤーとの距離
	float useRange = GetUseRangeForCurrentCard(); // 現在カードの射程

	if ( !canSeePlayer_ ) {
		state_ = ( investigateTimer_ > 0 ) ? State::Investigate : State::Patrol;
		thinkTimer_ = 0;
		isCasting_ = false;
		castTimer_ = 0;
		return;
	}

	// クールダウン中はカードを使わず、追跡または捜索へ戻る
	if ( cardCooldownTimer_ > 0 ) {
		state_ = ( investigateTimer_ > 0 ) ? State::Investigate : State::ChasePlayer;
		thinkTimer_ = 0;
		isCasting_ = false;
		castTimer_ = 0;
		return;
	}

	// 射程外に出ていたらいったん詠唱前に追跡へ戻す
	if ( !isCasting_ && dist > useRange + 1.5f ) {
		state_ = ( investigateTimer_ > 0 ) ? State::Investigate : State::ChasePlayer;
		thinkTimer_ = 0;
		return;
	}

	// プレイヤー方向を向く
	if ( dist > 0.01f ) {
		rot_.y = std::atan2f(dir.x, dir.z);
	}

	// 詠唱開始
	if ( !isCasting_ ) {
		isCasting_ = true;
		castTimer_ = castTime_;

		// 拾ったカードがあればそちらを使い、なければ基本カードを使う
		if ( HasUsablePickupCard() ) {
			currentUseCard_ = pickupCard_;
		} else {
			currentUseCard_ = baseCard_;
		}

		return;
	}

	// 詠唱中
	if ( castTimer_ > 0 ) {
		castTimer_--;

		// ======== 魔法陣リングパーティクル（詠唱中） ========
		if ( castTime_ > 0 ) {
			float t = 1.0f - static_cast<float>(castTimer_) / static_cast<float>(castTime_);

			// カード距離タイプに応じた魔法陣の色 (近=土茶 / 中=黄 / 遠=青)
			Vector4 circleColor;
			switch ( currentUseCard_.attackRangeType ) {
			case CardAttackRangeType::Melee: circleColor = { 1.0f, 0.12f, 0.08f, 0.9f }; break; // 近距離: 赤
			case CardAttackRangeType::Mid:   circleColor = { 1.0f, 0.85f, 0.1f, 0.9f }; break; // 中距離: 黄
			case CardAttackRangeType::Long:  circleColor = { 0.2f, 0.55f, 1.0f, 0.9f }; break; // 遠距離: 青
			default:                         circleColor = { 0.5f, 0.20f, 0.9f, 0.9f }; break;
			}

			// 詠唱の進捗に合わせてリングが広がる (半径 0.4 → 2.2)
			float ringRadius = 0.4f + t * 1.8f;
			float baseY = pos_.y + 0.1f;
			int elapsedFrames = castTime_ - castTimer_;

			// 2粒子を等間隔に、時間で回転させながら配置
			for ( int i = 0; i < 2; i++ ) {
				float angle = 3.14159f * static_cast<float>(i)
					+ static_cast<float>(elapsedFrames) * 0.2f;
				Vector3 ringPos = {
					pos_.x + std::cosf(angle) * ringRadius,
					baseY,
					pos_.z + std::sinf(angle) * ringRadius
				};
				Vector3 ringVel = {
					( rand() % 5 - 2 ) * 0.01f,
					0.015f + ( rand() % 3 ) * 0.01f,
					( rand() % 5 - 2 ) * 0.01f
				};
				float circleScale = 0.25f + t * 0.35f;
				GPUParticleManager::GetInstance()->Emit(ringPos, ringVel, 0.2f, circleScale, circleColor);
			}
		}

		return;
	}

	// カード使用確定
	cardUseRequest_ = true;
	cardCooldownTimer_ = cardCooldown_;
	isCasting_ = false;

	// 使用直後の硬直
	SetActionLock(8);

	// プレイヤー寄りに、初回使用をきっかけに一定時間だけ使い続けられるようにする
	// 使用後の行動
	if ( ShouldKeepDistanceForCurrentCard() ) {
		state_ = State::Retreat;
	} else {
		state_ = State::ChasePlayer;
	}

	thinkTimer_ = 5;
}

void Enemy::UpdateRetreat(){
	Vector3 dir = {
		pos_.x - playerPos_.x,
		0.0f,
		pos_.z - playerPos_.z
	}; // プレイヤーから離れる方向

	float dist = Length(dir); // プレイヤーとの距離
	float retreatEnter = GetRetreatEnterRangeForCurrentCard(); // 離脱開始距離

	// 十分離れたらカード使用へ戻す
	if ( dist >= retreatEnter + 0.5f ) {
		state_ = State::UseCard;
		thinkTimer_ = 0;
		return;
	}

	if ( dist > 0.01f ) {
		// 真後ろへ下がるだけだと壁に詰まりやすいので、横移動を混ぜる
		Vector3 backDir = Normalize(dir);
		Vector3 sideDir = {
			backDir.z * static_cast< float >( strafeDirection_ ),
			0.0f,
			-backDir.x * static_cast< float >( strafeDirection_ )
		};
		Vector3 moveDir = Normalize(backDir + sideDir * retreatStrafeStrength_);
		pos_ += moveDir * chaseSpeed_;
		rot_.y = std::atan2f(moveDir.x, moveDir.z);
	}
}

void Enemy::UpdateStatusEffects(){
	//  1. 凍結（Freeze）中の冷気エフェクト
	if ( isFrozen_ ) {
		// 体の周囲から、下に向かって冷気（白い煙・氷の粒）がこぼれ落ちる
		Vector3 auraPos = {
			pos_.x + ( rand() % 15 - 7 ) * 0.1f, // 体の幅に合わせて散らす
			pos_.y + 0.8f + ( rand() % 11 - 5 ) * 0.1f, // 少し高めの位置から
			pos_.z + ( rand() % 15 - 7 ) * 0.1f
		};
		Vector3 auraVel = {
			( rand() % 11 - 5 ) * 0.005f,
			-0.05f - ( rand() % 5 ) * 0.01f, // 🌟 下に向かって落ちる重たい冷気
			( rand() % 11 - 5 ) * 0.005f
		};

		// 氷のような水色と白を混ぜる
		Vector4 color = ( rand() % 2 == 0 ) ? Vector4 { 0.6f, 0.9f, 1.0f, 0.6f } : Vector4 { 1.0f, 1.0f, 1.0f, 0.6f };
		float scale = 0.3f + ( rand() % 4 ) * 0.1f;

		GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.5f, scale, color);
	}

	// ☠️ 2. 攻撃力ダウン中の毒々しいオーラ
	if ( isAttackDebuffed_ ) {
		// 体の足元〜中心から、上に向かって不気味な泡や煙がフワフワ昇る
		Vector3 auraPos = {
			pos_.x + ( rand() % 11 - 5 ) * 0.1f,
			pos_.y + 0.2f + ( rand() % 11 ) * 0.05f, // 足元付近から
			pos_.z + ( rand() % 11 - 5 ) * 0.1f
		};
		Vector3 auraVel = {
			( rand() % 11 - 5 ) * 0.01f,
			0.05f + ( rand() % 5 ) * 0.01f, // 🌟 上に向かってフワフワ昇る
			( rand() % 11 - 5 ) * 0.01f
		};

		// 毒々しい紫色
		Vector4 color = { 0.6f, 0.2f, 0.8f, 0.6f };
		float scale = 0.4f + ( rand() % 4 ) * 0.1f;

		GPUParticleManager::GetInstance()->Emit(auraPos, auraVel, 0.6f, scale, color);
	}

	// ★ スタンタイマーの更新
	if ( isStunned_ ) {
		stunTimer_--;

		if ( stunTimer_ <= 0 ) {
			isStunned_ = false;
			// 🌟 姿勢（回転）を真っ直ぐに戻す
			rot_.x = 0.0f;
		}
		return; // スタン中は以下の AI 思考や移動を全て飛ばす
	}
}
void Enemy::TakeDamage(int damage){
	if ( isDead_ ) return;
	GameSE::AttackHit();
	if ( isDead_ ) return; // 既に死亡していたら無視

	if ( state_ == State::UseCard ) {
		cardUseRequest_ = false;     // カード使用要求をキャンセル
		isActionLocked_ = false;     // 行動ロック解除
		actionLockTimer_ = 0;        // ロック残り時間リセット
		state_ = State::ChasePlayer; // 被弾したら追跡へ戻す
	}

	hp_ -= damage;                    // HP減少

	Vector3 hitDir = {
			pos_.x - playerPos_.x,
			0.0f,
			pos_.z - playerPos_.z
	}; // プレイヤーから敵への方向

	if ( Length(hitDir) > 0.01f ) {
		hitDir = Normalize(hitDir);
		knockbackVelocity_ = hitDir * 0.18f; // 後ろへ少し吹き飛ばす
	} else {
		hitDir = { 0.0f, 0.0f, 1.0f }; // 万が一のフォールバック
		knockbackVelocity_ = { 0.0f, 0.0f, 0.0f };
	}

	Vector3 hitCenter = { pos_.x, pos_.y + 0.8f, pos_.z };

	// ① 中心の白い衝撃フラッシュ（瞬間的に大きく光る）
	GPUParticleManager::GetInstance()->Emit(hitCenter, { 0, 0, 0 }, 0.08f, 3.5f, { 1.0f, 1.0f, 0.9f, 1.0f });
	GPUParticleManager::GetInstance()->Emit(hitCenter, { 0, 0, 0 }, 0.14f, 2.0f, { 1.0f, 0.85f, 0.3f, 1.0f });

	// ② 衝撃波リング（白→黄色、速く広がる）
	for ( int i = 0; i < 20; i++ ) {
		float angle = ( 3.141592f * 2.0f / 20.0f ) * i;
		float speed = 0.55f + ( rand() % 5 ) * 0.04f;
		Vector3 ringVel = { std::sinf(angle) * speed, 0.02f, std::cosf(angle) * speed };
		GPUParticleManager::GetInstance()->Emit(hitCenter, ringVel, 0.18f, 1.2f, { 1.0f, 1.0f, 0.8f, 1.0f });
	}

	// ③ 鋭い火花（攻撃方向に多く飛ぶ）
	for ( int i = 0; i < 25; i++ ) {
		Vector3 sparkVel = {
			hitDir.x * ( 0.4f + ( rand() % 8 ) * 0.06f ) + ( rand() % 11 - 5 ) * 0.06f,
			0.2f + ( rand() % 12 ) * 0.04f,
			hitDir.z * ( 0.4f + ( rand() % 8 ) * 0.06f ) + ( rand() % 11 - 5 ) * 0.06f
		};
		float scale = 0.35f + ( rand() % 5 ) * 0.08f;
		float orange = 0.3f + ( rand() % 8 ) * 0.06f;
		GPUParticleManager::GetInstance()->Emit(hitCenter, sparkVel, 0.3f, scale, { 1.0f, orange, 0.0f, 1.0f });
	}


	isHit_ = true;                    // ヒット状態開始
	hitTimer_ = hitDuration_;         // ヒット表示時間セット
	hitStunTimer_ = hitStunDuration_; // 被弾硬直時間セット

	thinkTimer_ = hitStunDuration_; // 硬直中は再思考しない
	isCasting_ = false;
	castTimer_ = 0;
	currentUseCard_ = { -1, "", 0 };
	cardCooldownTimer_ = 20; // 被弾直後は少しカードを使わせない



	if ( hp_ <= 0 ) {
		hp_ = 0;
		isDead_ = true;
		GameSE::EnemyDeath();

		Vector3 deathCenter = { pos_.x, pos_.y + 0.6f, pos_.z };

		// ① 中心の白い衝撃フラッシュ（瞬間爆発感）
		GPUParticleManager::GetInstance()->Emit(deathCenter, { 0, 0, 0 }, 0.06f, 6.0f, { 1.0f, 1.0f, 0.9f, 1.0f });
		GPUParticleManager::GetInstance()->Emit(deathCenter, { 0, 0, 0 }, 0.14f, 4.0f, { 1.0f, 0.7f, 0.2f, 1.0f });

		// ② 爆発リング（全方向に素早く広がる）
		for ( int i = 0; i < 24; i++ ) {
			float angle = ( 3.141592f * 2.0f / 24.0f ) * i;
			float speed = 0.7f + ( rand() % 6 ) * 0.05f;
			Vector3 ringVel = { std::sinf(angle) * speed, 0.05f, std::cosf(angle) * speed };
			GPUParticleManager::GetInstance()->Emit(deathCenter, ringVel, 0.22f, 1.5f, { 1.0f, 0.9f, 0.5f, 1.0f });
		}

		// ③ 炎の爆散（多数・広範囲）
		for ( int i = 0; i < 50; i++ ) {
			Vector3 expPos = {
				pos_.x + ( rand() % 21 - 10 ) * 0.08f,
				pos_.y + 0.4f + ( rand() % 16 ) * 0.07f,
				pos_.z + ( rand() % 21 - 10 ) * 0.08f
			};
			Vector3 expVel = {
				( rand() % 21 - 10 ) * 0.22f,
				( rand() % 15 ) * 0.1f + 0.05f,
				( rand() % 21 - 10 ) * 0.22f
			};
			float g = 0.1f + ( rand() % 7 ) * 0.08f;
			float scale = 0.4f + ( rand() % 6 ) * 0.1f;
			GPUParticleManager::GetInstance()->Emit(expPos, expVel, 0.55f, scale, { 1.0f, g, 0.0f, 1.0f });
		}

		// ④ 高速飛散スパーク（鋭く遠くへ飛ぶ）
		for ( int i = 0; i < 20; i++ ) {
			Vector3 sparkVel = {
				( rand() % 21 - 10 ) * 0.3f,
				( rand() % 15 ) * 0.15f + 0.1f,
				( rand() % 21 - 10 ) * 0.3f
			};
			float scale = 0.2f + ( rand() % 4 ) * 0.06f;
			GPUParticleManager::GetInstance()->Emit(deathCenter, sparkVel, 0.4f, scale, { 1.0f, 0.8f, 0.3f, 1.0f });
		}

		// ⑤ 黒煙（増量・大きく・ゆっくり上昇）
		for ( int i = 0; i < 18; i++ ) {
			Vector3 smokePos = {
				pos_.x + ( rand() % 21 - 10 ) * 0.12f,
				pos_.y + 0.3f + ( rand() % 11 ) * 0.12f,
				pos_.z + ( rand() % 21 - 10 ) * 0.12f
			};
			Vector3 smokeVel = {
				( rand() % 21 - 10 ) * 0.06f,
				0.08f + ( rand() % 11 ) * 0.04f,
				( rand() % 21 - 10 ) * 0.06f
			};
			float scale = 0.9f + ( rand() % 5 ) * 0.15f;
			GPUParticleManager::GetInstance()->Emit(smokePos, smokeVel, 1.3f, scale, { 0.18f, 0.18f, 0.18f, 0.78f });
		}

	}
}

void Enemy::SetActionLock(int frame){
	isActionLocked_ = true;   // 行動ロック開始
	actionLockTimer_ = frame; // ロック残り時間設定
}

bool Enemy::IsVisible() const{
	if ( !isHit_ ) return true; // 通常時は表示

	return ( hitTimer_ % 2 ) == 0; // ヒット中は点滅表示
}

float Enemy::GetUseRangeForCurrentCard() const{
	const Card& card = HasUsablePickupCard() ? pickupCard_ : baseCard_;

	// カードの距離タイプで使用距離を変える
	switch ( card.attackRangeType ) {
	case CardAttackRangeType::Melee:
		return 1.8f; // 近距離

	case CardAttackRangeType::Mid:
		return 4.0f; // 中距離

	case CardAttackRangeType::Long:
		return 7.0f; // 遠距離
	}

	return cardUseEnterRange_;
}

float Enemy::GetRetreatEnterRangeForCurrentCard() const{
	const Card& card = HasUsablePickupCard() ? pickupCard_ : baseCard_;

	// カードの距離タイプで離脱開始距離を変える
	switch ( card.attackRangeType ) {
	case CardAttackRangeType::Melee:
		return 1.0f; // 近距離はあまり逃げない

	case CardAttackRangeType::Mid:
		return 2.5f; // 中距離は少し距離を取る

	case CardAttackRangeType::Long:
		return 4.0f; // 遠距離はしっかり距離を取る
	}

	return retreatEnterRange_;
}

bool Enemy::ShouldKeepDistanceForCurrentCard() const{
	const Card& card = HasUsablePickupCard() ? pickupCard_ : baseCard_;
	return card.attackRangeType != CardAttackRangeType::Melee;
}
