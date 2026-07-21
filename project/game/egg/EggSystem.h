#pragma once
#include "game/egg/Egg.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

class Obj3d;            // トレイル煙パフ用（実体で確実に表示する）
class SDFVolumeObject;  // 産卵エロージョン演出用（SDFの卵が芯から育つ）
struct ID3D12GraphicsCommandList;

// =====================================================================
//  EggSystem：ヨッシーの卵を管理する（飲み込みで生成 → 後ろに整列・追従 → 投擲 → 割れる）。
//   ・各卵は自分の状態(Held/Flying/Broken)を持つ → ここは「並び順」と「全体の管理」を担当。
//   ・卵はヨッシー追従かつ敵に当たるので、両方を知るシーンが所有して使う。
// =====================================================================
class EggSystem {
public:
    static const int kMaxEggs = 6; // 同時に持てる卵の最大数（お腹の容量）

    EggSystem() = default;
    ~EggSystem(); // unique_ptr<Obj3d> を含むため cpp 側で定義

    void Initialize();   // 全卵をクリア（プレイ開始/リセット時）

    // 毎フレーム更新。保持中の卵に「後ろのスロット」を割り当てて整列・追従させ、各卵の状態を進める。
    //   playerPos : プレイヤー位置 / facing : プレイヤーの向き(水平単位ベクトル)
    void Update(const Vector3& playerPos, const Vector3& facing, float dt);
    void Draw() const;

    // 敵を飲み込んだ → お腹(stomach)に1匹ためる（まだ卵にはしない）。
    //   お腹が一杯(kMaxEggs)なら古い分は増えない。
    void AddToBelly();
    int  BellyCount() const{ return stomach_; }

    // しゃがみ等で「産む」：お腹が1以上なら1匹を卵(Held)にして birthPos に生む。産んだら true。
    //   持っている卵が満杯なら一番古い卵を割って捨ててから生む（常に産める）。
    bool LayEgg(const Vector3& birthPos);
    // 産卵演出中か（プレイヤーの EggLay アニメ切り替えに使う）
    bool IsBirthActive() const{ return birthTimer_ >= 0.0f; }

    // 保持中の卵を1個、指定方向へ投げる（Held → Flying）。speed=初速。投げられたら true。
    bool TryThrow(const Vector3& playerPos, const Vector3& dir, float speed = 12.0f);

    // お腹の敵を1匹、指定方向へ吐き出す（ヨッシーの吐き出し攻撃）。吐けたら true。
    //   from=口元の位置 / dir=プレイヤーの向いている水平方向
    bool SpitOut(const Vector3& from, const Vector3& dir, float speed = 13.0f);

    // 飛行中の吐き出し弾を onHit(位置, 半径) で判定し、true が返ったら弾を消す。
    //   （卵の ResolveHits と同じ流儀。敵側の処理は CombatSystem が行う）
    void ResolveSpitHits(const std::function<bool(const Vector3&, float)>& onHit);

    // 飛行中の卵それぞれを onHit(卵位置, 卵半径) で判定し、true が返ったら卵を割る。
    //   敵を知るのはシーンなので、当たり判定の中身はシーンから渡す（割れ演出は Update が拾う）。
    void ResolveHits(const std::function<bool(const Vector3&, float)>& onHit);

    // --- イベント別の演出（実体パフ。踏みつけのリングとは別物にして使い回しに見せない）---
    void SpawnSwallowFx(const Vector3& pos); // 飲み込み：緑がふわっと上へ吸い込まれる
    void SpawnLayFx(const Vector3& pos);     // 産卵：白＆緑がぽわっと広がる（生まれた感）
    void SpawnHitFx(const Vector3& pos);     // 命中：黄＆オレンジが鋭く飛び散る（衝撃）

    int HeldCount() const;    // 保持中の卵の数
    int FlyingCount() const;  // 飛行中の卵の数
    int TotalCount() const { return ( int ) eggs_.size(); }

    // --- 産卵エロージョン演出（SDFの卵が芯から育ち、育ちきったら実体メッシュへ交代）---
    //   セットアップ：egg.sdf3d を読み込む（シーンの LoadResources から呼ぶ。
    //   読み込み失敗時は演出なし＝従来のポン出しに自動フォールバック）
    void InitializeBirthFx(ID3D12GraphicsCommandList* commandList);
    //   描画：専用PSOへ切り替えるため、シーンMRTパスの最後（他のObj3dの後）で呼ぶこと
    void DrawBirthFx(ID3D12GraphicsCommandList* commandList);

private:
    std::vector<std::unique_ptr<Egg>> eggs_;
    int stomach_ = 0; // お腹にためた敵の数（産むと卵になる）

    // --- 産卵エロージョン演出の状態 ---
    std::unique_ptr<SDFVolumeObject> birthFx_; // SDFの卵（1個を使い回す）
    Egg*  birthEgg_   = nullptr;               // 演出対象（eggs_ 内に存在するか毎フレーム検証）
    float birthTimer_ = -1.0f;                 // 経過秒。負=演出していない

    // --- 飛行中の卵が残す「煙パフ」---
    //   加算パーティクルは明るい背景で見えないので、実体(Obj3d)の小球で表示する（縮んで消える）。
    struct TrailPuff {
        std::unique_ptr<Obj3d> obj;
        Vector3 pos { 0.0f, 0.0f, 0.0f };
        Vector3 vel { 0.0f, 0.0f, 0.0f };
        Vector3 rot { 0.0f, 0.0f, 0.0f };      // 姿勢（欠片が舞うように回す）
        Vector3 rotSpeed { 0.0f, 0.0f, 0.0f };
        float   life    = 0.0f;
        float   maxLife = 0.4f;
        float   baseScale = 0.3f;
    };
    std::vector<TrailPuff> puffs_;
    float trailTimer_ = 0.0f; // 一定間隔でトレイルを出すためのタイマー

    // --- 吐き出し弾（飲んだ敵をそのまま前方へ発射。モンスターボール柄の球）---
    struct SpitBall {
        std::unique_ptr<Obj3d> obj;
        Vector3 pos { 0.0f, 0.0f, 0.0f };
        Vector3 vel { 0.0f, 0.0f, 0.0f };
        float   life   = 2.0f;   // 保険の寿命(秒)
        float   radius = 0.45f;  // 当たり判定＆見た目の半径
        float   spin   = 0.0f;   // 転がり回転
        bool    dead   = false;
    };
    std::vector<SpitBall> spits_;

    // パフを1個出す（pos=位置, vel=初速, color=色, scale=大きさ, life=寿命秒, modelName=見た目）。
    //   modelName="fxSphere"（白い専用の粒。敵の"sphere"とは別物）の時だけ color でタイントする。
    //   "eggShell" 等の専用モデルは自前の色をそのまま見せる。
    void SpawnPuff(const Vector3& pos, const Vector3& vel, const Vector4& color, float scale, float life,
                   const std::string& modelName = "fxSphere");
};
