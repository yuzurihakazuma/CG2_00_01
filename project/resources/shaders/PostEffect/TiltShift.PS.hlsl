#include "Fullscreen.hlsli"

// ティルトシフト（ジオラマ風）：
//   画面のY方向で「ピントの帯」から離れるほどぼかす。カメラはプレイヤーの高さを
//   画面中央付近に映すので、実質「プレイヤー周辺はシャープ・遠景と手前はぼける」
//   ＝工作のミニチュアを覗いているような見た目になる
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time;
    float32_t param0;
    float32_t colorR;
    float32_t colorG;
    float32_t colorB;
    float32_t irisRadius;
    float32_t irisCX;
    float32_t irisCY;
    float32_t tiltStrength;   // ぼかし強さ(px)
    float32_t tiltCenterY;    // ピント帯の中心（UV）
    float32_t tiltHalfWidth;  // 帯の半幅（この範囲はシャープ）
    float32_t grainStrength;
    float32_t posterLevels;
    float32_t posterEdge;
    float32_t2 padding;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float32_t width, height;
    gTexture.GetDimensions(width, height);
    float32_t2 texel = float32_t2(1.0f / width, 1.0f / height);

    // ピント帯からの距離 → ぼかし量（0〜1）。帯の外へ出てからなだらかに強くなる
    float32_t dist = abs(input.texcoord.y - tiltCenterY);
    float32_t blurT = saturate((dist - tiltHalfWidth) / 0.25f);
    blurT = blurT * blurT; // 帯の際は極小、外側で効く

    float32_t radius = tiltStrength * blurT;

    // 9タップの円形サンプリング（軽量なディスクぼかし）
    float32_t4 sum = gTexture.Sample(gSampler, input.texcoord) * 2.0f;
    const float32_t2 kOffsets[8] = {
        float32_t2( 1.0f,  0.0f), float32_t2(-1.0f,  0.0f),
        float32_t2( 0.0f,  1.0f), float32_t2( 0.0f, -1.0f),
        float32_t2( 0.7f,  0.7f), float32_t2(-0.7f,  0.7f),
        float32_t2( 0.7f, -0.7f), float32_t2(-0.7f, -0.7f)
    };
    for (int i = 0; i < 8; ++i)
    {
        sum += gTexture.Sample(gSampler, input.texcoord + kOffsets[i] * texel * radius);
    }
    output.color = sum / 10.0f;
    output.color.a = 1.0f;
    return output;
}
