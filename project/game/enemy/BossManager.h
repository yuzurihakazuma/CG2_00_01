#pragma once
#include <memory>
#include <array>
#include <unordered_map>
#include "engine/math/VectorMath.h"

// 前方宣言
class Boss;
class Obj3d;
class Camera;
class Sprite;
class CardUseSystem;
class MapManager;
class Player;
class EnemyManager;
class CardPickupManager;

class BossManager {
public:
    // ボス登場時のカメラ演出状態
    enum class IntroCameraState {
        None,           // 通常
        SkyLook,        // 空を見上げる
        BossReveal,     // 上空のボスを見せる
        BossDropFollow, // 落下中のボスを追う
        BossLandImpact, // 着地の見せ場
        ToBattle,        // 戦闘位置へ移動
        BossSplit,   // 10階層用の分裂演出
    };

    enum class BossType {
        Normal,
        Split
    };

public:
    BossManager() = default;
    ~BossManager() = default;

    // ボス・描画・カードシステム生成
    void Initialize(Camera* camera);

    // リソース解放
    void Finalize();

    // ボスを部屋に再配置（通常 / ボス部屋で位置分岐）
    void RespawnInRoom(MapManager* mapManager);

    // 内部状態リセット（演出やフラグ）
    void Reset();

    // ボスの全更新（AI / カード / 召喚 / 演出 / 死亡処理）
    void Update(
        Player* player,
        EnemyManager* enemyManager,
        CardPickupManager* cardPickupManager,
        MapManager* mapManager,
        Camera* camera,
        const Vector3& playerPos,
        const Vector3& targetPos
    );

    // ボス本体 + カード演出 + HPバー描画
    void Draw(MapManager* mapManager);

    // HPバーのみ描画
    void DrawHpBar(MapManager* mapManager);

    // ボス本体取得（状態参照用）
    Boss* GetBoss() const { return boss_.get(); }

    // ボス用カードシステム取得
    CardUseSystem* GetBossCardSystem() const { return bossCardSystem_.get(); }

    // 描画オブジェクト取得
    Obj3d* GetBossObj() const { return bossObj_.get(); }

    // HPバーUI取得
    Sprite* GetBossHpBackSprite() const { return bossHpBackSprite_.get(); }
    Sprite* GetBossHpFillSprite() const { return bossHpFillSprite_.get(); }
    Sprite* GetSplitBossHpBackSprite(int index) const { return splitBossHpBackSprites_[index].get(); }
    Sprite* GetSplitBossHpFillSprite(int index) const { return splitBossHpFillSprites_[index].get(); }

    // 登場演出状態
    bool IsBossIntroPlaying() const { return isBossIntroPlaying_; }
    int GetBossIntroTimer() const { return bossIntroTimer_; }
    IntroCameraState GetBossIntroCameraState() const { return bossIntroCameraState_; }

    // カード降らせ演出状態
    int GetBossCardRainTimer() const { return bossCardRainTimer_; }
    int GetBossCardRainInterval() const { return bossCardRainInterval_; }
    int GetBossCardRainMax() const { return bossCardRainMax_; }
    bool IsBossCardRainEnabled() const { return isBossCardRainEnabled_; }

    // デバッグ・外部制御用
    void SetBossIntroPlaying(bool flag) { isBossIntroPlaying_ = flag; }
    void SetBossIntroTimer(int timer) { bossIntroTimer_ = timer; }
    void SetBossIntroCameraState(IntroCameraState state) { bossIntroCameraState_ = state; }

    void SetBossCardRainTimer(int timer) { bossCardRainTimer_ = timer; }
    void SetBossCardRainMax(int max) { bossCardRainMax_ = max; }
    void SetBossCardRainEnabled(bool flag) { isBossCardRainEnabled_ = flag; }

    // 死亡処理が既に実行されたか（多重実行防止）
    bool IsBossDeadHandled() const { return bossDeadHandled_; }
    void SetBossDeadHandled(bool flag) { bossDeadHandled_ = flag; }

    // 登場演出開始 / 終了
    void StartBossIntro();
    void EndBossIntro();

    // ボス撃破時にゲームクリアするか判定
    bool ShouldTriggerGameClear(MapManager* mapManager) const;
    bool IsBossBattleFinished(MapManager* mapManager) const;
    bool IsBossDeathAnimationPlaying() const;

	// ボスタイプ判定
    bool IsSplitBossBattle() const { return bossType_ == BossType::Split; }

