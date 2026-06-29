#pragma once
#include "game/egg/Egg.h"
#include <vector>

// =====================================================================
//  EggSystem：ヨッシーの卵を管理する（飲み込みで生成 → 保持 → 投擲 → 割れる）。
//   今は状態管理のみ（実体＝モデル描画はまだ持たない）。シーンが所有する。
//   ・卵は「ヨッシー追従」かつ「敵に当たる」ので、両方を知るシーンが仲介して使う。
// =====================================================================
class EggSystem {
public:
    void Initialize();   // 全卵をクリア（プレイ開始/リセット時）

    // 毎フレーム更新。保持中の卵はヨッシーの後ろへ追従させ、各卵の状態を進める。
    //   playerPos    : プレイヤー位置 / facing : プレイヤーの向き(水平単位ベクトル)
    void Update(const Vector3& playerPos, const Vector3& facing, float dt);

    // 敵を飲み込んだ → お腹に卵を1個（Held 状態）追加する
    void OnSwallow(const Vector3& atPos);

    // 保持中の卵を1個、前方へ投げる（Held → Flying）。投げられたら true。
    bool TryThrow(const Vector3& playerPos, const Vector3& facing);

    int HeldCount() const;    // 保持中の卵の数（UI表示・投擲可否に使う）
    int FlyingCount() const;  // 飛行中の卵の数
    int TotalCount() const { return ( int ) eggs_.size(); }

    // 実体化（描画）の手順で使う：今ある卵を参照する
    const std::vector<Egg>& GetEggs() const { return eggs_; }
    std::vector<Egg>&       GetEggs()       { return eggs_; }

private:
    std::vector<Egg> eggs_;
};
