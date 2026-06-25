// SDF フォント用ピクセルシェーダー
// SDF テクスチャの距離値(R)を smoothstep で文字形状に変換する。
// fwidth で画面ピクセルの変化量を測り、拡大率に応じて自動でアンチエイリアス幅を決めるため
// どんなサイズでもガビガビにならず常にシャープに出る。

struct SDFParams
{
    float4 textColor;       // 文字の色 (RGBA)
    float4 outlineColor;    // アウトラインの色 (RGBA)
    float edgeWidth;        // アンチエイリアス幅の下限 (0.0〜0.1)
    float outlineWidth;     // アウトラインの太さ (0 = なし, 0.1 〜 0.3)
    float padding0;
    float padding1;
};

ConstantBuffer<SDFParams> gParams : register(b1);

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

    // SDF アトラスから距離値を取得 (R チャンネルに格納されている前提)
    float dist = gAtlas.Sample(gSampler, input.texcoord).r;

    // 画面ピクセルの変化量から最適なアンチエイリアス幅を求める。
    // edgeWidth を下限とし、拡大率に応じて自動調整する。
    float delta = max(fwidth(dist), gParams.edgeWidth);

    // 文字本体の alpha: 距離 0.5 を境にフェードイン
    float alpha = smoothstep(0.5 - delta, 0.5 + delta, dist);

    // アウトラインの alpha: 距離 (0.5 - outlineWidth) を境にフェードイン
    float outlineInner = 0.5 - gParams.outlineWidth;
    float outlineAlpha = smoothstep(outlineInner - delta, outlineInner + delta, dist);

    // アウトラインと文字を合成 (文字が優先)
    float3 blendedColor = lerp(gParams.outlineColor.rgb, gParams.textColor.rgb, alpha);
    float blendedAlpha = max(alpha * gParams.textColor.a, outlineAlpha * gParams.outlineColor.a);

    if (blendedAlpha < 0.005f)
        discard;

    output.color = float4(blendedColor, blendedAlpha);
    return output;
}
