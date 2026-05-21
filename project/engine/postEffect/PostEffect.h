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
    Count
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

    DirectXCommon* dxCommon_ = nullptr;
};