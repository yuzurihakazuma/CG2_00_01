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

// u1: FreeList（空きスロット番号のスタック。gFreeList[0 .. 空き数-1] が有効）
RWStructuredBuffer<uint> gFreeList : register(u1);

// u2: FreeListの空き数カウンタ（[0]のみ使用。Emitで減り、死亡返却で増える）
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

    // 寿命を超えたら死亡
    if (gParticles[i].currentTime >= gParticles[i].lifeTime)
    {
        gParticles[i].alive = 0;

        // 空いたスロット番号を FreeList へ返却する（Emit側が再利用できるように）。
        // InterlockedAdd は加算前の値を返すので、加算前の「空き数」が次の格納先になる
        int freeListIndex;
        InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
        if (freeListIndex < (int) maxParticles)
        {
            gFreeList[freeListIndex] = i;
        }
        else
        {
            // 万一あふれたらカウンタを戻す（全スロット返却済みなら理論上起きない）
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
