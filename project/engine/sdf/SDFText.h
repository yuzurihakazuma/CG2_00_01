#pragma once
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>
#include "engine/sdf/SDFAtlas.h"
#include "engine/math/struct.h"   // Vector4

// =====================================================================
//  SDFText
//   SDF フォントアトラスで文字列を描画するアイテム。
//   パイプライン/ルートシグネチャは SDFManager が共有で持ち、
//   このクラスは頂点バッファと描画パラメータだけを持つ。
//   使い方（SDFManager 経由が基本。直接使う場合）:
//     text.Initialize();
//     text.SetText(u8"こんにちは");
//     text.Update(atlas);          // 毎フレーム（変更があれば頂点再構築）
//     text.Draw(commandList, atlas); // パイプラインは事前にバインドしておく
// =====================================================================
class SDFText {
public:
    void Initialize();

    void SetText(const std::string& utf8Text);
    void SetPosition(float x, float y);
    void SetFontSize(float size);
    void SetColor(const Vector4& color) { color_ = color; }
    void SetOutlineWidth(float width) { outlineWidth_ = width; }
    void SetOutlineColor(const Vector4& color) { outlineColor_ = color; }
    void MarkDirty() { dirty_ = true; } // アトラスがリロードされた時などに呼ぶ

    const std::string& GetText() const { return text_; }
    float GetX() const { return posX_; }
    float GetY() const { return posY_; }
    float GetFontSize() const { return fontSize_; }
    Vector4& RefColor() { return color_; }               // ImGui編集用
    Vector4& RefOutlineColor() { return outlineColor_; } // ImGui編集用
    float& RefOutlineWidth() { return outlineWidth_; }   // ImGui編集用
    float& RefThickness() { return thickness_; }         // 太さ調整（+太く/-細く）
    void SetThickness(float t) { thickness_ = t; }

    // 毎フレーム呼ぶ（必要なら頂点バッファ再構築＋CB更新）
    void Update(const SDFAtlas& atlas);
    // 描画（SDFManager がパイプラインをバインドした後に呼ぶ）
    void Draw(ID3D12GraphicsCommandList* commandList, const SDFAtlas& atlas);

private:
    static std::vector<uint32_t> DecodeUTF8(const std::string& str);
    void RebuildGeometry(const SDFAtlas& atlas);
    void UpdateBuffers();

    struct Vertex { float x, y, u, v; };

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
    D3D12_VERTEX_BUFFER_VIEW vbv_ {};
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
    D3D12_INDEX_BUFFER_VIEW ibv_ {};

    struct TransformCB { float mat[16]; };
    Microsoft::WRL::ComPtr<ID3D12Resource> transformBuffer_;
    TransformCB* transformData_ = nullptr;

    struct ParamsCB {
        float textColor[4];
        float outlineColor[4];
        float edgeWidth;
        float outlineWidth;
        float boldOffset;   // 太さ調整（シェーダーのしきい値をずらす）
        float pad1;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> paramsBuffer_;
    ParamsCB* paramsData_ = nullptr;

    std::string text_ = "SDF Text";
    float posX_ = 100.0f, posY_ = 100.0f;
    float fontSize_ = 48.0f;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    float outlineWidth_ = 0.15f;
    float edgeWidth_ = 0.02f;
    float thickness_ = 0.0f; // 0=標準 / +太く / -細く

    uint32_t indexCount_ = 0;
    bool dirty_ = true;
};
