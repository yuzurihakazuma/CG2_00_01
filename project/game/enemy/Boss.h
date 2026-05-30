#pragma once
#include "engine/math/VectorMath.h"
#include "game/card/HandManager.h"
#include <unordered_map>
#include <vector>

class Camera;

class Boss {
public:
    enum class State {
        Appear,
        Idle,
        Chase,
        UseCard,
        Dead
    };

    enum class CombatRole {
        Melee,   // 近距離型
        Ranged   // 遠距離型
    };

public:
    void Initialize();
    void Update();

    const Vector3& GetPosition() const { return pos_; }
    const Vector3& GetRotation() const { return rot_; }
    const Vector3& GetScale() const { return scale_; }

    void SetPosition(const Vector3& pos) { pos_ = pos; }
    void SetScale(const Vector3& scale) {
        // 現在の見た目サイズと基準サイズを両方更新する
        scale_ = scale;
        baseScale_ = scale;
    }

    void SetSpawnPosition(const Vector3& pos);

    void SetPlayerPosition(const Vector3& pos) { playerPos_ = pos; }
    void SetCamera(Camera* camera) { camera_ = camera; }

    void SetState(State state) { state_ = state; }
    State GetState() const { return state_; }

    bool IsAppearing() const { return state_ == State::Appear; }

    void TakeDamage(int damage);
    int GetHP() const { return hp_; }
    int GetMaxHP() const { return maxHP_; }
    bool IsDead() const { return isDead_; }
    bool IsDeathAnimationPlaying() const { return isDead_ && deathAnimationTimer_ > 0; }

    bool IsEnraged() const{ return !isDead_ && hasEnrageTriggered_; }

    bool IsHit() const { return isHit_; }
    bool IsVisible() const;

    void SetActionLock(int frame);
    bool IsActionLocked() const { return isActionLocked_; }

    bool GetCardUseRequest() const { return cardUseRequest_; }
    void ClearCardUseRequest() { cardUseRequest_ = false; }

    const Card& GetSelectedCard() const { return selectedCard_; }

    bool HasAnyCard() const { return !heldCards_.empty(); }
    Card GetRandomDropCard() const;

    void ApplyAttackDebuff(int duration) {
        isAttackDebuffed_ = true;
    }

    void Freeze(int durationFrames) {
        isActionLocked_ = true;
        actionLockTimer_ = durationFrames;
    }

    bool IsAttackDebuffed() const { return isAttackDebuffed_; }
    bool IsCasting() const { return isCasting_; }
    int GetCastTimer() const { return castTimer_; }
    int GetCastDurationCurrent() const { return castDurationCurrent_; }
    void StartCardRepeatWarning(int cardId, int durationFrames);
    void StartClawRepeatWarning(int durationFrames);
    bool IsClawRepeatWarningActive() const { return clawRepeatWarningTimer_ > 0; }
    int GetClawRepeatWarningTimer() const { return clawRepeatWarningTimer_; }
    int GetClawRepeatWarningDuration() const { return clawRepeatWarningDuration_; }
    int GetRepeatWarningCardId() const { return repeatWarningCardId_; }

    void SetAttackIntervalRangeFrames(int minFrames, int maxFrames) { // 攻撃間隔の範囲を設定
        attackIntervalMinFrames_ = minFrames;
        attackIntervalMaxFrames_ = maxFrames;
    }
    void RequestSummon(int count) {
        summonRequest_ = true;
        summonCount_ = count;
    }
    bool GetSummonRequest() const { return summonRequest_; }
    int GetSummonCount() const { return summonCount_; }
    void ClearSummonRequest() {
        summonRequest_ = false;
        summonCount_ = 0;
    }

    void PlayPreBattlePose(float normalizedTime) { ApplyPreBattlePose(normalizedTime); }
    void ClearPreBattlePose() { ResetPose(); }

    // HP
    void SetMaxHP(int maxHp);
    void SetHP(int hp);

    void SetCombatRole(CombatRole role) { combatRole_ = role; }
    CombatRole GetCombatRole() const { return combatRole_; }

    void SetPartnerPosition(const Vector3& pos) {
        partnerPos_ = pos;
        hasPartnerPosition_ = true;
    }

    void ClearPartnerPosition() {
        hasPartnerPosition_ = false;
    }

    void SetForceMeleeMode(bool flag) { forceMeleeMode_ = flag; }

    void SetSplitBehaviorEnabled(bool flag) { isSplitBehaviorEnabled_ = flag; }
    bool IsSplitBehaviorEnabled() const { return isSplitBehaviorEnabled_; }

