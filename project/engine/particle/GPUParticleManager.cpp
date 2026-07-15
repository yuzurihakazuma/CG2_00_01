#include "GPUParticleManager.h"

#include <cassert>
#include <cstring>

#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/TextureManager.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

using namespace MatrixMath;

// -------------------------------------------------------
//  初期化
// -------------------------------------------------------
void GPUParticleManager::Initialize(
    DirectXCommon* dxCommon, SrvManager* srvManager,
    const std::string& textureFilePath){
    assert(dxCommon);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;

    CreateParticleBuffer();
    CreateFreeListBuffers();
    CreateConstantBuffers();
    CreateVertexBuffer();

    // テクスチャ読み込み
    ID3D12GraphicsCommandList* cmd = dxCommon_->GetCommandList();
    TextureData texData = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
        textureFilePath, cmd);
    textureSrvIndex_ = texData.srvIndex;
}

// -------------------------------------------------------
//  パーティクルバッファ (UAV + SRV)
// -------------------------------------------------------
void GPUParticleManager::CreateParticleBuffer(){
    const size_t bufferSize = sizeof(GPUParticleData) * kMaxParticles;

    // ① Default Heap に UAVバッファを作成
    particleBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(bufferSize);

    // ② UAVとして登録 (Computeシェーダーで書き込む用)
    uavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        uavIndex_, particleBuffer_.Get(),
        kMaxParticles, sizeof(GPUParticleData));

    // ③ SRVとして登録 (描画時にVSで読む用)
    srvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        srvIndex_, particleBuffer_.Get(),
        kMaxParticles, sizeof(GPUParticleData));

    // ④ 初期化: 全パーティクルを alive=0 にする
    //    Upload Heap の一時バッファを使ってコピー
    initBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(bufferSize); GPUParticleData* initData = nullptr;
    initBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&initData));
    ZeroMemory(initData, bufferSize); // alive=0 で初期化
    initBuffer_->Unmap(0, nullptr);

    auto* cmd = dxCommon_->GetCommandList();

    // COMMON → COPY_DEST
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    cmd->ResourceBarrier(1, &barrier);

    cmd->CopyResource(particleBuffer_.Get(), initBuffer_.Get());

    // COPY_DEST → UNORDERED_ACCESS
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmd->ResourceBarrier(1, &barrier);

    // ⑤ Emit用のUploadバッファを永続確保
    const size_t emitSize = sizeof(GPUParticleData) * kMaxEmitPerFrame;
    emitUploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(emitSize);
    emitUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&emitUploadData_));

    // ⑥ Emit CS が t0 で読めるように、UploadバッファのSRVを作る
    emitRequestSrvIndex_ = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        emitRequestSrvIndex_, emitUploadBuffer_.Get(),
        kMaxEmitPerFrame, sizeof(GPUParticleData));
}

// -------------------------------------------------------
//  FreeList用バッファ (gFreeList / gFreeListIndex)
//  パーティクルを使い回すための「空きインデックスのスタック」
// -------------------------------------------------------
void GPUParticleManager::CreateFreeListBuffers(){
    // ① gFreeList: 空きインデックスのスタック本体 (uint × kMaxParticles)
    freeListBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(
        sizeof(uint32_t) * kMaxParticles);
    freeListUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        freeListUavIndex_, freeListBuffer_.Get(),
        kMaxParticles, sizeof(uint32_t));

    // ② gFreeListIndex: スタックトップ (int × 1)
    freeListIndexBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(sizeof(int32_t));
    freeListIndexUavIndex_ = srvManager_->Allocate();
    srvManager_->CreateUAVForStructuredBuffer(
        freeListIndexUavIndex_, freeListIndexBuffer_.Get(),
        1, sizeof(int32_t));

    // ③ 空き数のデバッグ表示用に、スタックトップを読み戻す Readback バッファ
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeof(int32_t);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&freeListReadbackBuffer_));
        assert(SUCCEEDED(hr));
        freeListReadbackBuffer_->Map(0, nullptr,
            reinterpret_cast<void**>( &freeListReadbackData_ ));
    }

    // ※FreeListの中身の初期化（0〜max-1 を積む）は最初の Dispatch 時に
    //   ParticleInit.CS.hlsl を1回実行して GPU 側で行う（DispatchInit参照）
}

