#include "SDFText.h"

#include <cassert>
#include <cstring>
#include <algorithm>
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"

// ============================================================
// UTF-8 デコード
// ============================================================
std::vector<uint32_t> SDFText::DecodeUTF8(const std::string& str) {
    std::vector<uint32_t> codepoints;
    size_t i = 0;
    while (i < str.size()) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c; ++i;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < str.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(str[i+1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < str.size()) {
            cp = ((c & 0x0F) << 12)
               | ((static_cast<unsigned char>(str[i+1]) & 0x3F) << 6)
               |  (static_cast<unsigned char>(str[i+2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < str.size()) {
            cp = ((c & 0x07) << 18)
               | ((static_cast<unsigned char>(str[i+1]) & 0x3F) << 12)
               | ((static_cast<unsigned char>(str[i+2]) & 0x3F) << 6)
               |  (static_cast<unsigned char>(str[i+3]) & 0x3F);
            i += 4;
        } else { ++i; continue; }
        codepoints.push_back(cp);
    }
    return codepoints;
}

// ============================================================
// パイプライン構築
// ============================================================
void SDFText::BuildPipeline() {
    auto dxCommon = DirectXCommon::GetInstance();
    auto device   = dxCommon->GetDevice();

    // b0: 変換行列(VS) / b1: SDFParams(PS) / t0: アトラス / s0: サンプラー
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
    auto vsBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFText.VS.hlsl", L"vs_6_0");
    auto psBlob = compiler.CompileShader(L"resources/shaders/SDF/SDFText.PS.hlsl", L"ps_6_0");

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

// ============================================================
// 初期化
// ============================================================
void SDFText::Initialize(SDFFont* font) {
    assert(font);
    font_ = font;

    auto factory = ResourceFactory::GetInstance();
    transformBuffer_ = factory->CreateBufferResource(sizeof(TransformCB));
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&transformData_));
    paramsBuffer_ = factory->CreateBufferResource(sizeof(SDFParamsCB));
    paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&paramsData_));

    BuildPipeline();
    dirty_ = true;
}

// ============================================================
// 設定
// ============================================================
void SDFText::SetText(const std::string& utf8Text) {
    if (text_ != utf8Text) { text_ = utf8Text; dirty_ = true; }
}
void SDFText::SetPosition(float x, float y) {
    if (posX_ != x || posY_ != y) { posX_ = x; posY_ = y; dirty_ = true; }
}
void SDFText::SetFontSize(float size) {
    if (fontSize_ != size) { fontSize_ = size; dirty_ = true; }
}
void SDFText::SetColor(Vector4 color)        { color_ = color; }
void SDFText::SetOutlineWidth(float width)   { outlineWidth_ = width; }
void SDFText::SetOutlineColor(Vector4 color) { outlineColor_ = color; }
void SDFText::SetEdgeWidth(float width)      { edgeWidth_ = width; }

// ============================================================
// 頂点バッファ再構築
// ============================================================
void SDFText::RebuildGeometry() {
    if (!font_ || text_.empty()) {
        indexCount_ = 0;
        return;
    }

    auto codepoints = DecodeUTF8(text_);

    float baseSize = font_->GetBaseSize() > 0.0f ? font_->GetBaseSize() : 48.0f;
    float scale = fontSize_ / baseSize;
    float lineGap = fontSize_;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(codepoints.size() * 4);
    indices.reserve(codepoints.size() * 6);

    float cursorX = posX_;
    float lineTop = posY_;
    float maxW = 0.0f;

    for (uint32_t cp : codepoints) {
        if (cp == '\n') {
            maxW = (std::max)(maxW, cursorX - posX_);
            cursorX = posX_;
            lineTop += lineGap;
            continue;
        }

        const SDFGlyph* g = font_->GetGlyph(cp);
        if (!g) {
            cursorX += fontSize_ * 0.5f;
            continue;
        }
        if (g->width <= 0.0f || g->height <= 0.0f) {
            cursorX += g->advance * scale;
            continue;
        }

        // 上から下モデル: 行上端 + offsetY が glyph の上端
        float x0 = cursorX + g->offsetX * scale;
        float y0 = lineTop  + g->offsetY * scale;
        float x1 = x0 + g->width  * scale;
        float y1 = y0 + g->height * scale;

        uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ x0, y0, g->u0, g->v0 });
        vertices.push_back({ x1, y0, g->u1, g->v0 });
        vertices.push_back({ x0, y1, g->u0, g->v1 });
        vertices.push_back({ x1, y1, g->u1, g->v1 });

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);

        cursorX += g->advance * scale;
    }
    maxW = (std::max)(maxW, cursorX - posX_);
    textWidth_  = maxW;
    textHeight_ = (lineTop - posY_) + fontSize_;

    if (vertices.empty()) { indexCount_ = 0; return; }

    auto factory = ResourceFactory::GetInstance();

    size_t vbSize = vertices.size() * sizeof(Vertex);
    vertexBuffer_ = factory->CreateBufferResource(vbSize);
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices.data(), vbSize);
    vertexBuffer_->Unmap(0, nullptr);
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes    = static_cast<UINT>(vbSize);
    vbv_.StrideInBytes  = sizeof(Vertex);

    size_t ibSize = indices.size() * sizeof(uint32_t);
    indexBuffer_ = factory->CreateBufferResource(ibSize);
    indexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, indices.data(), ibSize);
    indexBuffer_->Unmap(0, nullptr);
    ibv_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibv_.SizeInBytes    = static_cast<UINT>(ibSize);
    ibv_.Format         = DXGI_FORMAT_R32_UINT;

    indexCount_ = static_cast<uint32_t>(indices.size());
}

// ============================================================
// 正射影行列を b0 に転送
// ============================================================
void SDFText::UpdateTransformBuffer() {
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

// ============================================================
// Update / Draw
// ============================================================
void SDFText::Update() {
    if (dirty_) {
        RebuildGeometry();
        dirty_ = false;
    }
    UpdateTransformBuffer();

    if (!paramsData_) return;
    paramsData_->textColor[0]    = color_.x;
    paramsData_->textColor[1]    = color_.y;
    paramsData_->textColor[2]    = color_.z;
    paramsData_->textColor[3]    = color_.w;
    paramsData_->outlineColor[0] = outlineColor_.x;
    paramsData_->outlineColor[1] = outlineColor_.y;
    paramsData_->outlineColor[2] = outlineColor_.z;
    paramsData_->outlineColor[3] = outlineColor_.w;
    paramsData_->edgeWidth    = edgeWidth_;
    paramsData_->outlineWidth = outlineWidth_;
}

void SDFText::Draw(ID3D12GraphicsCommandList* commandList) {
    if (!pipelineReady_ || indexCount_ == 0 || !font_) return;

    commandList->SetGraphicsRootSignature(rootSignature_.Get());
    commandList->SetPipelineState(pipelineState_.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);

    commandList->SetGraphicsRootConstantBufferView(0, transformBuffer_->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, paramsBuffer_->GetGPUVirtualAddress());
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, font_->GetAtlasSrvIndex());

    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
