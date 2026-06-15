#pragma once
#include"engine/math/struct.h"
#include <functional>
#include <cstdint>

// 軸に平行な箱
struct AABB {
    Vector3 min; // 最小座標 (左下奥)
    Vector3 max; // 最大座標 (右上隅)
};

struct Ray {
    Vector3 origin;    // 始点（発射位置）
    Vector3 direction; // 方向（※必ず正規化されたベクトルを入れる）
    float length;      // 届く距離（射程）
};

// =====================================================================
//  Collider : 当たり判定コンポーネント
//   形状（球 or AABB）と中心位置・属性/マスク・当たった時のコールバックを持つ。
//   CollisionManager に Register すると、毎フレーム自動で総当たり判定され、
//   重なった相手同士で OnHit が呼ばれる。
//
//   使い方:
//     collider_.SetSphere({0,0,0}, 1.0f);
//     collider_.SetOwner(this);
//     collider_.SetOnHit([](Collider* other){ ... });
//     CollisionManager::GetInstance()->Register(&collider_);
//     ... 毎フレーム位置更新:
//     collider_.SetCenter(position_);
// =====================================================================
class Collider{
public:
    enum class Shape{ Sphere, AABB };

    // --- 形状設定 ---
    void SetSphere(const Vector3& center, float radius){
        shape_ = Shape::Sphere; center_ = center; radius_ = radius;
    }
    void SetAABB(const Vector3& center, const Vector3& size){
        shape_ = Shape::AABB; center_ = center; halfSize_ = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
    }
    void SetCenter(const Vector3& center){ center_ = center; }
    void SetRadius(float r){ radius_ = r; }

    Shape   GetShape() const { return shape_; }
    Vector3 GetCenter() const { return center_; }

    Sphere GetSphere() const { return { center_, radius_ }; }
    AABB   GetAABB() const {
        return { { center_.x - halfSize_.x, center_.y - halfSize_.y, center_.z - halfSize_.z },
                 { center_.x + halfSize_.x, center_.y + halfSize_.y, center_.z + halfSize_.z } };
    }

    // --- フィルタリング（属性ビット & マスクビット）---
    //   a と b が当たるのは ( a.attribute & b.mask ) かつ ( b.attribute & a.mask ) の時。
    void     SetAttribute(uint32_t a){ attribute_ = a; }
    uint32_t GetAttribute() const { return attribute_; }
    void     SetMask(uint32_t m){ mask_ = m; }
    uint32_t GetMask() const { return mask_; }

    // --- 当たった時のコールバック ---
    void SetOnHit(std::function<void(Collider*)> cb){ onHit_ = std::move(cb); }
    void OnHit(Collider* other){ if ( onHit_ ) onHit_(other); }

    // --- 所有者（this ポインタなどを入れておくと相手の正体が分かる）---
    void  SetOwner(void* owner){ owner_ = owner; }
    void* GetOwner() const { return owner_; }

    // 有効/無効（無効中は判定対象外）
    void SetEnabled(bool e){ enabled_ = e; }
    bool IsEnabled() const { return enabled_; }

private:
    Shape   shape_ = Shape::Sphere;
    Vector3 center_ { 0.0f, 0.0f, 0.0f };
    float   radius_ = 1.0f;                 // 球用
    Vector3 halfSize_ { 0.5f, 0.5f, 0.5f }; // AABB用

    uint32_t attribute_ = 0xffffffff;
    uint32_t mask_ = 0xffffffff;

    std::function<void(Collider*)> onHit_;
    void* owner_ = nullptr;
    bool  enabled_ = true;
};