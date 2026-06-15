#pragma once
// =====================================================================
//  DebugDraw : ワイヤーフレームのデバッグ描画（線/箱/球/グリッド）
//   毎フレーム Line/Box/Sphere/Grid で線を積み、Render(camera) で一括描画→クリア。
//   深度テスト無し（常に手前に見える）。当たり判定・配置・経路の可視化に。
//
//   使い方:
//     DebugDraw::GetInstance()->Box(aabb, {1,0,0,1});
//     DebugDraw::GetInstance()->Sphere(center, r, {0,1,0,1});
//     ... シーンのDraw末尾で:
//     DebugDraw::GetInstance()->Render(camera);
// =====================================================================
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "engine/math/struct.h"
#include "engine/collision/Collider.h" // AABB

class DirectXCommon;
class Camera;

class DebugDraw{
public:
    static DebugDraw* GetInstance();

    void Initialize(DirectXCommon* dxCommon);
    void Finalize();

    // --- 線を積む（ワールド空間）---
    void Line(const Vector3& a, const Vector3& b, const Vector4& color);
    void Box(const AABB& aabb, const Vector4& color);
    void Box(const Vector3& center, const Vector3& size, const Vector4& color);
    void Sphere(const Vector3& center, float radius, const Vector4& color, int segments = 16);
    void Grid(float halfSize, float step, const Vector4& color, float y = 0.0f);

    // 積んだ線を camera のVPで一括描画し、キューをクリアする
    void Render(const Camera* camera);

private:
    DebugDraw() = default;
    ~DebugDraw() = default;
    DebugDraw(const DebugDraw&) = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    struct LineVertex{
        Vector3 pos;
        Vector4 color;
    };

    DirectXCommon* dxCommon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

    // 動的頂点バッファ（毎フレーム書き換え）
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    LineVertex* vertexMapped_ = nullptr;

    // VP 用定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
    void* cameraMapped_ = nullptr;

    std::vector<LineVertex> vertices_;
    static const uint32_t kMaxVertices = 200000; // 線10万本ぶん
};
