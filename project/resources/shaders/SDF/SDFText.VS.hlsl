// SDF 描画共通の頂点シェーダー
// スクリーン座標の頂点を正射影行列でクリップ座標へ変換するだけ

struct Transform
{
    float4x4 WVP;
};
ConstantBuffer<Transform> gTransform : register(b0);

struct VSInput
{
    float2 position : POSITION;
    float2 texcoord : TEXCOORD;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 0.0f, 1.0f), gTransform.WVP);
    output.texcoord = input.texcoord;
    return output;
}
