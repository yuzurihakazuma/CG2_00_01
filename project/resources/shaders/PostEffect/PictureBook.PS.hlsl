#include "Fullscreen.hlsli"

// 絵本風：色数を絞り（ポスタライズ）、輝度差の大きい所に輪郭線を引く。
//   全体の質感が「切り絵・絵本の挿絵」のようにそろう
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
    float32_t tiltStrength;
    float32_t tiltCenterY;
    float32_t tiltHalfWidth;
    float32_t grainStrength;
    float32_t posterLevels; // 色の段階数（6前後）
    float32_t posterEdge;   // 輪郭線の濃さ（0〜1）
    float32_t2 padding;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

float32_t Luminance(float32_t3 rgb)
{
    return dot(rgb, float32_t3(0.2125f, 0.7154f, 0.0721f));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 texColor = gTexture.Sample(gSampler, input.texcoord);

    // 1) ポスタライズ：色を段階に量子化（少しソフトに丸める）
    float32_t levels = max(posterLevels, 2.0f);
    float32_t3 poster = floor(texColor.rgb * levels + 0.5f) / levels;
    float32_t3 baseColor = lerp(texColor.rgb, poster, 0.85f);

    // 2) 輪郭線：輝度の横縦差分（簡易ソーベル）でエッジを検出して暗くする
    float32_t width, height;
    gTexture.GetDimensions(width, height);
    float32_t2 texel = float32_t2(1.0f / width, 1.0f / height);
    float32_t lumL = Luminance(gTexture.Sample(gSampler, input.texcoord + float32_t2(-texel.x, 0)).rgb);
    float32_t lumR = Luminance(gTexture.Sample(gSampler, input.texcoord + float32_t2( texel.x, 0)).rgb);
    float32_t lumU = Luminance(gTexture.Sample(gSampler, input.texcoord + float32_t2(0, -texel.y)).rgb);
    float32_t lumD = Luminance(gTexture.Sample(gSampler, input.texcoord + float32_t2(0,  texel.y)).rgb);
    float32_t edge = saturate((abs(lumR - lumL) + abs(lumD - lumU)) * 4.0f) * posterEdge;

    output.color.rgb = baseColor * (1.0f - edge * 0.75f);
    output.color.a = texColor.a;
    return output;
}
