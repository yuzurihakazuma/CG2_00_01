// SDFボリューム（レイマーチング）用 頂点シェーダー
// 単位キューブ(0〜1)の頂点をワールド空間のAABBへ展開し、カメラのVPでクリップへ変換する。
// 実際の面はピクセルシェーダーのレイマーチングが作るので、ここはただの「箱の器」。

struct VolumeCB
{
    float4x4 viewProj;   // ビュー射影行列
    float3 boxMin; float pad0; // ワールド空間AABB最小
    float3 boxMax; float pad1; // ワールド空間AABB最大
    float3 cameraPos; float distScale; // レイ原点 / 距離値のワールド換算係数
    float4 baseColor;
    float3 lightDir; float erode;      // erode: エロージョン量(m)。+で痩せる/-で太る
    float useColorTex; float3 pad3;    // 1=カラーボリュームで着色 / 0=baseColor
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
    float3 wp = lerp(gVolume.boxMin, gVolume.boxMax, input.position);
    output.worldPos = wp;
    output.position = mul(float4(wp, 1.0f), gVolume.viewProj);
    return output;
}
