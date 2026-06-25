// SDF スプライト用ピクセルシェーダー
// カラー保持スプライト (RGB=元の色, A=SDF距離) と
// 単色スプライト (R=SDF距離) の両方に対応する。
// アウトライン(フチ) とグロー(光彩) をシェーダーだけで付けられる。

struct SDFSpriteParams
{
    float4 baseColor;     // useTexColor=0 のときの塗り色 / 兼ティント色
    float4 outlineColor;  // アウトラインの色
    float4 glowColor;     // グロー(光彩)の色。a でグローの強さ
    float edgeBias;       // SDF 境界値 (通常 0.5)
    float outlineWidth;   // アウトラインの太さ (0=なし, 0.05〜0.3)
    float glowWidth;      // グローの広がり (0=なし, 0.1〜0.4)
    float sdfInAlpha;     // 1.0=A から距離取得(keepColor) / 0.0=R から取得
    float useTexColor;    // 1.0=テクスチャRGBを表示(keepColor) / 0.0=baseColor
    float rawSample;      // 1.0=SDF処理せず通常テクスチャをそのまま表示(比較用)
    float pad1;
    float pad2;
};

ConstantBuffer<SDFSpriteParams> gParams : register(b1);

Texture2D<float4> gAtlas : register(t0);
SamplerState gSampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

struct PSOutput
{
    float4 color : SV_TARGET;
};

PSOutput main(VSOutput input)
{
    PSOutput output;

    float4 tex = gAtlas.Sample(gSampler, input.texcoord);

    // 比較用: SDF処理をせず、普通のテクスチャをそのまま描く。
    // 拡大すると元画像のままぼやける／ガビガビになる（SDF版との違いが分かる）。
    if (gParams.rawSample > 0.5f)
    {
        if (tex.a < 0.003f) discard;
        output.color = tex * gParams.baseColor;
        return output;
    }

    // 距離値: keepColor なら A、そうでなければ R
    float dist = lerp(tex.r, tex.a, gParams.sdfInAlpha);

    // 表示する基本色: keepColor ならテクスチャの RGB、そうでなければ baseColor
    float3 base = lerp(gParams.baseColor.rgb, tex.rgb, gParams.useTexColor);

    // 画面ピクセル変化量から自動アンチエイリアス幅を算出
    float delta = fwidth(dist);
    if (delta <= 0.0f) delta = 0.01f;

    float edge = gParams.edgeBias;

    // 本体マスク
    float bodyAlpha = smoothstep(edge - delta, edge + delta, dist);

    // アウトライン (境界より外側に outlineWidth 分広げる)
    float outlineEdge = edge - gParams.outlineWidth;
    float outlineAlpha = smoothstep(outlineEdge - delta, outlineEdge + delta, dist);

    // グロー (さらに外側へ滑らかに減衰)
    float glowEdge = edge - gParams.outlineWidth - gParams.glowWidth;
    float glowAlpha = smoothstep(glowEdge, edge, dist);

    // 色合成: 本体 ← アウトライン ← グロー の順に背後へ重ねる
    float3 color = lerp(gParams.outlineColor.rgb, base, bodyAlpha);
    color = lerp(gParams.glowColor.rgb, color, outlineAlpha);

    // 不透明度: 本体/フチ or グロー の大きい方
    float finalAlpha = max(outlineAlpha, glowAlpha * gParams.glowColor.a);
    finalAlpha *= gParams.baseColor.a;

    if (finalAlpha < 0.003f)
        discard;

    output.color = float4(color, finalAlpha);
    return output;
}
