#include "CollisionManager.h"

#include <algorithm>

#include "engine/collision/Collider.h"
#include "engine/collision/Collision.h"
#include "engine/graphics/DebugDraw.h"

CollisionManager* CollisionManager::GetInstance(){
    static CollisionManager instance;
    return &instance;
}

void CollisionManager::Register(Collider* collider){
    if ( !collider ) return;
    if ( std::find(colliders_.begin(), colliders_.end(), collider) == colliders_.end() ) {
        colliders_.push_back(collider);
    }
}

void CollisionManager::Unregister(Collider* collider){
    colliders_.erase(std::remove(colliders_.begin(), colliders_.end(), collider), colliders_.end());
}

void CollisionManager::Clear(){
    colliders_.clear();
}

// 形状の組み合わせごとに既存の Collision::IsCollision を使い分ける
bool CollisionManager::IsHit(Collider* a, Collider* b) const{
    bool aSphere = ( a->GetShape() == Collider::Shape::Sphere );
    bool bSphere = ( b->GetShape() == Collider::Shape::Sphere );

    if ( aSphere && bSphere ) {
        return Collision::IsCollision(a->GetSphere(), b->GetSphere());
    } else if ( !aSphere && !bSphere ) {
        return Collision::IsCollision(a->GetAABB(), b->GetAABB());
    } else if ( aSphere && !bSphere ) {
        return Collision::IsCollision(a->GetSphere(), b->GetAABB());
    } else { // !aSphere && bSphere
        return Collision::IsCollision(b->GetSphere(), a->GetAABB());
    }
}

void CollisionManager::CheckAllCollisions(){
    const size_t n = colliders_.size();
    for ( size_t i = 0; i < n; ++i ) {
        Collider* a = colliders_[i];
        if ( !a || !a->IsEnabled() ) continue;

        for ( size_t j = i + 1; j < n; ++j ) {
            Collider* b = colliders_[j];
            if ( !b || !b->IsEnabled() ) continue;

            // 属性/マスクのフィルタ（双方向に許可されている時だけ）
            if ( ( a->GetAttribute() & b->GetMask() ) == 0 ) continue;
            if ( ( b->GetAttribute() & a->GetMask() ) == 0 ) continue;

            if ( IsHit(a, b) ) {
                a->OnHit(b);
                b->OnHit(a);
            }
        }
    }
}

void CollisionManager::DrawDebug(const Vector4& color){
    DebugDraw* dd = DebugDraw::GetInstance();
    for ( Collider* c : colliders_ ) {
        if ( !c || !c->IsEnabled() ) continue;
        if ( c->GetShape() == Collider::Shape::Sphere ) {
            Sphere s = c->GetSphere();
            dd->Sphere(s.center, s.radius, color);
        } else {
            dd->Box(c->GetAABB(), color);
        }
    }
}
