// SDFボリューム（レイマーチング）用 頂点シェーダー
// 単位キューブ(0〜1)の頂点をワールド空間のレイ用AABB（モーフ時はA∪B）へ展開し、
// カメラのVPでクリップへ変換する。実際の面はピクセルシェーダーが作る。

struct VolumeCB
{
    float4x4 viewProj;
    float3 rayBoxMin; float pad0;  // レイ/プロキシ箱（モーフ時は A∪B）
    float3 rayBoxMax; float pad1;
    float3 boxMinA; float pad2;    // ボリュームAのワールドAABB
    float3 boxMaxA; float pad3;
    float3 boxMinB; float pad4;    // ボリュームB（モーフ先）のワールドAABB
    float3 boxMaxB; float pad5;
    float3 cameraPos; float distScale;
    float4 baseColor;
    float3 lightDir; float erode;
    float useColorTexA; float useColorTexB; float useMorph; float morphT;
};
ConstantBuffer<VolumeCB> gVolume : register(b0);

struct VSInput
{
    float3 position : POSITION; // 単位キューブ頂点 (0〜1)
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0; // ボックス表面のワールド座標（レイの入口）
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float3 wp = lerp(gVolume.rayBoxMin, gVolume.rayBoxMax, input.position);
    output.worldPos = wp;
    output.position = mul(float4(wp, 1.0f), gVolume.viewProj);
    return output;
}
