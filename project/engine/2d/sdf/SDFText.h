#pragma once
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "engine/2d/sdf/SDFFont.h"
#include "engine/math/struct.h"   // Vector4

// SDF フォントで文字列を描画するクラス
//   SDFText text;
//   text.Initialize(font);
//   text.SetText(u8"Hello 世界");
//   text.SetPosition(100, 200);
//   text.SetFontSize(32.0f);
//   text.Update();
//   text.Draw(commandList);
class SDFText {
public:
    SDFText() = default;
    ~SDFText() = default;

    void Initialize(SDFFont* font);

    void SetText(const std::string& utf8Text);
    void SetPosition(float x, float y);
    void SetFontSize(float size);
    void SetColor(Vector4 color);
    void SetOutlineWidth(float width);
    void SetOutlineColor(Vector4 color);
    void SetEdgeWidth(float width);

    float GetTextWidth() const { return textWidth_; }
    float GetTextHeight() const { return textHeight_; }

    void Update();
    void Draw(ID3D12GraphicsCommandList* commandList);

private:
    static std::vector<uint32_t> DecodeUTF8(const std::string& str);
    void RebuildGeometry();
    void UpdateTransformBuffer();
    void BuildPipeline();

    struct Vertex {
        float x, y;
        float u, v;
    };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    struct TransformCB { float mat[16]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;
    TransformCB* transformData_ = nullptr;

    struct SDFParamsCB {
        float textColor[4];
        float outlineColor[4];
        float edgeWidth;
        float outlineWidth;
        float pad0, pad1;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsBuffer_;
    SDFParamsCB* paramsData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    bool pipelineReady_ = false;

    SDFFont* font_ = nullptr;
    std::string text_;
    float posX_ = 0.0f, posY_ = 0.0f;
    float fontSize_ = 32.0f;
    Vector4 color_        = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    float edgeWidth_    = 0.05f;
    float outlineWidth_ = 0.0f;

    uint32_t indexCount_ = 0;
    bool dirty_ = true;

    float textWidth_  = 0.0f;
    float textHeight_ = 0.0f;
};
