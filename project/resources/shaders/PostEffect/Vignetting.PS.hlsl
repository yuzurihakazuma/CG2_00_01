#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time; // RandomNoise 用（ここでは使わない）
    float32_t intensity; // ビネットの強さ（0=なし、1=最大）
    float32_t colorR; // 縁の色 R（1.0=赤）
    float32_t colorG; // 縁の色 G
    float32_t colorB; // 縁の色 B
    float32_t3 padding; // 16バイトアライン
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float32_t4 texColor = gTexture.Sample(gSampler, input.texcoord);

    float32_t2 center = float32_t2(0.5f, 0.5f);
    float32_t dist = distance(input.texcoord, center);

    // 縁ほど強くなるグラデーション（0=中心、1=端）
    float32_t vignetteFactor = smoothstep(0.25f, 0.65f, dist) * intensity;

    // 縁に向かって暗い色調にブレンドする
    float32_t3 tintedDark = float32_t3(colorR, colorG, colorB) * 0.20f;
    output.color.rgb = lerp(texColor.rgb, tintedDark, vignetteFactor);
    output.color.a = texColor.a;

    return output;
}