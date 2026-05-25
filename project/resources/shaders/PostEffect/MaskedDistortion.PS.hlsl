#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time;     // アニメーション時間（歪みの波に使用）
    float32_t param0;
    float32_t colorR;
    float32_t colorG;
    float32_t colorB;
    float32_t3 padding;
};

cbuffer MaskParams : register(b1)
{
    float32_t slot0X;       // ボス スクリーンUV X
    float32_t slot0Y;       // ボス スクリーンUV Y
    float32_t slot0Radius;  // 歪み半径
    float32_t slot0Pad;     // セピア用半径（このシェーダーでは未使用）
    float32_t slot1X;
    float32_t slot1Y;
    float32_t slot1Radius;
    float32_t aspectRatio;  // アスペクト比（正円補正）
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t2 uv = input.texcoord;

    // アスペクト比補正で楕円でなく正円になるようにする
    float32_t2 diff = float32_t2((uv.x - slot0X) * aspectRatio, uv.y - slot0Y);
    float32_t dist  = length(diff);
    float32_t mask  = 1.0f - smoothstep(slot0Radius * 0.5f, slot0Radius, dist);

    if (mask > 0.001f)
    {
        // 熱波のような波状UV歪み
        float32_t waveX = sin(uv.y * 14.0f + time * 3.5f) * 0.005f;
        float32_t waveY = cos(uv.x * 14.0f + time * 2.8f) * 0.005f;
        float32_t2 distortedUV = uv + float32_t2(waveX, waveY) * mask;

        float32_t4 distorted = gTexture.Sample(gSampler, distortedUV);
        float32_t4 original  = gTexture.Sample(gSampler, uv);
        output.color = lerp(original, distorted, mask);
    }
    else
    {
        output.color = gTexture.Sample(gSampler, uv);
    }

    return output;
}
