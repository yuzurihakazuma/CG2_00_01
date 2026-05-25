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
    float32_t slot0Pad;
    float32_t slot1X;       // プレイヤー スクリーンUV X
    float32_t slot1Y;       // プレイヤー スクリーンUV Y
    float32_t slot1Radius;  // グロー半径
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

    float32_t2 diff = float32_t2((uv.x - slot1X) * aspectRatio, uv.y - slot1Y);
    float32_t dist  = length(diff);
    // 中心ほど強く、外に向かってなめらかに消える
    float32_t mask  = 1.0f - smoothstep(slot1Radius * 0.35f, slot1Radius, dist);

    float32_t4 texColor = gTexture.Sample(gSampler, uv);

    // プレイヤー周囲をスポットライトのように明るくする
    float32_t3 glow = texColor.rgb * (1.0f + 0.28f * mask);
    glow = min(glow, float32_t3(1.0f, 1.0f, 1.0f));

    output.color = float32_t4(glow, texColor.a);
    return output;
}
