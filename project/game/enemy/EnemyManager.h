#pragma once
#include "game/enemy/Enemy.h"
#include "game/enemy/EnemyEditor.h" // EnemySpawnData
#include <vector>
#include <memory>
#include <functional>

class SplineRail;
class BlockSystem;

// =====================================================================
//  EnemyManager：敵の実体の生成・更新・描画をまとめて管理する。
//   ・配置テンプレート(EnemySpawnData)から実体を再構築する
//   ・毎フレームの移動＋「飲み込まれ中」の吸い込みTick＋消化（完了で削除）を担当
//   ・踏みつけ/卵命中/ロックオン等の走査は GetEnemies() を通して各システムが行う
//  シーンから分離した理由：敵のライフサイクル管理は独立した関心事で、他シーンでも再利用できる。
// =====================================================================
class EnemyManager {
public:
    // 配置テンプレートから敵を再構築する（マップ同期時に呼ぶ）
    void Spawn(const std::vector<EnemySpawnData>& spawns, const std::vector<SplineRail>& rails);

    // 移動＋吸い込みTick＋消化。
    //   吸い込みが完了した敵は onConsumed(その位置) を呼んでから削除する
    //   （お腹+1 や演出はシーン/EggSystem 側が行う）。
    //   blocks を渡すとパトロール中の敵がブロックで引き返す（貫通防止）
    void Update(const std::vector<SplineRail>& rails, const Vector3& playerPos, float dt,
                const std::function<void(const Vector3&)>& onConsumed,
                const BlockSystem* blocks = nullptr);

    void Draw();
    void Clear(){ enemies_.clear(); }

    // 各システム（踏みつけ・飲み込み・ロックオン・卵命中）が走査に使う
    std::vector<std::unique_ptr<Enemy>>& GetEnemies(){ return enemies_; }
    const std::vector<std::unique_ptr<Enemy>>& GetEnemies() const{ return enemies_; }

private:
    std::vector<std::unique_ptr<Enemy>> enemies_;
};
