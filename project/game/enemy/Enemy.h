#pragma once
#include "engine/math/VectorMath.h"
#include "game/card/HandManager.h"

class Enemy {
public:
    // 敵の行動状態
    enum class State {
        Patrol,       // 巡回
        MoveToCard,   // カードへ移動
        ChasePlayer,  // プレイヤー追跡
        Investigate,  // 最後に見た位置を調べる
        UseCard,      // カード使用
        Retreat       // プレイヤーから離れる
    };

    enum class Type {
        Normal,
        Fast,
        Ranged,
        Heavy
    };

    void Initialize(); // 初期化
    void Update();     // 更新

    // 各Transform取得
    const Vector3& GetPosition() const { return pos_; }      // 位置取得
    const Vector3& GetScale() const { return scale_; }       // スケール取得
    const Vector3& GetRotation() const { return rot_; }      // 回転取得

    // 各Transform設定
    void SetPosition(const Vector3& pos) {
        pos_ = pos;
        startX_ = pos.x;
        patrolAnchor_ = pos;
        patrolTarget_ = pos;
    } // 位置設定
    void SetScale(const Vector3& scale) { scale_ = scale; }  // スケール設定

    // 通常の位置補正用。巡回基準は変えない
    void SetPositionOnly(const Vector3& pos) { pos_ = pos; } // 位置だけ設定

    // 各種ターゲット設定
    void SetPlayerPosition(const Vector3& pos) { playerPos_ = pos; } // プレイヤー位置設定
    void SetNavigationTarget(bool hasTarget, const Vector3& pos) {
        hasNavigationTarget_ = hasTarget;
        navigationTargetPos_ = pos;
    }
    void SetCardTarget(bool hasTarget, const Vector3& pos) {
        hasTargetCard_ = hasTarget; // 目標カードの有無
        targetCardPos_ = pos;       // 目標カード位置
    }
    void SetBossRoomBehavior(bool isBossRoom) { isBossRoom_ = isBossRoom; }
    bool IsBossRoomBehavior() const { return isBossRoom_; }
    void SetType(Type type);
    Type GetType() const { return type_; }
    int GetExpReward() const { return expReward_; }

    // 状態関連
    void SetState(State state) { state_ = state; } // 状態設定
    State GetState() const { return state_; }      // 状態取得
    bool HasPickupCard() const { return hasPickupCard_; }
    bool HasUsablePickupCard() const { return hasPickupCard_; }

    void SetPickupCard(const Card& card) {
        pickupCard_ = card;
        hasPickupCard_ = true;
    }

    const Card& GetPickupCard() const { return pickupCard_; }

    void ClearPickupCard() {
        hasPickupCard_ = false;
        pickupCard_ = { -1, "", 0 };
    }

    const Card& GetBaseCard() const { return baseCard_; }
    const Card& GetCurrentUseCard() const { return currentUseCard_; }
    CardAttackRangeType GetCurrentAttackRangeType() const {
        const Card& card = HasUsablePickupCard() ? pickupCard_ : baseCard_;
        return card.attackRangeType;
    }

    // HP関連
    void TakeDamage(int damage);             // ダメージを受ける
    bool IsDead() const { return isDead_; } // 死亡状態
    int GetHP() const { return hp_; }       // HP取得

    // 行動ロック関連
    void SetActionLock(int frame);                           // 一定時間行動ロック
    bool IsActionLocked() const { return isActionLocked_; } // 行動ロック中か

    // ヒット点滅関連
    bool IsHit() const { return isHit_; } // ヒット中か
    bool IsVisible() const;               // 描画するか

    // カード使用リクエスト
    bool GetCardUseRequest() const { return cardUseRequest_; } // カード使用要求
    void ClearCardUseRequest() { cardUseRequest_ = false; }    // カード使用要求クリア

    // 敵を凍らせる
    void Freeze(int durationFrames);
    bool IsFrozen() const { return isFrozen_; }

    // デバフを掛ける
    void ApplyAttackDebuff(int duration) {
        isAttackDebuffed_ = true;
        attackDebuffTimer_ = duration;
    }

    // 現在デバフ中か
    bool IsAttackDebuffed() const { return isAttackDebuffed_; }

    // 詠唱状態
    bool IsCasting() const { return isCasting_; }
    int GetCastTimer() const { return castTimer_; }

    // 強制的に吹き飛ばす力を与える関数
    void ApplyKnockback(const Vector3 &velocity) {
        knockbackVelocity_ = velocity;
        isHit_ = true;
        hitTimer_ = 10; // 吹き飛んでいる時間
    }

private:
    void DecideNextState();                         // 次の状態を決める
    bool IsStuck() const;                          // 詰まり判定
    float GetUseRangeForCurrentCard() const;       // 現在カードの使用距離
    float GetRetreatEnterRangeForCurrentCard() const; // 離脱開始距離
    bool ShouldKeepDistanceForCurrentCard() const;

