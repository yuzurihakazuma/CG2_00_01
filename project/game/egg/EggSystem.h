#pragma once
#include "game/egg/Egg.h"
#include <vector>
#include <memory>

// =====================================================================
//  EggSystem：ヨッシーの卵を管理する（飲み込みで生成 → 後ろに整列・追従 → 投擲 → 割れる）。
//   ・各卵は自分の状態(Held/Flying/Broken)を持つ → ここは「並び順」と「全体の管理」を担当。
//   ・卵はヨッシー追従かつ敵に当たるので、両方を知るシーンが所有して使う。
// =====================================================================
class EggSystem {
public:
    static const int kMaxEggs = 6; // 同時に持てる卵の最大数（お腹の容量）

    void Initialize();   // 全卵をクリア（プレイ開始/リセット時）

    // 毎フレーム更新。保持中の卵に「後ろのスロット」を割り当てて整列・追従させ、各卵の状態を進める。
    //   playerPos : プレイヤー位置 / facing : プレイヤーの向き(水平単位ベクトル)
    void Update(const Vector3& playerPos, const Vector3& facing, float dt);
    void Draw() const;

    // 敵を飲み込んだ → プレイヤー位置から卵が生まれる（Held 状態で追加）。
    //   お腹が一杯(kMaxEggs)なら追加せず false を返す。
    bool OnSwallow(const Vector3& birthPos);

    // 保持中の卵を1個、指定方向へ投げる（Held → Flying）。speed=初速。投げられたら true。
    bool TryThrow(const Vector3& playerPos, const Vector3& dir, float speed = 12.0f);

    int HeldCount() const;    // 保持中の卵の数
    int FlyingCount() const;  // 飛行中の卵の数
    int TotalCount() const { return ( int ) eggs_.size(); }

private:
    std::vector<std::unique_ptr<Egg>> eggs_;
};
