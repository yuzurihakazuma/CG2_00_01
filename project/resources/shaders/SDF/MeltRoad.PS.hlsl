// SDF溶け道（チェーン融合レイマーチング）用 ピクセルシェーダー
// レール沿いのチェーン点列を「角丸断面の線分SDF」として smooth min で融合し、
// 1回のレイマーチで継ぎ目のない1本の道として描く（円盤の重なり縁が出ない）。
//   ・エロージョンは点ごと（プレイヤー距離）を線分上で補間 ＝ 溶け前線が道に沿って動く
//   ・ワールド座標ノイズで溶けムラ（穴あき・焼け崩れ）
//   ・溶け際は焦げ→赤熱（段ボールを熱した風）
//   ・SV_Depth にヒット点の深度を書くので通常メッシュと正しく前後する

#define MAX_POINTS 16

struct MeltRoadCB
{
    float4x4 viewProj;
    float3 rayBoxMin; float pad0;
    float3 rayBoxMax; float pad1;
    float3 cameraPos; float pointCount;
    float4 baseColor;
    float3 lightDir; float noiseFreq;
    float halfWidth; float halfThick; float roundR; float blendK;
    float erodeBand; float chainOffset; float spacing; float pad4;
    float4 pts[MAX_POINTS];   // xyz=チェーン点 / w=その点のエロージョン量
};
ConstantBuffer<MeltRoadCB> gRoad : register(b0);

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
    float4 mask  : SV_TARGET1;
    float  depth : SV_Depth;
};

// --- 3D値ノイズ（溶けムラ用。SDFVolume.PS と同じもの）---
float Hash3(float3 p)
{
    p = frac(p * 0.3183099f + float3(0.1f, 0.2f, 0.3f));
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float ValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float n000 = Hash3(i + float3(0, 0, 0)), n100 = Hash3(i + float3(1, 0, 0));
    float n010 = Hash3(i + float3(0, 1, 0)), n110 = Hash3(i + float3(1, 1, 0));
    float n001 = Hash3(i + float3(0, 0, 1)), n101 = Hash3(i + float3(1, 0, 1));
    float n011 = Hash3(i + float3(0, 1, 1)), n111 = Hash3(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
                lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}

// 多項式 smooth min：aとbを幅kでなめらかに融合（SDF合成の定番）
float SMin(float a, float b, float k)
{
    float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}

// --- 段ボールの中芯（波板）パラメータ ---
static const float kCorrugPitch = 0.16f;  // 波の周期(m)
static const float kCorrugDepth = 0.035f; // 波の彫り込み深さ(m)
static const float kCoreHalf    = 0.08f;  // 中芯帯の半分の厚み(m)。この帯にだけ波を彫る

// 合成距離場：全線分の「角丸断面SDF＋点補間エロージョン」を smooth min で融合。
//   厚みの中央帯には道に沿った周期の波（中芯）を彫り込む → どこを切っても・
//   どこが溶けても、断面に段ボールの波板が自動で現れる。
// outErode=焦げ判定用の実効エロージョン / outLocalY=道の中心からの高さ / outWave=波の山谷(0〜1)
float MapDist(float3 wp, out float outErode, out float outLocalY, out float outWave)
{
    float noiseMul = lerp(0.45f, 1.65f, ValueNoise(wp * gRoad.noiseFreq)); // 溶けムラ
    float d = 1e9f;
    float e = 0.0f;
    float localY = 0.0f;
    float waveOut = 0.0f;
    float cutY = -1e9f; // 最寄り線分での「レール線からの高さ」。上面の平面カットに使う
    int count = (int)gRoad.pointCount;

    [loop]
    for (int i = 0; i + 1 < count; ++i)
    {
        float3 a = gRoad.pts[i].xyz;
        float3 b = gRoad.pts[i + 1].xyz;
        float3 pa = wp - a, ba = b - a;
        float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6f));
        float3 q = pa - ba * h;   // 線分からのオフセット（端では丸い道端になる）

        // チェーン点はレール線上にあり、断面の中心はその halfThick 下。
        //   → 上面（中心+halfThick）が常にレール線ぴったりになる（厚みを変えても不変）
        float yc = q.y + gRoad.halfThick;

        // 断面：横=halfWidth / 縦=halfThick の角丸ボックス
        float2 c = float2(length(q.xz) - gRoad.halfWidth + gRoad.roundR,
                          abs(yc) - gRoad.halfThick + gRoad.roundR);
        float di = min(max(c.x, c.y), 0.0f) + length(max(c, 0.0f)) - gRoad.roundR;

        // 中芯の波：道に沿った距離 s で位相を取る（チャンクをまたいでも連続）
        float s = (gRoad.chainOffset + (float)i + h) * gRoad.spacing;
        float wave = 0.5f + 0.5f * sin(s * (6.2831853f / kCorrugPitch));
        float core = 1.0f - smoothstep(kCoreHalf, kCoreHalf + 0.05f, abs(yc));
        di += core * wave * kCorrugDepth; // 波の山の位置だけ表面を彫る

        // 点ごとのエロージョンを線分上で補間して足す（溶け前線が道に沿って動く）
        float ei = lerp(gRoad.pts[i].w, gRoad.pts[i + 1].w, h) * noiseMul;
        di += ei;

        if (di < d) { e = ei; localY = yc; waveOut = wave; cutY = q.y; } // 最寄り線分の情報を色付け＆カットに使う
        d = SMin(d, di, gRoad.blendK);
    }
    // 上面をレール線で平らにカット：smooth min の膨らみが歩行面より上へ盛り上がらない
    // （ブロック・敵・プレイヤーは全てレール線基準で接地するので、道の上面もそこに固定する）
    d = max(d, cutY);
    outErode = e;
    outLocalY = localY;
    outWave = waveOut;
    return d;
}
float MapDistOnly(float3 wp) { float e, ly, w; return MapDist(wp, e, ly, w); }

