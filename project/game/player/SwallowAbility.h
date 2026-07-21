#pragma once

#include "engine/math/struct.h"
#include <memory>
#include <d3d12.h>

class Player;
class EnemyManager;
class EggSystem;
class HitFeel;
class Enemy;
class Camera;
class Model;
class Obj3d;
class SDFVolumeObject;

// =====================================================================
//  SwallowAbility：ヨッシーの「舌で捕まえて食べる／産む／吐き出す」アクション。
//   ・E      … 舌を正面へ伸ばす（Shooting→Retractingの3段動作）。
//              届く範囲に敵がいれば捕まえて食べる／いなければ空振りで引っ込む。
//              「どこでも」出せる演出なので、敵の有無に関わらず必ず舌は伸びる。
//              Idle中は「今Eを押したら捕まえられる敵」を脈動リングで常時プレビュー表示する
//              （DebugDrawのワイヤーフレーム。食える判定の分かりづらさ対策）。
//              移動はロックしない：走りながらでも出せる（口の位置は毎フレーム追従）。
//              空振り中の狙い方向も毎フレーム player の向き(yaw)から取り直すため、
//              ジャンプで上下してもねじれず一直線のまま／振り向けば舌も一緒に振り向く。
//              （敵を捕まえた場合はホーミングが優先＝プレイヤーの向き変更では逸れない）
//   ・左Ctrl … しゃがんでお腹の敵を1匹「卵」として後ろに産む
//   ・F      … お腹の敵を1匹、向いている方向へ吐き出す（敵にぶつけて倒せる）
//
//  以前は E を押した瞬間に敵がその場で縮んで一瞬でお腹へ吸い込まれていたが、
//  「動作」として見せられるように、舌を実際に伸ばして掴み、引き込む区間を挟む
//  ステートマシンに変更した：
//    Idle → Shooting（舌が狙い位置へ一直線に伸びる。約0.12秒。
//                     敵を捕まえた場合はその位置へホーミング）
//         → Retracting（捕まえた場合：Enemy::StartSwallow に処理を渡し、舌は
//           口↔敵の間を見た目だけ追従＝既存の縮小しながら吸い込まれる演出と
//           同じ0.35秒。空振りの場合：短い引っ込みだけで終わる）
//         → Idle
//  舌はワールド座標へ直接焼く動的メッシュ（薄い十字リボン）。実体を持つのは
//  Shooting/Retracting 中だけで、Idle では空メッシュにして描画コストを0にする。
//
//  捕獲した敵は実体メッシュを隠し（Enemy::SetVisualHidden）、代わりに
//  enemyBall.sdf3d の SDFボリュームを口へ引き込みながらエロージョンで溶かして
//  消す（CombatSystem の踏みつけ消滅と同じ「SDFで溶ける」演出を、食べる時にも使う）。
//
//  シーンから分離した理由：入力→対象選択→開始通知だけの独立したアクションで、
//  他シーンでも同じ組み合わせで再利用できる。
// =====================================================================
class SwallowAbility {
public:
    SwallowAbility();
    ~SwallowAbility(); // unique_ptr<Model>/<Obj3d>/<SDFVolumeObject> のため cpp 側で定義

    // 捕まえた敵をSDFで溶かして消す演出のセットアップ（enemyBall.sdf3d を先読み。
    //   シーンの LoadResources から呼ぶ。失敗時は従来の縮小アニメへ自動フォールバック）
    void InitializeEatFx(ID3D12GraphicsCommandList* commandList);

    // 毎フレーム（Play中のみ）呼ぶ。dt はクールタイム・舌アニメの経過用
    void Update(Player& player, EnemyManager& enemies, EggSystem& eggs, HitFeel& hitFeel,
               Camera* camera, float dt);

    // 舌の描画（Shooting/Retracting中だけ描く。不透明パスで他のObj3dと一緒に呼ぶ）
    void Draw() const;

    // 捕獲した敵のSDF溶解演出の描画（専用PSOのため、シーンMRTパスの最後で呼ぶこと）
    void DrawEatFx(ID3D12GraphicsCommandList* commandList) const;

    // モード切替時などに強制的にIdleへ戻す（対象への参照も破棄する）。
    //   敵がマップ再同期で作り直される瞬間（Edit→Play）に必ず呼ぶこと
    //   （呼ばないと Shooting 中の target_ がダングリングポインタになる）
    void Reset();

    bool IsActive() const { return state_ != State::Idle; }

    // ノードエディタの「→ ゲーム値」用（舌の届く距離・再使用間隔を外から調整できる）
    float* SwallowReachPtr(){ return &swallowReach_; }
    float* SwallowCooldownPtr(){ return &swallowCooldown_; }

private:
public:
    // ベロ動作中か（プレイヤーモデルの TongueOut アニメ切り替えに使う）
    bool IsTongueActive() const{ return state_ != State::Idle; }

private:
    enum class State { Idle, Shooting, Retracting };
    State state_ = State::Idle;

    Enemy*  target_ = nullptr; // Shooting中のみ有効（Retracting中は保持しない＝ダングリング回避）
    float   phaseT_ = 0.0f;    // 現在フェーズの経過秒
    Vector3 grabPos_  { 0.0f, 0.0f, 0.0f }; // 捕獲時：掴んだ瞬間の敵位置（Retractingの起点）
    float   retractDuration_ = 0.0f;        // Retracting の秒数（捕獲=0.35秒固定 / 空振り=短め）
    bool    retractCaught_   = false;       // Retracting が捕獲後か空振りかの区別
    float   eatRadius_       = 0.6f;        // 捕獲時：敵の半径（SDF溶解の大きさに使う）
    bool    eatFxActive_     = false;       // 今回の捕獲でSDF溶解演出を使っているか（読込失敗時はfalse）
    float   highlightPulseT_ = 0.0f;        // 「捕まえられる」プレビューリングの脈動タイマー

    float swallowReach_    = 2.0f;  // 飲み込みの届く距離 (m)
    float swallowCooldown_ = 0.35f; // 飲み込み成功後の再使用間隔 (秒)。連打での連発を防ぐ
    float cooldownTimer_   = 0.0f;  // 残りクールタイム

    // 舌の見た目を口(mouth)〜先端(tip)の十字リボンとして書き直す（無ければ初回に確保）
    void UpdateTongueMesh(const Vector3& mouth, const Vector3& tip, Camera* camera);

    std::unique_ptr<Model> tongueModel_;
    std::unique_ptr<Obj3d> tongueObj_;

    // 捕まえた敵を溶かして消すSDFボリューム（1個を使い回す。同時に飲み込みは1匹だけなので十分）
    std::unique_ptr<SDFVolumeObject> eatFx_;
};
