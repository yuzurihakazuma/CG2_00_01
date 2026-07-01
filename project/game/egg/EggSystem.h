#pragma once
#include "game/egg/Egg.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

class Obj3d; // トレイル煙パフ用（実体で確実に表示する）

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

    // 保持中の卵を1個、指定方向へ投げる（Held → Flying）。speed=初速。投げられたら true。
    bool TryThrow(const Vector3& playerPos, const Vector3& dir, float speed = 12.0f);

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

private:
    std::vector<std::unique_ptr<Egg>> eggs_;
    int stomach_ = 0; // お腹にためた敵の数（産むと卵になる）

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

    // パフを1個出す（pos=位置, vel=初速, color=色, scale=大きさ, life=寿命秒, modelName=見た目）。
    //   modelName="fxSphere"（白い専用の粒。敵の"sphere"とは別物）の時だけ color でタイントする。
    //   "eggShell" 等の専用モデルは自前の色をそのまま見せる。
    void SpawnPuff(const Vector3& pos, const Vector3& vel, const Vector4& color, float scale, float life,
                   const std::string& modelName = "fxSphere");
};
