#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time; // 未使用
    float32_t strength; // ブラー強度倍率（1.0=通常）
    float32_t colorR; // 未使用
    float32_t colorG; // 未使用
    float32_t colorB; // 未使用
    float32_t3 padding;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float32_t2 center = float32_t2(0.5f, 0.5f);
    float32_t2 direction = input.texcoord - center;

    int32_t numSamples = 10;
    float32_t blurWidth = 0.028f * strength; // 0.05f → 0.028f ベースで弱く

    float32_t4 color = float32_t4(0.0f, 0.0f, 0.0f, 0.0f);

    for (int32_t i = 0; i < numSamples; ++i)
    {
        float32_t scale = 1.0f - blurWidth * ((float32_t) i / (float32_t) (numSamples - 1));
        float32_t2 sampleCoord = center + direction * scale;
        color += gTexture.Sample(gSampler, sampleCoord);
    }

    output.color = color / (float32_t) numSamples;
    return output;
}