    // 強制的に吹き飛ばす力を与える関数
    void ApplyKnockback(const Vector3 &velocity) {
        knockbackVelocity_ = velocity;
        isHit_ = true;
        hitTimer_ = 10; // 吹き飛んでいる時間
    }

    const Vector3& GetBaseScale() const { return baseScale_; }

    void SetStun(int durationFrames); // スタンさせる関数
    bool IsStunned() const { return isStunned_; } // スタン中か確認

    void SetRotation(const Vector3& rot) { rot_ = rot; }
    int GetAnimationTimer() const { return animationTimer_; }

private:
    void DecideNextState();
    void UpdateAppear();
    void UpdateIdle();
    void UpdateChase();
    void UpdateUseCard();
    void UpdateEnrageEffect();
    void InitializeBossCards();
    Card SelectCardForDistance(float dist, bool isEnraged);
    int GetCastTimeForCard(int cardId, bool isEnraged) const;
    int GetRecoveryTimeForCard(int cardId, bool isEnraged) const;
    void ApplyCastingPose(float normalizedTime);
    void ApplyPreBattlePose(float normalizedTime);
    void UpdateDeathAnimation();
    void ResetPose();

    bool IsCardReady(int cardId) const;
    void StartCardCooldown(int cardId, int time);
    int GetCardCooldownTime(int cardId) const;

private:
    Vector3 pos_{ 10.0f, 0.0f, 10.0f };
    Vector3 rot_{ 0.0f, 0.0f, 0.0f };
    Vector3 scale_{ 2.0f, 2.0f, 2.0f };

    Vector3 playerPos_{ 0.0f, 0.0f, 0.0f };
    Camera* camera_ = nullptr;
    State state_ = State::Appear;

    int hp_ = 20;
    int maxHP_ = 20;
    bool isDead_ = false;
    int deathAnimationTimer_ = 0;
    const int deathAnimationDuration_ = 150;
    float deathStartY_ = 0.0f;

    float chaseSpeed_ = 0.06f;
    float chaseRange_ = 100.0f;
    float cardExitRange_ = 10.0f;
    int thinkTimer_ = 0;

    bool isCasting_ = false;
    int castTimer_ = 0;
    const int castTime_ = 60;
    int castDurationCurrent_ = 60;

    bool isActionLocked_ = false;
    int actionLockTimer_ = 0;

    bool isHit_ = false;
    int hitTimer_ = 0;
    const int hitDuration_ = 10;
    Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f };

    int cardCooldownTimer_ = 0;

    std::vector<Card> heldCards_;
    Card selectedCard_{ -1, "", 0 };
    int lastUsedCardId_ = -1;

    bool isAttackDebuffed_ = false;

    float appearStartY_ = -4.0f;
    float appearTargetY_ = 2.0f;
    int appearTimer_ = 0;
    const int appearDuration_ = 110;

    bool summonRequest_ = false;
    int summonCount_ = 0;

    bool cardUseRequest_ = false;
    int clawRepeatWarningTimer_ = 0;
    int clawRepeatWarningDuration_ = 0;
    int repeatWarningCardId_ = 101;
    std::unordered_map<int, int> cardCooldownTimers_;

    CombatRole combatRole_ = CombatRole::Melee; // 個体ごとの役割
    Vector3 partnerPos_{ 0.0f, 0.0f, 0.0f };    // 相方の位置
    bool hasPartnerPosition_ = false;           // 相方位置が有効か
    bool forceMeleeMode_ = false;               // 片方撃破後に両方近距離化
    bool isSplitBehaviorEnabled_ = false;      // 10階層分裂ボス専用AIを使うか
    Vector3 baseScale_{ 2.0f, 2.0f, 2.0f }; // この個体の基準サイズ


    bool isStunned_ = false;       // スタン中か
    int stunTimer_ = 0;            // スタンの残り時間
    float preStunYaw_ = 0.0f;     // スタン前の向き（解除後に復元）
    int animationTimer_ = 0;       // ★ 演出計算用の汎用カウンター

    int attackIntervalMinFrames_ = 120; // 攻撃間隔の最小値。60fpsで2秒
    int attackIntervalMaxFrames_ = 240; // 攻撃間隔の最大値。60fpsで4秒
    int attackIntervalTimer_ = 0;       // 次に攻撃開始できるまでの残り時間

    bool hasEnrageTriggered_ = false;
};