    void UpdatePatrol();        // 巡回処理
    void UpdateMoveToCard();    // カードへ向かう処理
    void UpdateChasePlayer();   // プレイヤー追跡処理
    void UpdateInvestigate();   // 最後に見た位置を調べる処理
    void UpdateUseCard();       // カード使用処理
    void UpdateRetreat();       // プレイヤーから離れる処理

private:
    Vector3 pos_{ 5.0f, 0.0f, 5.0f };   // 位置
    Vector3 rot_{ 0.0f, 0.0f, 0.0f };   // 回転
    Vector3 scale_{ 1.0f, 1.0f, 1.0f }; // スケール

    float speed_ = 0.07f;       // 巡回速度
    float chaseSpeed_ = 0.12f;  // 追跡速度
    float moveRange_ = 3.0f;    // 旧巡回範囲

    float chaseRange_ = 14.0f;       // プレイヤーを追跡し始める距離
    float bossRoomChaseRange_ = 30.0f; // ボス部屋での追跡距離
    float cardUseEnterRange_ = 6.0f; // カード使用に入る距離
    float cardUseExitRange_ = 7.5f;  // カード使用をやめる距離
    float retreatEnterRange_ = 3.0f; // 離れる行動に入る距離
    float retreatExitRange_ = 4.5f;  // 離れる行動をやめる距離

    float startX_ = 5.0f; // 初期X位置
    int direction_ = 1;   // 旧巡回方向

    State state_ = State::Patrol; // 現在の状態
    Type type_ = Type::Normal;

    Vector3 playerPos_{ 0.0f, 0.0f, 0.0f };          // プレイヤー位置
    Vector3 navigationTargetPos_{ 0.0f, 0.0f, 0.0f };
    bool hasNavigationTarget_ = false;
    Vector3 lastKnownPlayerPos_{ 0.0f, 0.0f, 0.0f }; // 最後に確認したプレイヤー位置
    Vector3 targetCardPos_{ 0.0f, 0.0f, 0.0f };      // 目標カード位置
    bool hasTargetCard_ = false;                     // 目標カードがあるか

    // 巡回を近場の目標点ベースで動かすための情報
    Vector3 patrolAnchor_{ 5.0f, 0.0f, 5.0f }; // 巡回の中心地点
    Vector3 patrolTarget_{ 5.0f, 0.0f, 5.0f }; // 巡回時の次の目標地点
    int patrolWaitTimer_ = 0;                  // 目標を決め直すまでの待機
    int investigateTimer_ = 0;                 // 見失い後の捜索時間
    int bossRoomInvestigateFrames_ = 300;      // ボス部屋での捜索継続時間
    float patrolTargetRadius_ = 5.5f;          // 巡回先を散らす半径
    float patrolTargetMinDistance_ = 2.0f;     // 次の巡回先が近すぎないようにする距離
    bool isBossRoom_ = false;                  // ボス部屋かどうか

    Card baseCard_{ -1, "", 0 };        // 固定のパンチカード
    bool hasPickupCard_ = false;        // 拾ったカードを持っているか
    Card pickupCard_{ -1, "", 0 };      // 拾ったカード
    Card currentUseCard_{ -1, "", 0 };  // 今回使うカード

    int hp_ = 3;               // 敵HP
    int expReward_ = 1;
    bool isDead_ = false;      // 死亡フラグ

    bool isActionLocked_ = false; // 行動ロック中か
    int actionLockTimer_ = 0;     // 行動ロック残り時間

    int thinkTimer_ = 0; // 思考更新までの残り時間

    bool isHit_ = false;  // ヒット中フラグ
    int hitTimer_ = 0;    // ヒット残り時間
    const int hitDuration_ = 10; // ヒット表示時間
    Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f }; // ノックバック速度

    bool cardUseRequest_ = false; // カード使用要求フラグ

    int cardCooldownTimer_ = 0;   // カード使用クールダウン
    const int cardCooldown_ = 30; // カード使用クールダウン時間

    Vector3 prevPos_{ 5.0f, 0.0f, 5.0f }; // 前フレーム位置
    int stuckTimer_ = 0;                  // 詰まり時間
    const int stuckThreshold_ = 20;       // 何フレームで詰まりとみなすか
    float stuckDistanceThreshold_ = 0.01f; // ほぼ動いていない距離

    bool isFrozen_ = false; // 凍結状態フラグ
    int freezeTimer_ = 0;   // 凍結時間タイマー

    int hitStunTimer_ = 0;           // 被弾硬直の残り時間
    const int hitStunDuration_ = 24; // 被弾硬直時間

    bool isAttackDebuffed_ = false; // 攻撃デバフ中か
    int attackDebuffTimer_ = 0;     // 攻撃デバフ残り時間

    bool isCasting_ = false; // 詠唱中か
    int castTimer_ = 0;      // 詠唱残り時間
    const int castTime_ = 30;
    int strafeDirection_ = 1;
    float retreatStrafeStrength_ = 0.65f; // 離脱時に横へずれる強さ
};
