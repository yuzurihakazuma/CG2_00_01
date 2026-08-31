#include "Fullscreen.hlsli"

// アイリスワイプ：指定点（プレイヤー）中心の円の外側を紙色で覆う。
//   半径を 1.4→0 で閉じ、0→1.4 で開く。マリオ/ヨッシーの画面転換の定番。
//   半径は「画面の高さ=1」基準（横はアスペクト補正して真円にする）
Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostEffectParams : register(b0)
{
    float32_t time;
    float32_t param0;
    float32_t colorR;
    float32_t colorG;
    float32_t colorB;
    float32_t irisRadius; // 円の半径（0=全閉 / 1.4=全開）
    float32_t irisCX;     // 円の中心UV
    float32_t irisCY;
    float32_t tiltStrength;
    float32_t tiltCenterY;
    float32_t tiltHalfWidth;
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
    float32_t4 texColor = gTexture.Sample(gSampler, input.texcoord);

    float32_t width, height;
    gTexture.GetDimensions(width, height);
    float32_t aspect = width / height;

    float32_t2 diff = input.texcoord - float32_t2(irisCX, irisCY);
    diff.x *= aspect; // 真円にする
    float32_t dist = length(diff);

    // 円の内側=1 / 外側=0（縁は少しだけ柔らかく）
    float32_t inside = 1.0f - smoothstep(irisRadius - 0.02f, irisRadius + 0.02f, dist);

    // 外側はクラフト紙の焦げ茶（目に刺さらない暗色）
    const float32_t3 kCraftDark = float32_t3(0.12f, 0.085f, 0.055f);
    output.color.rgb = lerp(kCraftDark, texColor.rgb, inside);
    output.color.a = 1.0f;
    return output;
}
