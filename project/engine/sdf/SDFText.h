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

    // --- 3D配置（看板用。SDFSprite と同じ流儀）---
    //   3Dワールド空間に文字列を板として置く（カメラは既定カメラを自動使用）。
    //   worldScale は「1行の高さ(m)」。文字列全体の中心が worldPos に来るよう自動で中央揃えする
    void SetTransform3D(const Vector3& worldPos, float worldScale);
    void SetScreenMode(); // スクリーン配置（2D）に戻す
    bool Is3D() const { return use3D_; }
    Vector3& RefWorldPos() { return worldPos3D_; }   // ImGui編集用
    float& RefWorldScale() { return worldScale3D_; } // ImGui編集用

    // --- 近接表示（看板用）：プレイヤーが近くにいる時だけ表示する ---
    void SetProximity(bool enabled, float radius) { proximityEnabled_ = enabled; proximityRadius_ = radius; }
    bool  IsProximityEnabled() const { return proximityEnabled_; }
    float GetProximityRadius() const { return proximityRadius_; }
    bool&  RefProximityEnabled() { return proximityEnabled_; } // ImGui編集用
    float& RefProximityRadius() { return proximityRadius_; }   // ImGui編集用
    // 表示アルファ（フェード用。SDFManager が毎フレーム距離から設定する）
    void SetDrawAlpha(float a) { drawAlpha_ = a; }

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

    // --- 3D配置（看板用）---
    bool    use3D_ = false;
    Vector3 worldPos3D_ { 0.0f, 3.0f, 0.0f };
    float   worldScale3D_ = 1.0f; // 1行の高さ (m)

    // --- 近接表示 ---
    bool  proximityEnabled_ = false; // true=プレイヤーが近くにいる時だけ表示
    float proximityRadius_  = 8.0f;  // 表示距離 (m)。端の25%でフェード
    float drawAlpha_ = 1.0f;         // フェード係数（SDFManager が設定）

    // 文字列全体のピクセル空間バウンズ（RebuildGeometry で計測。3Dの中央揃えに使う）
    float boundsMinX_ = 0.0f, boundsMinY_ = 0.0f;
    float boundsMaxX_ = 0.0f, boundsMaxY_ = 0.0f;

    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
    float outlineWidth_ = 0.15f;
    float edgeWidth_ = 0.02f;
    float thickness_ = 0.0f; // 0=標準 / +太く / -細く

    uint32_t indexCount_ = 0;
    bool dirty_ = true;
};
