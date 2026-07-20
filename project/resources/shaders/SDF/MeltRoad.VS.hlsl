// SDF溶け道（チェーン融合レイマーチング）用 頂点シェーダー
// 単位キューブ(0〜1)の頂点をチャンクのワールドAABBへ展開する。面はPSが作る。

#define MAX_POINTS 16

struct MeltRoadCB
{
    float4x4 viewProj;
    float3 rayBoxMin; float pad0;   // チャンクのプロキシAABB
    float3 rayBoxMax; float pad1;
    float3 cameraPos; float pointCount;
    float4 baseColor;
    float3 lightDir; float noiseFreq;
    float halfWidth; float halfThick; float roundR; float blendK;
    float erodeBand; float chainOffset; float spacing; float pad4;
    float4 pts[MAX_POINTS];         // xyz=チェーン点 / w=その点のエロージョン量
};
ConstantBuffer<MeltRoadCB> gRoad : register(b0);

struct VSInput
{
    float3 position : POSITION;     // 単位キューブ頂点 (0〜1)
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;    // ボックス表面のワールド座標（レイの入口）
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 wp = lerp(gRoad.rayBoxMin, gRoad.rayBoxMax, input.position);
    output.worldPos = wp;
    output.position = mul(float4(wp, 1.0f), gRoad.viewProj);
    return output;
}