// -------------------------------------------------------
//  定数バッファ
// -------------------------------------------------------
void GPUParticleManager::CreateConstantBuffers(){
    // Compute用
    updateCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleUpdateCB) + 0xFF) & ~0xFF);
    updateCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&updateCBData_));
    // Update が G キー押下時しか呼ばれなくても Init/Emit CS が maxParticles を
    // 正しく読めるように、初期値をここで入れておく
    updateCBData_->deltaTime = 0.0f;
    updateCBData_->gravityY = gravityY_;
    updateCBData_->maxParticles = kMaxParticles;
    updateCBData_->pad = 0.0f;

    // Emit用 (emitCount)
    emitCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleEmitCB) + 0xFF) & ~0xFF);
    emitCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitCBData_));
    emitCBData_->emitCount = 0;

    // Camera用
    cameraCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleCameraCB) + 0xFF) & ~0xFF);
    cameraCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraCBData_));

    // Material用 (白・ライトなし)
    materialCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleMaterial) + 0xFF) & ~0xFF);
    GPUParticleMaterial* mat = nullptr;
    materialCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&mat));
    mat->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    mat->enableLighting = 0;
    mat->uvTransform = MakeIdentity4x4();
    materialCBResource_->Unmap(0, nullptr);
}

// -------------------------------------------------------
//  板ポリ頂点バッファ (ParticleManagerと同じ形式)
// -------------------------------------------------------
void GPUParticleManager::CreateVertexBuffer(){
    struct VertexData { Vector4 position; Vector2 texcoord; Vector3 normal; };

    VertexData vertices[] = {
        {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };

    vertexResource_ = ResourceFactory::GetInstance()->CreateBufferResource(sizeof(vertices));

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = sizeof(vertices);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);

    void* data = nullptr;
    vertexResource_->Map(0, nullptr, &data);
    std::memcpy(data, vertices, sizeof(vertices));
    vertexResource_->Unmap(0, nullptr);
}

// -------------------------------------------------------
//  毎フレーム更新
// -------------------------------------------------------
void GPUParticleManager::Update(float deltaTime, Camera* camera){
    updateCBData_->deltaTime = deltaTime;
    updateCBData_->gravityY = gravityY_;
    updateCBData_->maxParticles = kMaxParticles;

    cameraCBData_->view = camera->GetViewMatrix();
    cameraCBData_->projection = camera->GetProjectionMatrix();
}

// -------------------------------------------------------
//  Emit (発生キューに追加)
//  書き込み先スロットはCPUでは決めない。
//  GPUの Emit CS が FreeList から空きインデックスを取り出して書き込む。
// -------------------------------------------------------
void GPUParticleManager::Emit(const Vector3& position, const Vector3& velocity,
    float lifeTime, float scale, const Vector4& color){

    if (emitQueue_.size() >= kMaxEmitPerFrame) return; // 1フレームの上限チェック

    GPUParticleData p = {};
    p.position = position;
    p.velocity = velocity;
    p.lifeTime = lifeTime;
    p.currentTime = 0.0f;
    p.color = color;
    p.scale = scale;
    p.alive = 1;

    emitQueue_.push_back(p);
}

// -------------------------------------------------------
//  全Computeシェーダー共通のルートバインド
//  （Init / Emit / Update は同じルートシグネチャを使う）
// -------------------------------------------------------
void GPUParticleManager::BindComputeRoots(ID3D12GraphicsCommandList* commandList){
    // [0]: u0 パーティクルバッファ (UAV)
    srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);
    // [1]: b0 更新用CB
    commandList->SetComputeRootConstantBufferView(
        1, updateCBResource_->GetGPUVirtualAddress());
    // [2]: u1 FreeList本体 (UAV)
    srvManager_->SetComputeRootDescriptorTable(2, freeListUavIndex_);
    // [3]: u2 FreeListスタックトップ (UAV)
    srvManager_->SetComputeRootDescriptorTable(3, freeListIndexUavIndex_);
    // [4]: t0 発生リクエスト (SRV)
    srvManager_->SetComputeRootDescriptorTable(4, emitRequestSrvIndex_);
    // [5]: b1 発生用CB
    commandList->SetComputeRootConstantBufferView(
        5, emitCBResource_->GetGPUVirtualAddress());
}