PSOutput main(PSInput input)
{
    float3 ro = gRoad.cameraPos;
    float3 rd = normalize(input.worldPos - ro);

    // スラブ法でレイとチャンクAABBの交差区間 [t0, t1]
    float3 invD = 1.0f / rd;
    float3 tA = (gRoad.rayBoxMin - ro) * invD;
    float3 tB = (gRoad.rayBoxMax - ro) * invD;
    float3 tMin3 = min(tA, tB);
    float3 tMax3 = max(tA, tB);
    float t0 = max(max(tMin3.x, tMin3.y), tMin3.z);
    float t1 = min(min(tMax3.x, tMax3.y), tMax3.z);
    if (t1 <= max(t0, 0.0f)) { discard; }

    // スフィアトレーシング
    float boxDiag = length(gRoad.rayBoxMax - gRoad.rayBoxMin);
    float eps = boxDiag * 0.0012f;
    float t = max(t0, 0.0f) + eps;
    float3 p = ro + rd * t;
    float hitErode = 0.0f;
    float hitLocalY = 0.0f;
    float hitWave = 0.0f;
    bool hit = false;

    [loop]
    for (int i = 0; i < 112; ++i)
    {
        p = ro + rd * t;
        float d = MapDist(p, hitErode, hitLocalY, hitWave);
        if (d < eps) { hit = true; break; }
        t += max(d * 0.7f, eps); // 波板の彫り込みで距離場が急勾配になるため歩幅を抑える
        if (t > t1) { break; }
    }
    if (!hit) { discard; }

    // 法線 = 融合距離場の勾配（中央差分）。融合部でもなめらかな向きが出る
    float h = 0.02f;
    float3 n = normalize(float3(
        MapDistOnly(p + float3(h, 0, 0)) - MapDistOnly(p - float3(h, 0, 0)),
        MapDistOnly(p + float3(0, h, 0)) - MapDistOnly(p - float3(0, h, 0)),
        MapDistOnly(p + float3(0, 0, h)) - MapDistOnly(p - float3(0, 0, h))));

    // --- 段ボールの塗り分け ---
    //   表裏の薄い層（ライナー）＝明るいクラフト紙＋繊維ムラ。
    //   厚みの中央（中芯）＝少し暗い波板。波の山谷で明暗を付けて紙の波が覗いて見えるように。
    //   切り口も溶けて開いた穴も、この塗り分けが「本物の断面」として自動で現れる
    static const float3 kLinerColor = float3(0.76f, 0.60f, 0.38f);
    static const float3 kCoreColor  = float3(0.55f, 0.40f, 0.23f);
    float linerBand = gRoad.halfThick - 0.055f; // これより外側の高さ＝ライナー層
    bool flatFace = abs(n.y) > 0.6f;            // 上面/下面はライナーの紙面
    float3 albedo;
    if (flatFace || abs(hitLocalY) > linerBand)
    {
        float fiber = 0.92f + 0.16f * ValueNoise(p * 21.0f); // 紙の繊維ムラ
        albedo = kLinerColor * fiber;
    }
    else
    {
        albedo = kCoreColor * (0.72f + 0.55f * hitWave);     // 中芯：波の山谷で明暗
    }

    // ハーフランバート＋環境光（クラフト風のやわらかい陰影）
    float3 l = normalize(-gRoad.lightDir);
    float ndl = saturate(dot(n, l) * 0.5f + 0.5f);
    float3 rgb = albedo * (0.25f + 0.75f * ndl);

    // 段ボール焼け：溶け前線ほど焦げ、消える寸前の縁は赤熱（自発光）
    float charT = saturate(hitErode / max(gRoad.erodeBand, 1e-3f));
    float3 charColor = float3(0.13f, 0.08f, 0.05f);
    rgb = lerp(rgb, charColor, smoothstep(0.25f, 0.85f, charT) * 0.9f);
    float ember = smoothstep(0.55f, 0.95f, charT);
    rgb += float3(1.0f, 0.35f, 0.06f) * ember * 1.6f;

    PSOutput output;
    output.color = float4(rgb, gRoad.baseColor.a);
    output.mask  = float4(0.0f, 0.0f, 0.0f, 0.0f);

    float4 clip = mul(float4(p, 1.0f), gRoad.viewProj);
    output.depth = clip.z / clip.w;
    return output;
}
