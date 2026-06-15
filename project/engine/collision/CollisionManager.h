#pragma once
// =====================================================================
//  CollisionManager : Collider を登録して毎フレーム総当たり判定するマネージャ
//   ・Register したコライダー同士を CheckAllCollisions() で総当たり
//   ・属性/マスクが合い、形状が重なっていれば両者の OnHit を呼ぶ
//   ・DrawDebug() で DebugDraw に形状を積む（可視化）
// =====================================================================
#include <vector>
#include "engine/math/struct.h"

class Collider;

class CollisionManager{
public:
    static CollisionManager* GetInstance();

    void Register(Collider* collider);   // 判定対象に追加
    void Unregister(Collider* collider); // 判定対象から外す
    void Clear();                        // 全部外す（シーン切替時など）

    // 登録済みコライダーを総当たりで判定し、重なりがあれば OnHit を呼ぶ
    void CheckAllCollisions();

    // 登録中の形状を DebugDraw に積む（色指定）
    void DrawDebug(const Vector4& color = { 0.2f, 1.0f, 0.4f, 0.8f });

private:
    CollisionManager() = default;
    ~CollisionManager() = default;
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // 2つのコライダーが（属性/マスクOKの上で）重なっているか
    bool IsHit(Collider* a, Collider* b) const;

    std::vector<Collider*> colliders_;
};
