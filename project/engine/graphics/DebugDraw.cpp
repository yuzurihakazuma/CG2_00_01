#include "DebugDraw.h"

#include <cmath>
#include <algorithm>

#include "engine/base/DirectXCommon.h"
#include "engine/base/WindowProc.h"
#include "engine/camera/Camera.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/math/struct.h"

DebugDraw* DebugDraw::GetInstance(){
    static DebugDraw instance;
    return &instance;
}

void DebugDraw::Initialize(DirectXCommon* dxCommon){
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon_->GetDevice();

    // --- ルートシグネチャ（b0 = VP行列）---
    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);
    rsBuilder.Build(device, rootSignature_);

    // --- シェーダーコンパイル ---
    auto vs = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Debug/DebugLine.VS.hlsl", L"vs_6_0");
    auto ps = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Debug/DebugLine.PS.hlsl", L"ps_6_0");

    // --- 入力レイアウト（POSITION float3 / COLOR float4）---
    D3D12_INPUT_ELEMENT_DESC elems[2] = {};
    elems[0].SemanticName = "POSITION";
    elems[0].SemanticIndex = 0;
    elems[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[0].AlignedByteOffset = 0;
    elems[0].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    elems[1].SemanticName = "COLOR";
    elems[1].SemanticIndex = 0;
    elems[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[1].AlignedByteOffset = 12;
    elems[1].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    // --- PSO（線・αブレンド・カリングなし）---
    //   シーンのMRT（color/mask の2枚・SRGB）と D24深度に描く。
    //   深度テストON・書き込みOFF → 3D形状にちゃんと隠れる（正しい遮蔽）。
    GraphicsPipelineBuilder builder;
    builder.SetRootSignature(rootSignature_.Get())
        .SetShaders(vs.Get(), ps.Get())
        .SetInputLayout(elems, 2)
        .SetBlendMode(BlendMode::kNormal)
        .SetTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(true, false) // 深度テストあり・書き込みなし
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    builder.Build(device, pipelineState_);

    // --- 動的頂点バッファ ---
    vertexResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(LineVertex) * kMaxVertices);
    vertexResource_->Map(0, nullptr, reinterpret_cast< void** >( &vertexMapped_ ));
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(LineVertex) * kMaxVertices;
    vertexBufferView_.StrideInBytes = sizeof(LineVertex);

    // --- VP用定数バッファ ---
    cameraResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(Matrix4x4));
    cameraResource_->Map(0, nullptr, &cameraMapped_);

    vertices_.reserve(4096);
}

void DebugDraw::Finalize(){
    vertices_.clear();
    vertexMapped_ = nullptr;
    cameraMapped_ = nullptr;
    vertexResource_.Reset();
    cameraResource_.Reset();
    pipelineState_.Reset();
    rootSignature_.Reset();
}

void DebugDraw::Line(const Vector3& a, const Vector3& b, const Vector4& color){
    if ( vertices_.size() + 2 > kMaxVertices ) return;
    vertices_.push_back({ a, color });
    vertices_.push_back({ b, color });
}

void DebugDraw::Box(const AABB& aabb, const Vector4& color){
    const Vector3& mn = aabb.min;
    const Vector3& mx = aabb.max;
    // 8頂点
    Vector3 p[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z }, // 下面
        { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }, // 上面
    };
    // 下面の4辺
    Line(p[0], p[1], color); Line(p[1], p[2], color); Line(p[2], p[3], color); Line(p[3], p[0], color);
    // 上面の4辺
    Line(p[4], p[5], color); Line(p[5], p[6], color); Line(p[6], p[7], color); Line(p[7], p[4], color);
    // 縦の4辺
    Line(p[0], p[4], color); Line(p[1], p[5], color); Line(p[2], p[6], color); Line(p[3], p[7], color);
}

void DebugDraw::Box(const Vector3& center, const Vector3& size, const Vector4& color){
    Vector3 h = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
    AABB aabb;
    aabb.min = { center.x - h.x, center.y - h.y, center.z - h.z };
    aabb.max = { center.x + h.x, center.y + h.y, center.z + h.z };
    Box(aabb, color);
}

void DebugDraw::Sphere(const Vector3& c, float radius, const Vector4& color, int segments){
    if ( segments < 4 ) segments = 4;
    const float kPi2 = 6.28318530718f;
    // 3つの円（XY, YZ, XZ）を描く
    for ( int axis = 0; axis < 3; ++axis ) {
        Vector3 prev {};
        for ( int i = 0; i <= segments; ++i ) {
            float a = kPi2 * static_cast< float >( i ) / static_cast< float >( segments );
            float s = std::sin(a) * radius, co = std::cos(a) * radius;
            Vector3 p = c;
            if ( axis == 0 )      { p.x += co; p.y += s; }      // XY平面
            else if ( axis == 1 ) { p.y += co; p.z += s; }      // YZ平面
            else                  { p.x += co; p.z += s; }      // XZ平面
            if ( i > 0 ) Line(prev, p, color);
            prev = p;
        }
    }
}

void DebugDraw::Grid(float halfSize, float step, const Vector4& color, float y){
    if ( step <= 0.0f ) step = 1.0f;
    for ( float v = -halfSize; v <= halfSize + 0.001f; v += step ) {
        Line({ v, y, -halfSize }, { v, y, halfSize }, color); // Z方向の線
        Line({ -halfSize, y, v }, { halfSize, y, v }, color); // X方向の線
    }
}

void DebugDraw::Render(const Camera* camera){
    if ( vertices_.empty() || !camera || !pipelineState_ ) { vertices_.clear(); return; }

    // 頂点を転送
    uint32_t count = static_cast< uint32_t >( std::min<size_t>( vertices_.size(), kMaxVertices ) );
    std::memcpy(vertexMapped_, vertices_.data(), sizeof(LineVertex) * count);

    // VP を転送（エンジンの行ベクトル規約のまま）
    *reinterpret_cast< Matrix4x4* >( cameraMapped_ ) = camera->GetViewProjectionMatrix();

    auto commandList = dxCommon_->GetCommandList();

    // ★この Render はシーンのMRTパス内（2RTV＋D24深度がバインド済み）で呼ばれる前提。
    //   描画先(OM)はいじらず、現在バインドされているシーンRTにそのまま線を描く。
    //   → ポストエフェクト/Bloom を通って Game View にも単体表示にも反映される。
    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->SetGraphicsRootConstantBufferView(0, cameraResource_->GetGPUVirtualAddress());
    commandList->DrawInstanced(count, 1, 0, 0);

    vertices_.clear();
}
