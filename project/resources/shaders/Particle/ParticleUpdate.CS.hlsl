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
RWStructuredBuffer<GPUParticleData> gParticles      : register(u0);
// u1: FreeListのスタックトップ (空きスロット数)
RWStructuredBuffer<uint>            gFreeListIndex  : register(u1);
// u2: FreeListの本体 (空きスロット番号の配列)
RWStructuredBuffer<uint>            gFreeList       : register(u2);

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

    // 寿命を超えたら死亡 → インデックスをFreeListに返却
    if (gParticles[i].currentTime >= gParticles[i].lifeTime)
    {
        gParticles[i].alive = 0;

        uint slot;
        InterlockedAdd(gFreeListIndex[0], 1, slot);
        gFreeList[slot] = i;
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
