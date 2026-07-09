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
    float3 lightDir; float erode; // エロージョン量(m)。距離場に足すと表面が法線方向に痩せる
    float useColorTex; float3 pad3; // 1=カラーボリューム(t1)で着色 / 0=baseColor
};
ConstantBuffer<VolumeCB> gVolume : register(b0);

Texture3D<float>  gSDF      : register(t0); // 距離ボリューム
Texture3D<float4> gColorVol : register(t1); // カラーボリューム（最寄り表面の色を焼いたもの）
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
        // エロージョン：距離に定数を足す＝「erode(m) ぶん痩せた形」の正しい距離場になる。
        //   時間で増やすと氷が溶けるように細い所から消え、減らすと芯から生えてくる
        float d = SampleDist(p) + gVolume.erode;
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

    // 色：カラーボリュームがあればヒット点の焼き込み色、無ければ単色
    float3 uvHit = (p - gVolume.boxMin) / (gVolume.boxMax - gVolume.boxMin);
    float3 albedo = lerp(gVolume.baseColor.rgb,
                         gColorVol.SampleLevel(gSampler, uvHit, 0).rgb,
                         gVolume.useColorTex);

    // ハーフランバート＋環境光（クラフト風のやわらかい陰影）
    float3 l = normalize(-gVolume.lightDir);
    float ndl = saturate(dot(n, l) * 0.5f + 0.5f);
    float3 rgb = albedo * (0.25f + 0.75f * ndl);

    PSOutput output;
    output.color = float4(rgb, gVolume.baseColor.a);
    output.mask  = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // ヒット点のクリップ空間深度（箱の表面ではなく「実際の面」の深度になる）
    float4 clip = mul(float4(p, 1.0f), gVolume.viewProj);
    output.depth = clip.z / clip.w;
    return output;
}
