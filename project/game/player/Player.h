#pragma once
#include <memory>
#include <string>
#include <vector>

#include "engine/math/VectorMath.h"
#include "engine/3d/obj/SkinnedObj3d.h"

#include "game/particle/DodgeParticleEffect.h"

class Camera;

class Player {
public:
    // ポーズ編集で保存するジョイント情報
    struct JointPoseData {
        std::string jointName;
        Vector3 rotation;
        Vector3 translation;
    };

    struct NamedPoseData {
        std::string name;
        std::vector<JointPoseData> joints;
    };

    // 任意のポーズへ滑らかに遷移するための補間情報
    struct PoseBlendJointData {
        std::string jointName;
        Vector3 startRotation;
        Vector3 startTranslation;
        Vector3 targetRotation;
        Vector3 targetTranslation;
    };

    void Initialize();  // 初期化
    void Update();      // 更新
    void Draw();        // 描画
	void DrawAfterimage(); // 残像の描画
    void DrawAnimationDebugUI(); // プレイヤーアニメGUIの描画

    void SetCamera(const Camera* camera); // カメラ設定

    // 各Transform取得
    const Vector3& GetPosition() const { return pos_; }
    const Vector3& GetScale() const { return scale_; }
    const Vector3& GetRotation() const { return rot_; }

    // 各Transform設定
    void SetPosition(const Vector3& pos) { pos_ = pos; }
    void SetScale(const Vector3& scale) { scale_ = scale; }
    void SetRotation(const Vector3& rot) { rot_ = rot; }

    void LockAction(int frame); // 指定フレームの間プレイヤー操作をロック

    // レベル・経験値取得
    int GetLevel() const { return level_; }
    int GetExp() const { return exp_; }
    int GetNextLevelExp() const { return nextLevelExp_; }

    // コスト取得
    int GetCost() const { return cost_; }
    int GetMaxCost() const { return maxCost_; }

    void AddExp(int value);             // 経験値加算
    bool CanUseCost(int value) const;   // コスト使用可能判定
    void UseCost(int value);            // コスト消費

    void AddMaxCost(int amount);        // 最大コスト増減
    void SetCostRecoveryMultiplier(float multiplier);  //  コスト回復速度倍率セット

    int GetHP() const { return hp_; }         // 現在HP取得
    void SetTutorialNoDeath(bool enable) { isTutorialNoDeath_ = enable; } // チュートリアル中はHPが0にならないようにする
    int GetMaxHP() const { return maxHp_; }   // 最大HP取得
    bool IsDead() const { return isDead_; }   // 死亡判定
    bool IsDeathAnimationFinished() const { return isDead_ && deathAnimationTimer_ <= 0; } // 死亡演出が終わったか
    bool IsInvincible() const { return isInvincible_; } // 無敵状態判定
    bool IsDodging() const { return isDodging_; }       // 回避中判定

    bool IsHit() const { return isHit_; } // 被弾中か
    bool IsVisible() const;               // 描画するか

    bool IsKnockback() const { return isKnockback_; } // ノックバック中か

    void TakeDamage(int damage, const Vector3& attackFrom, float knockbackScale = 1.0f); // ダメージ処理
    void PlayCardUsePose(int durationFrames); // カード使用ポーズを再生する
    void PlayIdlePose(int durationFrames); // 通常姿勢へ戻す

    void SetInputEnable(bool enable) { isInputEnabled_ = enable; } // 入力有効/無効切り替え
    void ApplySpeedBuff(float multiplier, int durationFrames); // スピードバフ適用
    float GetSpeedMultiplier() const { return speedMultiplier_; } // 現在の速度倍率取得
    void Heal(int amount); // HP回復処理

    bool IsShieldActive() const { return isShieldActive_; } // シールド状態の取得
    void AddShieldHits(int hits) { shieldHitCount_ = hits; } // シールド回数セット
    int GetShieldHits() const { return shieldHitCount_; } // シールド残り回数取得

    bool IsActionLocked() const { return isActionLocked_; }

    void SetMaxCost(int cost) { maxCost_ = cost; }
    void SetCost(int cost) { cost_ = cost; }
    void SetHP(int hp) { hp_ = hp; }

    void SetEnemyAtkDebuffed(bool isDebuffed) { isEnemyAtkDebuffed_ = isDebuffed; }
    bool IsEnemyAtkDebuffed() const { return isEnemyAtkDebuffed_; }

    void SetDebugInvincible(bool isInvincible) { isDebugInvincible_ = isInvincible; } //デバッグモード専用の完全無敵切り替え

    void SetStun(int durationFrames); // スタンさせる関数

private:
    void LevelUp();      // レベルアップ処理
    void UpdateCost();   // コスト自然回復

    // ポーズ編集用の内部処理
    void RefreshJointList(); // モデルからジョイント一覧を取り直す
    void RefreshFilteredJointIndices(); // 検索条件に合うジョイント一覧を作る
    void SyncSelectedJointFromModel(); // 選択中ジョイントの値をモデルから読む
    void ApplySelectedJointToModel(); // GUIで編集した値をモデルへ反映する
    void ApplyPose(const NamedPoseData& pose); // 保存済みポーズをモデルへ反映する
    void ApplyPoseByName(const std::string& poseName); // 名前指定でポーズを反映する
    void SaveCurrentPose(const std::string& poseName); // 現在のジョイント状態をポーズとして保存する
    void SavePoseFile() const; // ポーズJSONを書き出す
    void LoadPoseFile(); // ポーズJSONを読み込む
    void EnsureDefaultPoseEntries(); // スロット名に対応する空ポーズを補完する
    int FindPoseIndex(const std::string& poseName) const; // 名前から保存済みポーズを探す
    void StartPoseBlendByName(const std::string& poseName, int duration); // 現在姿勢から指定ポーズへの補間を開始する
    void PreviewPoseBlend(const std::string& startPoseName, const std::string& targetPoseName, int duration); // 開始姿勢を揃えてから補間を再生する
    void UpdatePoseBlend(); // 補間中のポーズを更新する
    bool IsPoseBlendPlaying() const { return poseBlendTimer_ > 0; } // 補間中か

private:
    Vector3 pos_{ 0.0f, 0.0f, 0.0f };     // 位置
    Vector3 rot_{ 0.0f, 0.0f, 0.0f };     // 回転
    Vector3 scale_{ 1.0f, 1.0f, 1.0f };   // スケール