    Boss* GetBossAt(int index) const {
        // 通常ボス時は0番だけ通常ボスを返す
        if (bossType_ != BossType::Split) {
            return index == 0 ? boss_.get() : nullptr;
        }

        // 分裂ボス時は配列から返す
        if (index < 0 || index >= static_cast<int>(splitBosses_.size())) {
            return nullptr;
        }

        return splitBosses_[index].get();
    }
	// 分裂ボス用の位置
    Vector3 GetBossFocusPosition() const;
	// HP割合取得（分裂ボスは平均）
    float GetBossHpRate() const;
    float GetBossHpRateAt(int index) const;
	// 分裂ボスの中心位置（演出用）
    Vector3 GetSplitBossCenterPosition() const { return splitBossCenterPosition_; }
	// 分裂ボスのターゲット位置（攻撃エフェクトの中心など、演出用）
    const Vector3& GetSplitBossTargetPosition(int index) const { return splitBossTargetPositions_[index]; }
    void SetBossAttackIntervalRangeFrames(int minFrames, int maxFrames) { // 通常ボス用
        bossAttackIntervalMinFrames_ = minFrames;
        bossAttackIntervalMaxFrames_ = maxFrames;
    }

    void SetSplitBossAttackIntervalRangeFrames(int index, int minFrames, int maxFrames) { // 10階の各ボス用
        if (index < 0 || index >= 2) {
            return;
        }
        splitBossAttackIntervalMinFrames_[index] = minFrames;
        splitBossAttackIntervalMaxFrames_[index] = maxFrames;
    }

private:
    void UpdateBeamWarning(MapManager* mapManager);
    void DrawBossCastCards(MapManager* mapManager);
    void DrawCastCardForBoss(const Boss& boss);
	bool ShouldDrawSplitBossUnion() const;

    std::unique_ptr<Boss> boss_ = nullptr;              // ボス本体
    std::unique_ptr<Obj3d> bossObj_ = nullptr;          // 描画用
	std::unique_ptr<Obj3d> splitBossUnionObj_ = nullptr;
    std::unique_ptr<Obj3d> beamWarningObj_ = nullptr;
    std::unordered_map<int, std::unique_ptr<Obj3d>> bossCastCardObjs_;
    Camera* camera_ = nullptr;
    std::unique_ptr<CardUseSystem> bossCardSystem_ = nullptr; // カード処理

    bool bossDeadHandled_ = false; // 死亡処理フラグ
    bool splitBossFirstDefeatExpAwarded_ = false; // 分裂ボスの1体目撃破経験値を渡したか

    // HPバー
    std::unique_ptr<Sprite> bossHpBackSprite_ = nullptr;
    std::unique_ptr<Sprite> bossHpFillSprite_ = nullptr;
    std::array<std::unique_ptr<Sprite>, 2> splitBossHpBackSprites_{};
    std::array<std::unique_ptr<Sprite>, 2> splitBossHpFillSprites_{};

    // 登場演出
    IntroCameraState bossIntroCameraState_ = IntroCameraState::None;
    int bossIntroTimer_ = 0;
    bool isBossIntroPlaying_ = false;

    // カード降らせ演出
    int bossCardRainTimer_ = 0;
    const int bossCardRainInterval_ = 180;
    int bossCardRainMax_ = 8;
    bool isBossCardRainEnabled_ = true;

	// ボスのタイプ
    BossType bossType_ = BossType::Normal;

    // 10階層用の分裂ボス2体
    std::array<std::unique_ptr<Boss>, 2> splitBosses_{};
    std::array<std::unique_ptr<Obj3d>, 2> splitBossObjs_{};

    // 分裂演出の開始位置と左右の着地点
    Vector3 splitBossCenterPosition_{ 0.0f, 0.0f, 0.0f };
    std::array<Vector3, 2> splitBossTargetPositions_{};

    // 分裂ボスの見た目調整
    Vector3 splitBossScale_{ 1.45f, 1.45f, 1.45f };
    float splitBossOffset_ = 3.2f;

    int bossAttackIntervalMinFrames_ = 120; // 通常ボス用の最小攻撃間隔。2秒
    int bossAttackIntervalMaxFrames_ = 240; // 通常ボス用の最大攻撃間隔。4秒

    std::array<int, 2> splitBossAttackIntervalMinFrames_{ 120, 120 }; // 10階左・右ボスの最小攻撃間隔
    std::array<int, 2> splitBossAttackIntervalMaxFrames_{ 240, 240 }; // 10階左・右ボスの最大攻撃間隔

    bool hasSplitParticleTriggered_ = false; // 分裂演出（登場完了爆発）の発火フラグ
    bool hasSplitSeenAppearing_     = false; // 少なくとも1体がAppear中だったことを確認するフラグ

};
