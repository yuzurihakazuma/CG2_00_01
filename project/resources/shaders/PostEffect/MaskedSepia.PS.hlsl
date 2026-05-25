#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time;
    float32_t param0;
    float32_t colorR;
    float32_t colorG;
    float32_t colorB;
    float32_t3 padding;
};

cbuffer MaskParams : register(b1)
{
    float32_t slot0X;
    float32_t slot0Y;
    float32_t slot0Radius;
    float32_t slot0Pad;    // セピアの半径（distortionより広く設定）
    float32_t slot1X;
    float32_t slot1Y;
    float32_t slot1Radius;
    float32_t aspectRatio;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t2 uv = input.texcoord;
    float32_t4 texColor = gTexture.Sample(gSampler, uv);

    // slot0Pad を半径として使用（distortionより広い範囲）
    float32_t2 diff = float32_t2((uv.x - slot0X) * aspectRatio, uv.y - slot0Y);
    float32_t dist  = length(diff);
    float32_t mask  = (1.0f - smoothstep(slot0Pad * 0.55f, slot0Pad, dist)) * 0.5f;

    float32_t grey  = dot(texColor.rgb, float32_t3(0.2125f, 0.7154f, 0.0721f));
    float32_t3 sepia = grey * float32_t3(1.0f, 74.0f / 107.0f, 43.0f / 107.0f);

    output.color.rgb = lerp(texColor.rgb, sepia, mask);
    output.color.a   = texColor.a;
    return output;
}
