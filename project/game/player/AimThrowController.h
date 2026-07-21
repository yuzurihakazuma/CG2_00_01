#pragma once
#include "engine/math/struct.h"
#include <memory>
#include <cstdint>

class Player;
class EnemyManager;
class EggSystem;
class Camera;
class Sprite;

// =====================================================================
//  AimThrowController：ヨッシー風の卵投げ（構え→狙い→投擲）。
//   ・Q長押しで構え、矢印キーで「画面上のカーソル」を動かす（上=上/右=右で直感的）
//   ・カーソルが敵に画面上で重なるとロックオン（粘りつき）。Qを離すと投擲
//   ・ロック中はその敵へ速い弾 / 未ロックはカーソルの奥へアンプロジェクトして投げる
//  内部は Idle / Aiming の小さなステートマシン。
//  シーンから分離した理由：入力・カーソルUI・照準・投擲が一体の独立した操作系で、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class AimThrowController {
public:
    AimThrowController();
    ~AimThrowController(); // unique_ptr<Sprite> のため cpp 側で定義

    // カーソル用テクスチャ（circle2 等）の SRV インデックスを受け取りスプライトを作る
    void Initialize(uint32_t cursorTexSrv);

    // 毎フレーム（Play中のみ）呼ぶ
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, Camera* camera, float dt);

    // 構え中だけカーソルを描画（SpriteCommon::PreDraw 済みの状態で呼ぶ）
    void DrawSprite();

    // 構えを解除して通常状態へ（モード切替時などに呼ぶ）
    void Reset(){ state_ = State::Idle; aimLocked_ = false; }

    bool IsAiming() const { return state_ == State::Aiming; }

    // 直前にキャンセル（X）されたか。読むとフラグは消える（投げモーションを出さない判定用）
    bool ConsumeCanceled(){ bool wasCanceled = canceled_; canceled_ = false; return wasCanceled; }

    // 今の狙い方向のヨー角（構え中、プレイヤーの向きをこれに合わせる）
    float GetAimYaw() const{ return aimYaw_; }

    // ノードエディタの「→ ゲーム値」用（投げ初速を外から調整できる）
    float* ThrowSpeedNormalPtr(){ return &throwSpeedNormal_; }
    float* ThrowSpeedLockPtr(){ return &throwSpeedLock_; }

private:
    // 構えのステート（Idle=通常 / Aiming=構え中）
    enum class State { Idle, Aiming };
    State state_ = State::Idle;

    float cursorX_ = 0.0f;   // 狙いカーソルの画面位置X(px)
    float cursorY_ = 0.0f;   // 狙いカーソルの画面位置Y(px)
    bool  aimLocked_ = false; // 敵にロックオン中か（カーソルの色/サイズ変更）
    bool  canceled_ = false;  // 直前にXでキャンセルしたか（1回読まれたら消えるフラグ）
    float aimYaw_ = 0.0f;     // 今の狙い方向のヨー角（プレイヤーの向き合わせ用）
    float aimAxisYaw_ = 0.0f; // 構え開始時のレール軸の向き（狙い中は固定）

    float throwSpeedNormal_ = 13.0f; // 通常の投げ初速 (m/s)
    float throwSpeedLock_   = 22.0f; // ロックオン時の投げ初速 (m/s)

    float pulseT_ = 0.0f; // カーソルの脈動用タイマー（視認性向上）

    std::unique_ptr<Sprite> cursorSprite_;   // 狙い用カーソル（構え中だけ表示）
    std::unique_ptr<Sprite> lockRingSprite_; // ロックオン中に敵の周りで回るリング
};
