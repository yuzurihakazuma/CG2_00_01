#include "Fullscreen.hlsli"

// 紙の質感：横に伸びた繊維ノイズ＋細かい粒を画面全体へうっすら乗算する。
//   クラフト（工作紙）の世界観の底上げ。強さは grainStrength（0.1前後が上品）
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
    float32_t grainStrength; // 繊維ノイズの強さ
    float32_t posterLevels;
    float32_t posterEdge;
    float32_t2 padding;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// 位置から決まる擬似乱数（毎フレーム同じ＝紙の模様が固定されてチラつかない）
float32_t Hash21(float32_t2 p)
{
    p = frac(p * float32_t2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t4 texColor = gTexture.Sample(gSampler, input.texcoord);

    float32_t width, height;
    gTexture.GetDimensions(width, height);
    float32_t2 pixel = input.texcoord * float32_t2(width, height);

    // 横方向に伸びた繊維（横長ブロックで乱数を引く）＋細かい粒
    float32_t fiber = Hash21(floor(pixel * float32_t2(0.20f, 1.0f))) - 0.5f;
    float32_t fine  = Hash21(floor(pixel)) - 0.5f;
    float32_t grain = fiber * 0.7f + fine * 0.3f;

    output.color.rgb = texColor.rgb * (1.0f + grain * grainStrength);
    output.color.a = texColor.a;
    return output;
}