    std::unique_ptr<SkinnedObj3d> model_ = nullptr; // プレイヤーのアニメーションモデル
    const Camera* camera_ = nullptr;                // 使用カメラ

    float moveSpeed_ = 0.2f; // 通常移動速度

    // 回避
    bool isDodging_ = false;
    int dodgeTimer_ = 0;
    int dodgeInvincibleTimer_ = 0;
    int dodgeCooldownTimer_ = 0;
    const int dodgeDuration_ = 18;
    const int dodgeInvincibleDuration_ = 8;
    const int dodgeRecoveryDuration_ = 8;
    const int dodgeCooldownDuration_ = 20;
    float dodgeStartSpeed_ = 0.42f;
    float dodgeEndSpeed_ = 0.08f;
    Vector3 dodgeDirection_{ 0.0f, 0.0f, 0.0f };

    // 行動ロック
    bool isActionLocked_ = false;
    int actionLockTimer_ = 0;

    // レベル
    int level_ = 1;
    int exp_ = 0;
    int nextLevelExp_ = 3;

    // コスト
    int cost_ = 3;
    int maxCost_ = 3;
    int costRecoveryTimer_ = 0;
    int costRecoveryInterval_ = 180;

    // HP
    int hp_ = 8;
    int maxHp_ = 8;
    bool isDead_ = false;
    int deathAnimationTimer_ = 0;             // 死亡演出の残り時間
    const int deathAnimationDuration_ = 45;   // 死亡演出の表示時間
    int poseBlendTimer_ = 0;                  // ポーズ補間の残り時間
    int poseBlendDuration_ = 12;              // ポーズ補間の時間
    std::vector<PoseBlendJointData> poseBlendJoints_; // 補間対象のジョイント一覧

    // 被弾後無敵
    bool isInvincible_ = false;
    int invincibleTimer_ = 0;
    const int invincibleDuration_ = 30;

    // 被弾演出
    bool isHit_ = false;
    int hitTimer_ = 0;
    const int hitDuration_ = 20;

    // ノックバック
    bool isKnockback_ = false;
    int knockbackTimer_ = 0;
    const int knockbackDuration_ = 10;
    Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f };

    bool isInputEnabled_ = true; // 入力有効フラグ

    // 速度
    float speedMultiplier_ = 1.0f;
    int speedBuffTimer_ = 0;

    // シールド管理
    bool isShieldActive_ = false;
    int shieldHitCount_ = 0;

    bool isEnemyAtkDebuffed_ = false;
    bool isTutorialNoDeath_ = false; // チュートリアル中だけ死亡を防ぐ

    // ここから下がアニメGUI用の状態
    std::string poseFilePath_ = "resources/player/player_pose.json";
    std::vector<NamedPoseData> savedPoses_;
    std::vector<std::string> jointNames_;
    std::vector<int> filteredJointIndices_;

    int selectedJointIndex_ = -1;
    int selectedPoseIndex_ = -1;
    bool showEditedOnlyJoints_ = false;

    Vector3 selectedJointRotation_{ 0.0f, 0.0f, 0.0f };
    Vector3 selectedJointTranslation_{ 0.0f, 0.0f, 0.0f };

    char jointSearchText_[128] = "";
    char poseFilePathBuffer_[260] = "resources/player/player_pose.json";
    char poseNameBuffer_[64] = "idle";
    char idlePoseNameBuffer_[64] = "idle";
    char cardUsePoseNameBuffer_[64] = "card_use";
    char hitPoseNameBuffer_[64] = "hit";
    char deathPoseNameBuffer_[64] = "death";
    char blendStartPoseNameBuffer_[64] = "idle";

    float costRecoveryMultiplier_ = 1.0f; // コスト回復速度倍率
    bool isDebugInvincible_ = false; // デバッグ用無敵フラグ

    struct Afterimage {
        std::unique_ptr<SkinnedObj3d> obj; // 3Dオブジェクト本体
        bool isActive = false;      // 現在使用中（表示中）か
        int lifeTimer = 0;          // 生存残り時間（寿命）
        int maxLife = 0;            // 生存初期時間（最大寿命）
    };

    std::vector<Afterimage> afterimages_; // 残像の配列
    int maxAfterimages_ = 5;            // 同時に出せる残像の最大数
    int afterimageSpawnInterval_ = 2;   // 残像を出す間隔（フレーム数）
    int afterimageSpawnTimer_ = 0;      // 次に残像を出すまでのタイマー
    int afterimageLife_ = 20;            // 残像1つあたりの寿命（フレーム数）

    Vector3 lastAfterimagePos_ = { 9999.0f, 9999.0f, 9999.0f };

    std::unique_ptr<DodgeParticleEffect> dodgeParticle_;


    bool isStunned_ = false;  // スタン状態フラグ
    int stunTimer_ = 0;   // スタン時間タイマー
};
