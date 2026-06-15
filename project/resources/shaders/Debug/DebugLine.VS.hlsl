// デバッグ線描画用 頂点シェーダー
struct CameraData
{
    float4x4 viewProjection;
};
ConstantBuffer<CameraData> gCamera : register(b0);

struct VSInput
{
    float3 position : POSITION0;
    float4 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    // エンジンの行ベクトル規約（v * VP）に合わせる
    output.position = mul(float4(input.position, 1.0f), gCamera.viewProjection);
    output.color = input.color;
    return output;
}
