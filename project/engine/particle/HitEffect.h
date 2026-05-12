#pragma once
#include "Engine/3D/Obj/Obj3d.h"
#include "engine/math/VectorMath.h"
#include "engine/math/struct.h"
#include <vector>
#include <memory>



class Camera;

class HitEffect {
public:
    // 発生位置、カメラ、テクスチャ、環境マップを渡して初期化
    void Initialize(const Vector3& position, Camera* camera, uint32_t textureIndex, uint32_t envMapIndex);

    void Update();
    void Draw();

    // 全ての火花が消えたら true を返す（削除判定用）
    bool IsDead() const { return isDead_; }

private:
    struct Spark {
        std::unique_ptr<Obj3d> obj;
        float lifeTime;
        float currentTime;
        Vector3 baseScale;
    };

    std::vector<Spark> sparks_;
    bool isDead_ = false;
};