// -------------------------------------------------------
//  FreeListの初期化Dispatch (最初のフレームに1回だけ)
//  gFreeList に 0〜max-1 を積み、スタックトップを max-1 にする
// -------------------------------------------------------
void GPUParticleManager::DispatchInit(ID3D12GraphicsCommandList* commandList){
    // FreeList用バッファを COMMON → UNORDERED_ACCESS へ
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = freeListBuffer_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barriers[1] = barriers[0];
    barriers[1].Transition.pResource = freeListIndexBuffer_.Get();
    commandList->ResourceBarrier(2, barriers);

    // Init CS を全パーティクル分実行
    PipelineManager::GetInstance()->SetGPUParticleInitPipeline(commandList);
    BindComputeRoots(commandList);
    UINT groupCount = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCount, 1, 1);

    // Initの書き込みが終わってから後続（Emit/Update）が読めるようにUAVバリア
    D3D12_RESOURCE_BARRIER uavBarriers[3] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = particleBuffer_.Get();
    uavBarriers[1] = uavBarriers[0];
    uavBarriers[1].UAV.pResource = freeListBuffer_.Get();
    uavBarriers[2] = uavBarriers[0];
    uavBarriers[2].UAV.pResource = freeListIndexBuffer_.Get();
    commandList->ResourceBarrier(3, uavBarriers);

    freeListInitialized_ = true;
}

// -------------------------------------------------------
//  発生Dispatch
//  CPUが積んだ発生リクエストをUploadバッファへ書き、
//  Emit CS が FreeList から空きスロットを取り出して発生させる
// -------------------------------------------------------
void GPUParticleManager::DispatchEmit(ID3D12GraphicsCommandList* commandList){

    lastFrameEmitCount_ = static_cast< uint32_t >( emitQueue_.size() );
    totalEmitted_ += lastFrameEmitCount_;

    if (emitQueue_.empty()) return;

    // 発生リクエストをUploadバッファ(=Emit CSのt0)へ書き込む
    for (uint32_t i = 0; i < lastFrameEmitCount_; i++) {
        emitUploadData_[i] = emitQueue_[i];
    }
    emitQueue_.clear();

    // 発生数をCBへ
    emitCBData_->emitCount = lastFrameEmitCount_;

    // Emit CS を発生数分だけ実行
    PipelineManager::GetInstance()->SetGPUParticleEmitPipeline(commandList);
    BindComputeRoots(commandList);
    UINT groupCount = (lastFrameEmitCount_ + 255) / 256;
    commandList->Dispatch(groupCount, 1, 1);

    // Emitの書き込みが終わってから Update が読めるようにUAVバリア
    D3D12_RESOURCE_BARRIER uavBarriers[3] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = particleBuffer_.Get();
    uavBarriers[1] = uavBarriers[0];
    uavBarriers[1].UAV.pResource = freeListBuffer_.Get();
    uavBarriers[2] = uavBarriers[0];
    uavBarriers[2].UAV.pResource = freeListIndexBuffer_.Get();
    commandList->ResourceBarrier(3, uavBarriers);
}

