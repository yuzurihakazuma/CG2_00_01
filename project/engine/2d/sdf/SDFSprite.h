#pragma once
#include <string>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include "engine/2d/sdf/SDFSpriteAtlas.h"
#include "engine/math/struct.h"   // Vector4 / Vector3 / Matrix4x4

class Camera;

// SDF スプライト（1 枚絵）を描画するクラス
//   SDFSprite spr;
//   spr.Initialize(atlas, "star_icon");
//   spr.SetPosition(100, 100);
//   spr.SetScale(0.5f);
//   spr.SetOutline(0.1f, {0,0,0,1});
//   spr.SetGlow(0.2f, {1,1,0,1});
//   spr.Update();
//   spr.Draw(commandList);
class SDFSprite {
public:
    SDFSprite() = default;
    ~SDFSprite() = default;

    void Initialize(SDFSpriteAtlas* atlas, const std::string& spriteName);

    // 比較用: SDFを使わず、普通のテクスチャ(SRV)をそのまま描くモードで初期化する。
    // texWidth/texHeight は縦横比のために使う。
    void InitializeRaw(uint32_t srvIndex, float texWidth, float texHeight);

    // 3D ワールド空間に配置する（カメラのビュー射影で投影）。
    // これで「3Dの板に貼ってもSDFはガビガビにならない」ことを確認できる。
    void SetTransform3D(const Vector3& worldPos, float worldScale, Camera* camera);

    void SetPosition(float x, float y);
    void SetScale(float scale);
    void SetColor(Vector4 color);
    void SetOutline(float width, Vector4 color);
    void SetGlow(float width, Vector4 color);
    void SetEdgeBias(float bias);

    float GetWidth()  const { return drawW_; }
    float GetHeight() const { return drawH_; }

    void Update();
    void Draw(ID3D12GraphicsCommandList* commandList);

private:
    void BuildPipeline();
    void RebuildGeometry();
    void UpdateTransformBuffer();

    struct Vertex { float x, y; float u, v; };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    struct TransformCB { float mat[16]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;
    TransformCB* transformData_ = nullptr;

    struct SDFParamsCB {
        float baseColor[4];
        float outlineColor[4];
        float glowColor[4];
        float edgeBias;
        float outlineWidth;
        float glowWidth;
        float sdfInAlpha;
        float useTexColor;
        float rawSample;
        float pad1, pad2;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsBuffer_;
    SDFParamsCB* paramsData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    bool pipelineReady_ = false;

    SDFSpriteAtlas* atlas_ = nullptr;
    const SDFSpriteInfo* info_ = nullptr;
    std::string spriteName_;

    // 通常テクスチャ(SDFなし)モード
    bool     rawMode_   = false;
    uint32_t rawSrv_    = 0;
    float    rawAspect_ = 1.0f;

    // 3D ワールド配置モード
    bool    use3D_      = false;
    Vector3 worldPos3D_ = { 0.0f, 0.0f, 0.0f };
    float   worldScale3D_ = 1.0f;
    Camera* camera3D_   = nullptr;

    float posX_ = 0.0f, posY_ = 0.0f;
    float scale_ = 1.0f;
    Vector4 color_        = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vector4 glowColor_    = { 1.0f, 1.0f, 0.0f, 1.0f };
    float edgeBias_     = 0.5f;
    float outlineWidth_ = 0.0f;
    float glowWidth_    = 0.0f;

    float drawW_ = 0.0f, drawH_ = 0.0f;
    uint32_t indexCount_ = 0;
    bool dirty_ = true;
};
