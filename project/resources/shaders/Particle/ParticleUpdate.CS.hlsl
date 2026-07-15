// GPU側でパーティクルの物理演算を行うコンピュートシェーダー
struct GPUParticleData
{
    float3 position;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
    float scale;
    uint alive; // 1=生存, 0=死亡
    float2 pad;
};

// u0: 読み書き可能なパーティクルバッファ
RWStructuredBuffer<GPUParticleData> gParticles : register(u0);

// u1: FreeList本体（空きインデックスを積むスタック）
RWStructuredBuffer<uint> gFreeList : register(u1);

// u2: FreeListの先頭位置（スタックトップ。[0]の1要素だけ使う）
//     値は「最後の有効要素のインデックス」。-1 なら空きなし。
RWStructuredBuffer<int> gFreeListIndex : register(u2);

// b0: 更新用定数バッファ
cbuffer UpdateCB : register(b0)
{
    float deltaTime;
    float gravityY;
    uint maxParticles;
    float pad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= maxParticles)
        return;
    if (gParticles[i].alive == 0)
        return;

    // 時間を進める
    gParticles[i].currentTime += deltaTime;

    // 寿命を超えたら死亡 → 自分のインデックスを FreeList に返却（使い回しの核心）
    if (gParticles[i].currentTime >= gParticles[i].lifeTime)
    {
        gParticles[i].alive = 0;
        gParticles[i].scale = 0.0f; // 念のため見えなくしておく

        // スタックトップを1つ進めて、空いた場所に自分のインデックスを置く。
        // InterlockedAdd で「加算前の値」を受け取るので、
        // 複数スレッドが同時に死んでも別々の場所に書き込める。
        int freeListIndex;
        InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
        if ((freeListIndex + 1) < (int) maxParticles)
        {
            gFreeList[freeListIndex + 1] = i;
        }
        else
        {
            // 理論上ここには来ない（FreeListが最大数を超えることはない）が、
            // 万一あふれたらカウンターを戻して整合性を保つ
            InterlockedAdd(gFreeListIndex[0], -1);
        }
        return;
    }

    // 重力
    gParticles[i].velocity.y += gravityY * deltaTime;

    // 位置更新
    gParticles[i].position += gParticles[i].velocity * deltaTime;

    // 経過割合(0→1)でα値をフェードアウト
    float t = gParticles[i].currentTime / gParticles[i].lifeTime;
    gParticles[i].color.a = 1.0f - t;
}