// -------------------------------------------------------
//  Dispatch (Computeシェーダー実行)
//  順序: (初回のみInit) → Emit → Update → 描画用に状態遷移
// -------------------------------------------------------
void GPUParticleManager::Dispatch(ID3D12GraphicsCommandList* commandList){
    // DescriptorHeapをセット（全Compute共通）
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // 前フレームのCompute書き込みが完了するまで待つUAVバリア
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = particleBuffer_.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    // ① 初回のみ: FreeListを構築（0〜max-1 が全部空き）
    if (!freeListInitialized_) {
        DispatchInit(commandList);
    }

    // ② 発生: FreeListから空きを取り出してパーティクルを書き込む
    DispatchEmit(commandList);

    // ③ 更新: 物理更新＋寿命が尽きたらFreeListへインデックス返却
    PipelineManager::GetInstance()->SetGPUParticleComputePipeline(commandList);
    BindComputeRoots(commandList);
    UINT groupCount = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCount, 1, 1);

    // ④ 空きスロット数をデバッグ表示用に読み戻す
    //    (スタックトップを Readback バッファへコピー。UAV→COPY_SOURCE→UAV)
    {
        D3D12_RESOURCE_BARRIER toCopy = {};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = freeListIndexBuffer_.Get();
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList->ResourceBarrier(1, &toCopy);

        commandList->CopyBufferRegion(
            freeListReadbackBuffer_.Get(), 0,
            freeListIndexBuffer_.Get(), 0, sizeof(int32_t));

        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        commandList->ResourceBarrier(1, &toCopy);
    }

    // ⑤ UAV → SRV (描画パスで読むため状態遷移)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

// -------------------------------------------------------
//  Draw (描画)
// -------------------------------------------------------
void GPUParticleManager::Draw(ID3D12GraphicsCommandList* commandList){
    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // 描画用パイプラインをセット
    PipelineManager::GetInstance()->SetGPUParticleDrawPipeline(commandList);

    // [0]: マテリアル (b0 PIXEL)
    commandList->SetGraphicsRootConstantBufferView(
        0, materialCBResource_->GetGPUVirtualAddress());
    // [1]: パーティクルデータSRV (t1 VERTEX)
    srvManager_->SetGraphicsRootDescriptorTable(1, srvIndex_);
    // [2]: テクスチャ (t0 PIXEL)
    srvManager_->SetGraphicsRootDescriptorTable(2, textureSrvIndex_);
    // [3]: ライトCB (b1 PIXEL) ← 今回はダミーでmaterialと同じアドレスを指す
    commandList->SetGraphicsRootConstantBufferView(
        3, materialCBResource_->GetGPUVirtualAddress());
    // [4]: カメラ行列 (b1 VERTEX)
    commandList->SetGraphicsRootConstantBufferView(
        4, cameraCBResource_->GetGPUVirtualAddress());

    // 板ポリ6頂点 × kMaxParticles インスタンス
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
    commandList->DrawInstanced(6, kMaxParticles, 0, 0);

    // SRV → UAV に戻す (次フレームのComputeのため)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = particleBuffer_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &barrier);
}

// -------------------------------------------------------
//  終了処理
// -------------------------------------------------------
void GPUParticleManager::Finalize(){
    if (emitUploadData_) {
        emitUploadBuffer_->Unmap(0, nullptr);
        emitUploadData_ = nullptr;
    }
    if (updateCBData_) {
        updateCBResource_->Unmap(0, nullptr);
        updateCBData_ = nullptr;
    }
    if (emitCBData_) {
        emitCBResource_->Unmap(0, nullptr);
        emitCBData_ = nullptr;
    }
    if (cameraCBData_) {
        cameraCBResource_->Unmap(0, nullptr);
        cameraCBData_ = nullptr;
    }
    if (freeListReadbackData_) {
        freeListReadbackBuffer_->Unmap(0, nullptr);
        freeListReadbackData_ = nullptr;
    }

    initBuffer_.Reset();
    particleBuffer_.Reset();
    emitUploadBuffer_.Reset();
    freeListBuffer_.Reset();
    freeListIndexBuffer_.Reset();
    freeListReadbackBuffer_.Reset();
    updateCBResource_.Reset();
    emitCBResource_.Reset();
    cameraCBResource_.Reset();
    materialCBResource_.Reset();
    vertexResource_.Reset();

    // 再初期化に備えてフラグを戻す
    freeListInitialized_ = false;
}
