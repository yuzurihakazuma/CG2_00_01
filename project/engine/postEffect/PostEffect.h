#pragma once

#include <memory>
#include <d3d12.h>
#include <string>
#include "engine/graphics/RenderTexture.h"

class DirectXCommon;
class SrvManager;

enum class PostEffectType{
    None,
    Grayscale,
    Sepia,
    Vignetting,
    BoxFilter,
    BoxFilter5x5,
    GaussianFilter,
    RadialBlur,
    Outline,
    RandomNoise,
    ColorTint,        // 全面カラーティント（死亡フラッシュ等）
    MaskedDistortion, // ボス周囲の熱波歪み
    MaskedGlow,       // プレイヤー周囲のスポットライト
    MaskedSepia,      // 特定エリアのセピア
    Count
};

// マスクエフェクト用パラメータ（b1 レジスタ、32バイト）
struct MaskParams{
    float slot0X      = 0.5f;           // ボス スクリーンUV X
    float slot0Y      = 0.5f;           // ボス スクリーンUV Y
    float slot0Radius = 0.18f;          // 歪み半径
    float slot0Pad    = 0.35f;          // セピア半径（歪みより広い）
    float slot1X      = 0.5f;           // プレイヤー スクリーンUV X
    float slot1Y      = 0.5f;           // プレイヤー スクリーンUV Y
    float slot1Radius = 0.15f;          // グロー半径
    float aspectRatio = 16.0f / 9.0f;  // アスペクト比（正円補正）
};

// シェーダーに渡す共有パラメータ（b0 レジスタ、32バイト固定）
struct PostEffectParams{
    float time = 0.0f;   // RandomNoise 用タイム
    float param0 = 1.0f;   // Vignette: 強度 / RadialBlur: 強度倍率
    float colorR = 1.0f;   // Vignette 縁の色 R
    float colorG = 1.0f;   // Vignette 縁の色 G
    float colorB = 1.0f;   // Vignette 縁の色 B
    float pad0 = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
};

class PostEffect{
public:

    static PostEffect* GetInstance(){
        static PostEffect instance;
        return &instance;
    }

    ~PostEffect() = default;

    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);
    void Update();
    void Finalize();

    void PreDrawScene(ID3D12GraphicsCommandList* commandList);
    void PostDrawScene(ID3D12GraphicsCommandList* commandList);
    void Draw(ID3D12GraphicsCommandList* commandList);
    void OnResize(uint32_t width, uint32_t height);
    void PreDrawSceneMRT(ID3D12GraphicsCommandList* commandList);
    void PostDrawSceneMRT(ID3D12GraphicsCommandList* commandList);

    void DrawDebugUI();

    void AddTime(float addValue){
        if ( effectParamsData_ ) {
            effectParamsData_->time += addValue;
        }
    }

    void Save(const std::string& filePath = "resources/data/postEffect.json");
    void Load(const std::string& filePath = "resources/data/postEffect.json");

    uint32_t GetSrvIndex() const;

    void SetEffectActive(PostEffectType type, bool isActive){
        activeEffects_[static_cast< int >( type )] = isActive;
    }

    bool GetEffectActive(PostEffectType type) const{
        return activeEffects_[static_cast< int >( type )];
    }

    void SetMasterActive(bool isActive){
        isActive_ = isActive;
    }

    void ClearAllEffects(){
        for ( int i = 0; i < static_cast< int >(PostEffectType::Count); ++i ) {
            activeEffects_[i] = false;
        }
    }

    uint32_t GetMaskSrvIndex() const{ return maskTexture_->GetSrvIndex(); }

    // ビネットの強度と色を設定（毎フレーム呼ぶ）
    void SetVignetteParams(float intensity, float r, float g, float b){
        if ( !effectParamsData_ ) return;
        effectParamsData_->param0 = intensity;
        effectParamsData_->colorR = r;
        effectParamsData_->colorG = g;
        effectParamsData_->colorB = b;
    }

    // ラジアルブラー強度を設定（1.0=通常、0.5=半分）
    void SetRadialBlurStrength(float strength){
        if ( !effectParamsData_ ) return;
        effectParamsData_->param0 = strength;
    }

    // 全面カラーティントを設定（ボス死亡フラッシュなど）
    void SetColorTint(float alpha, float r, float g, float b){
        SetVignetteParams(alpha, r, g, b); // 同じパラメータ構造を流用
    }

    // ボス位置のマスク円を設定（MaskedDistortion / MaskedSepia 用）
    void SetBossScreenUV(float x, float y, float distortRadius, float sepiaRadius){
        if ( !maskParamsData_ ) return;
        maskParamsData_->slot0X      = x;
        maskParamsData_->slot0Y      = y;
        maskParamsData_->slot0Radius = distortRadius;
        maskParamsData_->slot0Pad    = sepiaRadius;
    }

    // プレイヤー位置のマスク円を設定（MaskedGlow 用）
    void SetPlayerScreenUV(float x, float y, float radius){
        if ( !maskParamsData_ ) return;
        maskParamsData_->slot1X      = x;
        maskParamsData_->slot1Y      = y;
        maskParamsData_->slot1Radius = radius;
    }

    // 着弾点の歪みを一時的にトリガー（durationFrames 後に自動オフ）
    void TriggerDistortion(float x, float y, float radius, int durationFrames){
        if ( !maskParamsData_ ) return;
        maskParamsData_->slot0X      = x;
        maskParamsData_->slot0Y      = y;
        maskParamsData_->slot0Radius = radius;
        SetEffectActive(PostEffectType::MaskedDistortion, true);
        distortionTimer_ = durationFrames;
    }

private:
    PostEffect() = default;
    PostEffect(const PostEffect&) = delete;
    PostEffect& operator=(const PostEffect&) = delete;

    void ApplyEffect(ID3D12GraphicsCommandList* commandList, DirectXCommon* dxCommon,
        PostEffectType type, bool isEffectActive, uint32_t& src, uint32_t& dest);

private:
    std::unique_ptr<RenderTexture> renderTextures_[2];
    uint32_t finalResultIndex_ = 0;
    std::unique_ptr<RenderTexture> maskTexture_;

    bool isActive_ = true;
    bool activeEffects_[static_cast< int >( PostEffectType::Count )] = { false };

    Microsoft::WRL::ComPtr<ID3D12Resource> timeResource_;
    PostEffectParams* effectParamsData_ = nullptr;
    float timeSpeed_ = 0.05f;

    Microsoft::WRL::ComPtr<ID3D12Resource> maskParamsResource_;
    MaskParams* maskParamsData_ = nullptr;

    int distortionTimer_ = 0; // TriggerDistortion の残りフレーム数

    DirectXCommon* dxCommon_ = nullptr;
};