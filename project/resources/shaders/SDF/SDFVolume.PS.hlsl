// SDFボリューム（レイマーチング）用 ピクセルシェーダー
// Texture3D<float> に焼かれた「符号付き距離」をスフィアトレーシングして面を再構成する。
//   ・距離値は「最寄り面までの距離」なので、その距離ぶん一気にレイを進められる＝少ないループで済む
//   ・法線はポリゴンではなく距離場の勾配（中央差分）から求める
//   ・ヒット点の深度を SV_Depth に書くので、通常の3Dモデルと正しく前後関係が出る

struct VolumeCB
{
    float4x4 viewProj;
    float3 boxMin; float pad0;
    float3 boxMax; float pad1;
    float3 cameraPos; float distScale;
    float4 baseColor;
    float3 lightDir; float pad2;
};
ConstantBuffer<VolumeCB> gVolume : register(b0);

Texture3D<float> gSDF : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET0; // シーンの色キャンバス
    float4 mask  : SV_TARGET1; // エフェクトマスク（ブルーム対象外なので0）
    float  depth : SV_Depth;   // ヒット点の正確な深度
};

// ワールド座標 → ボリュームUV(0〜1) → 距離（ワールド単位）
float SampleDist(float3 wp)
{
    float3 uv = (wp - gVolume.boxMin) / (gVolume.boxMax - gVolume.boxMin);
    return gSDF.SampleLevel(gSampler, uv, 0) * gVolume.distScale;
}

PSOutput main(PSInput input)
{
    // レイ：カメラ → このピクセルのボックス表面点
    float3 ro = gVolume.cameraPos;
    float3 rd = normalize(input.worldPos - ro);

    // スラブ法でレイとAABBの交差区間 [t0, t1] を求める
    float3 invD = 1.0f / rd;
    float3 tA = (gVolume.boxMin - ro) * invD;
    float3 tB = (gVolume.boxMax - ro) * invD;
    float3 tMin3 = min(tA, tB);
    float3 tMax3 = max(tA, tB);
    float t0 = max(max(tMin3.x, tMin3.y), tMin3.z);
    float t1 = min(min(tMax3.x, tMax3.y), tMax3.z);
    if (t1 <= max(t0, 0.0f)) { discard; }

    // スフィアトレーシング
    float boxDiag = length(gVolume.boxMax - gVolume.boxMin);
    float eps = boxDiag * 0.0015f; // ヒット判定のしきい値（箱の大きさに比例）
    float t = max(t0, 0.0f) + eps;
    float3 p = ro + rd * t;
    bool hit = false;

    [loop]
    for (int i = 0; i < 128; ++i)
    {
        p = ro + rd * t;
        float d = SampleDist(p);
        if (d < eps) { hit = true; break; }
        t += max(d, eps); // 最低でも eps は進める（無限ループ防止）
        if (t > t1) { break; }
    }
    if (!hit) { discard; }

    // 法線 = 距離場の勾配（中央差分）
    float h = boxDiag * 0.01f;
    float3 n = normalize(float3(
        SampleDist(p + float3(h, 0, 0)) - SampleDist(p - float3(h, 0, 0)),
        SampleDist(p + float3(0, h, 0)) - SampleDist(p - float3(0, h, 0)),
        SampleDist(p + float3(0, 0, h)) - SampleDist(p - float3(0, 0, h))));

    // ハーフランバート＋環境光（クラフト風のやわらかい陰影）
    float3 l = normalize(-gVolume.lightDir);
    float ndl = saturate(dot(n, l) * 0.5f + 0.5f);
    float3 rgb = gVolume.baseColor.rgb * (0.25f + 0.75f * ndl);

    PSOutput output;
    output.color = float4(rgb, gVolume.baseColor.a);
    output.mask  = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // ヒット点のクリップ空間深度（箱の表面ではなく「実際の面」の深度になる）
    float4 clip = mul(float4(p, 1.0f), gVolume.viewProj);
    output.depth = clip.z / clip.w;
    return output;
}
