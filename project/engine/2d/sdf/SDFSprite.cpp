#include "SDFSprite.h"

#include <cassert>
#include <cstring>
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

void SDFSprite::BuildPipeline() {
    auto dxCommon = DirectXCommon::GetInstance();
    auto device   = dxCommon->GetDevice();

    RootSignatureBuilder rsBuilder;
    rsBuilder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);
    rsBuilder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL);
    rsBuilder.AddDefaultSampler(0);
    rsBuilder.Build(device, rootSignature_);

    static D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto& compiler = dxCommon->GetShaderCompiler();
    // 頂点シェーダーは SDFText と共通 (pos+uv を行列変換するだけ)
    auto vsBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFText.VS.hlsl",   L"vs_6_0");
    auto psBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFSprite.PS.hlsl", L"ps_6_0");

    GraphicsPipelineBuilder psoBuilder;
    psoBuilder
        .SetRootSignature(rootSignature_.Get())
        .SetShaders(vsBlob.Get(), psBlob.Get())
        .SetInputLayout(inputLayout, _countof(inputLayout))
        .SetBlendMode(BlendMode::kNormal)
        .SetCullMode(D3D12_CULL_MODE_NONE)
        .SetDepthStencil(false, false)
        .SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });
    psoBuilder.Build(device, pipelineState_);

    pipelineReady_ = true;
}

void SDFSprite::Initialize(SDFSpriteAtlas* atlas, const std::string& spriteName) {
    assert(atlas);
    atlas_      = atlas;
    spriteName_ = spriteName;
    info_       = atlas->GetSprite(spriteName);

    auto factory = ResourceFactory::GetInstance();
    transformBuffer_ = factory->CreateBufferResource(sizeof(TransformCB));
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    paramsBuffer_ = factory->CreateBufferResource(sizeof(SDFParamsCB));
    paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));

    BuildPipeline();
    dirty_ = true;
}

void SDFSprite::InitializeRaw(uint32_t srvIndex, float texWidth, float texHeight) {
    rawMode_   = true;
    rawSrv_    = srvIndex;
    rawAspect_ = (texHeight > 0.0f) ? (texWidth / texHeight) : 1.0f;

    auto factory = ResourceFactory::GetInstance();
    transformBuffer_ = factory->CreateBufferResource(sizeof(TransformCB));
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    paramsBuffer_ = factory->CreateBufferResource(sizeof(SDFParamsCB));
    paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));

    BuildPipeline();
    dirty_ = true;
}

void SDFSprite::SetTransform3D(const Vector3& worldPos, float worldScale, Camera* camera) {
    use3D_        = true;
    worldPos3D_   = worldPos;
    worldScale3D_ = worldScale;
    camera3D_     = camera;
    dirty_        = true;
}

void SDFSprite::SetPosition(float x, float y) {
    if (posX_ != x || posY_ != y) { posX_ = x; posY_ = y; dirty_ = true; }
}
void SDFSprite::SetScale(float scale) {
    if (scale_ != scale) { scale_ = scale; dirty_ = true; }
}
void SDFSprite::SetColor(Vector4 color) { color_ = color; }
void SDFSprite::SetOutline(float width, Vector4 color) { outlineWidth_ = width; outlineColor_ = color; }
void SDFSprite::SetGlow(float width, Vector4 color)    { glowWidth_ = width; glowColor_ = color; }
void SDFSprite::SetEdgeBias(float bias) { edgeBias_ = bias; }

void SDFSprite::RebuildGeometry() {
    if (!info_) { indexCount_ = 0; return; }

    float x0 = posX_ + info_->offsetX * scale_;
    float y0 = posY_ + info_->offsetY * scale_;
    drawW_   = info_->width  * scale_;
    drawH_   = info_->height * scale_;
    float x1 = x0 + drawW_;
    float y1 = y0 + drawH_;

    Vertex vertices[4] = {
        { x0, y0, info_->u0, info_->v0 },
        { x1, y0, info_->u1, info_->v0 },
        { x0, y1, info_->u0, info_->v1 },
        { x1, y1, info_->u1, info_->v1 },
    };
    uint32_t indices[6] = { 0, 1, 2, 1, 3, 2 };

    auto factory = ResourceFactory::GetInstance();

    vertexBuffer_ = factory->CreateBufferResource(sizeof(vertices));
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices, sizeof(vertices));
    vertexBuffer_->Unmap(0, nullptr);
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes    = sizeof(vertices);
    vbv_.StrideInBytes  = sizeof(Vertex);

    indexBuffer_ = factory->CreateBufferResource(sizeof(indices));
    indexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, indices, sizeof(indices));
    indexBuffer_->Unmap(0, nullptr);
    ibv_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibv_.SizeInBytes    = sizeof(indices);
    ibv_.Format         = DXGI_FORMAT_R32_UINT;

    indexCount_ = 6;
}

void SDFSprite::UpdateTransformBuffer() {
    if (!transformData_) return;
    auto dxCommon = DirectXCommon::GetInstance();
    float W = static_cast<float>(dxCommon->GetClientWidth());
    float H = static_cast<float>(dxCommon->GetClientHeight());

    std::memset(transformData_, 0, sizeof(TransformCB));
    transformData_->mat[0]  =  2.0f / W;
    transformData_->mat[5]  = -2.0f / H;
    transformData_->mat[10] =  1.0f;
    transformData_->mat[12] = -1.0f;
    transformData_->mat[13] =  1.0f;
    transformData_->mat[15] =  1.0f;
}

void SDFSprite::Update() {
    if (dirty_) {
        RebuildGeometry();
        dirty_ = false;
    }
    UpdateTransformBuffer();

    if (!paramsData_) return;
    auto setV4 = [](float* dst, const Vector4& c) {
        dst[0] = c.x; dst[1] = c.y; dst[2] = c.z; dst[3] = c.w;
    };
    setV4(paramsData_->baseColor,    color_);
    setV4(paramsData_->outlineColor, outlineColor_);
    setV4(paramsData_->glowColor,    glowColor_);
    paramsData_->edgeBias     = edgeBias_;
    paramsData_->outlineWidth = outlineWidth_;
    paramsData_->glowWidth    = glowWidth_;
    bool keep = atlas_ && atlas_->IsKeepColor();
    paramsData_->sdfInAlpha  = keep ? 1.0f : 0.0f;
    paramsData_->useTexColor = keep ? 1.0f : 0.0f;
}

void SDFSprite::Draw(ID3D12GraphicsCommandList* commandList) {
    if (!pipelineReady_ || indexCount_ == 0 || !atlas_) return;

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);

    commandList->SetGraphicsRootConstantBufferView(0, transformBuffer_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, paramsBuffer_->GetGPUVirtualAddress());
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, atlas_->GetAtlasSrvIndex());

    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
