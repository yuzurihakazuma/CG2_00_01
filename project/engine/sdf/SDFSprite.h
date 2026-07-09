#pragma once
#include <string>
#include <d3d12.h>
#include <wrl.h>
#include "engine/sdf/SDFAtlas.h"
#include "engine/math/struct.h"   // Vector4

// =====================================================================
//  SDFSprite
//   SDF スプライトアトラスの1画像を描画するアイテム。
//   カラー保持アトラスなら元の色のまま、フチ・グローをパラメータだけで付けられる。
//   パイプラインは SDFManager が共有で持つ。
// =====================================================================
class SDFSprite {
public:
    void Initialize();

    void SetSpriteName(const std::string& name);
    void SetPosition(float x, float y);
    void SetScale(float scale);

    // 3Dワールド空間に板として配置する（カメラは既定カメラを自動使用）
    // worldScale は板の高さ（幅はアスペクト比から自動）
    void SetTransform3D(const Vector3& worldPos, float worldScale);
    // スクリーン配置（2D）に戻す
    void SetScreenMode();
    bool Is3D() const { return use3D_; }
    Vector3& RefWorldPos() { return worldPos3D_; }   // ImGui編集用
    float& RefWorldScale() { return worldScale3D_; } // ImGui編集用
    void SetColor(const Vector4& color) { color_ = color; }
    void SetOutline(float width, const Vector4& color) { outlineWidth_ = width; outlineColor_ = color; }
    void SetGlow(float width, const Vector4& color) { glowWidth_ = width; glowColor_ = color; }
    void MarkDirty() { dirty_ = true; }

    const std::string& GetSpriteName() const { return spriteName_; }
    float GetX() const { return posX_; }
    float GetY() const { return posY_; }
    float& RefScale() { return scale_; }                 // ImGui編集用
    Vector4& RefColor() { return color_; }
    float& RefOutlineWidth() { return outlineWidth_; }
    Vector4& RefOutlineColor() { return outlineColor_; }
    float& RefGlowWidth() { return glowWidth_; }
    Vector4& RefGlowColor() { return glowColor_; }
    float& RefEdgeBias() { return edgeBias_; } // 0.5標準。小さくすると太る/大きくすると痩せる

    // --- 近接表示（看板用）：プレイヤーが近くにいる時だけ表示する ---
    void SetProximity(bool enabled, float radius) { proximityEnabled_ = enabled; proximityRadius_ = radius; }
    bool  IsProximityEnabled() const { return proximityEnabled_; }
    float GetProximityRadius() const { return proximityRadius_; }
    bool&  RefProximityEnabled() { return proximityEnabled_; } // ImGui編集用
    float& RefProximityRadius() { return proximityRadius_; }   // ImGui編集用
    // 表示アルファ（フェード用。SDFManager が毎フレーム距離から設定する）
    void SetDrawAlpha(float a) { drawAlpha_ = a; }

    void Update(const SDFAtlas& atlas);
    void Draw(ID3D12GraphicsCommandList* commandList, const SDFAtlas& atlas);

private:
    void RebuildGeometry(const SDFAtlas& atlas);
    void UpdateBuffers(const SDFAtlas& atlas);

    struct Vertex { float x, y, u, v; };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ {};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_ {};

    struct TransformCB { float mat[16]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;
    TransformCB* transformData_ = nullptr;

    struct ParamsCB {
        float baseColor[4];
        float outlineColor[4];
        float glowColor[4];
        float edgeBias;
        float outlineWidth;
        float glowWidth;
        float sdfInAlpha;
        float useTexColor;
        float pad0, pad1, pad2;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsBuffer_;
    ParamsCB* paramsData_ = nullptr;

    std::string spriteName_;
    float posX_ = 200.0f, posY_ = 200.0f;
    float scale_ = 1.0f;

    // 3D配置モード
    bool    use3D_ = false;
    Vector3 worldPos3D_ = { 0.0f, 3.0f, 5.0f };
    float   worldScale3D_ = 5.0f;
    bool    prev3D_ = false; // モード切替検知（ジオメトリ再構築用）

    // --- 近接表示 ---
    bool  proximityEnabled_ = false; // true=プレイヤーが近くにいる時だけ表示
    float proximityRadius_  = 8.0f;  // 表示距離 (m)。端の25%でフェード
    float drawAlpha_ = 1.0f;         // フェード係数（SDFManager が設定）
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    Vector4 glowColor_ = { 1.0f, 0.9f, 0.3f, 1.0f };
    float outlineWidth_ = 0.0f;
    float glowWidth_ = 0.0f;
    float edgeBias_ = 0.5f;

    uint32_t indexCount_ = 0;
    bool dirty_ = true;
};
