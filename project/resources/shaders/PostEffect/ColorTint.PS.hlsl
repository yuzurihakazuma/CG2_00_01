#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time; // 未使用
    float32_t alpha; // ティントの強さ（0=なし、1=全面）
    float32_t colorR;
    float32_t colorG;
    float32_t colorB;
    float32_t3 padding;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 texColor = gTexture.Sample(gSampler, input.texcoord);

    // 画面全体をティントカラーにブレンド
    output.color.rgb = lerp(texColor.rgb, float32_t3(colorR, colorG, colorB), alpha);
    output.color.a = texColor.a;

    return output;
}