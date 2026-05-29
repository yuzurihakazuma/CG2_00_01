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
    // 二重初期化を防ぐ（Framework と GamePlayScene の両方から呼ばれても安全）
    if (isInitialized_) { return; }

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

    isInitialized_ = true;
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

    // ⑤ Emit用のUploadバッファを永続確保 (EmitCSがSRVとして参照する)
    const size_t emitSize = sizeof(GPUParticleData) * kMaxEmitPerFrame;
    emitUploadBuffer_ = ResourceFactory::GetInstance()->CreateBufferResource(emitSize);
    emitUploadBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&emitUploadData_));

    // UploadバッファをSRVとして登録 (常にGENERIC_READ状態なので遷移不要)
    srvEmitRequestIdx_ = srvManager_->Allocate();
    srvManager_->CreateSRVforStructuredBuffer(
        srvEmitRequestIdx_, emitUploadBuffer_.Get(),
        kMaxEmitPerFrame, sizeof(GPUParticleData));
}

// -------------------------------------------------------
//  FreeListバッファ (空きスロット管理)
// -------------------------------------------------------
void GPUParticleManager::CreateFreeListBuffers(){
    auto* cmd = dxCommon_->GetCommandList();

    // --- freeListIndexBuffer_ (uint[1]): 空きスロット数 = kMaxParticles ---
    {
        const size_t sz = sizeof(uint32_t);
        freeListIndexBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(sz);

        // UAVとして登録
        uavFreeListIndexIdx_ = srvManager_->Allocate();
        srvManager_->CreateUAVForStructuredBuffer(
            uavFreeListIndexIdx_, freeListIndexBuffer_.Get(), 1, sizeof(uint32_t));

        // 初期値: kMaxParticles (全スロット空き)
        auto uploadBuf = ResourceFactory::GetInstance()->CreateBufferResource(sz);
        uint32_t* ptr = nullptr;
        uploadBuf->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        *ptr = kMaxParticles;
        uploadBuf->Unmap(0, nullptr);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = freeListIndexBuffer_.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd->ResourceBarrier(1, &b);
        cmd->CopyResource(freeListIndexBuffer_.Get(), uploadBuf.Get());
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmd->ResourceBarrier(1, &b);
        // uploadBuf はこのスコープを抜けると解放されるが、
        // ExecuteCommandLists前なので一時保持が必要。initBuffer_と同じ問題を避けるため
        // initBuffer_ に追加するか、別メンバで保持する必要があるが、
        // ここでは簡易的にComPtr局所変数のままにする。
        // (Initializeが同期的にSubmit→Waitするならこれで十分)
        freeListInitUpload0_ = std::move(uploadBuf);
    }

    // --- freeListBuffer_ (uint[kMaxParticles]): [0,1,2,...,N-1] ---
    {
        const size_t sz = sizeof(uint32_t) * kMaxParticles;
        freeListBuffer_ = ResourceFactory::GetInstance()->CreateUAVBuffer(sz);

        uavFreeListIdx_ = srvManager_->Allocate();
        srvManager_->CreateUAVForStructuredBuffer(
            uavFreeListIdx_, freeListBuffer_.Get(), kMaxParticles, sizeof(uint32_t));

        auto uploadBuf = ResourceFactory::GetInstance()->CreateBufferResource(sz);
        uint32_t* ptr = nullptr;
        uploadBuf->Map(0, nullptr, reinterpret_cast<void**>(&ptr));
        for (uint32_t i = 0; i < kMaxParticles; ++i) { ptr[i] = i; }
        uploadBuf->Unmap(0, nullptr);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = freeListBuffer_.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd->ResourceBarrier(1, &b);
        cmd->CopyResource(freeListBuffer_.Get(), uploadBuf.Get());
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmd->ResourceBarrier(1, &b);

        freeListInitUpload1_ = std::move(uploadBuf);
    }
}

// -------------------------------------------------------
//  定数バッファ
// -------------------------------------------------------
void GPUParticleManager::CreateConstantBuffers(){
    // Compute用
    updateCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleUpdateCB) + 0xFF) & ~0xFF);
    updateCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&updateCBData_));

    // Camera用
    cameraCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleCameraCB) + 0xFF) & ~0xFF);
    cameraCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraCBData_));

    // Emit CS用CB
    emitCBResource_ = ResourceFactory::GetInstance()->CreateBufferResource(
        (sizeof(GPUParticleEmitCB) + 0xFF) & ~0xFF);
    emitCBResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitCBData_));

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
// -------------------------------------------------------
void GPUParticleManager::Emit(const Vector3& position, const Vector3& velocity,
    float lifeTime, float scale, const Vector4& color){

    if (emitQueue_.size() >= kMaxEmitPerFrame) return;

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
//  Emit CS: FreeListからスロットを取得してパーティクルを発生
// -------------------------------------------------------
void GPUParticleManager::UploadEmitQueue(ID3D12GraphicsCommandList* commandList){

    lastFrameEmitCount_ = static_cast<uint32_t>(emitQueue_.size());
    totalEmitted_ += lastFrameEmitCount_;

    // EmitリクエストをUploadバッファに書き込む (Emit CSがSRVで読む)
    for (uint32_t i = 0; i < lastFrameEmitCount_; ++i){
        emitUploadData_[i] = emitQueue_[i];
    }
    emitQueue_.clear();

    if (lastFrameEmitCount_ == 0) return;

    // EmitCBに発生数をセット
    emitCBData_->emitCount = lastFrameEmitCount_;

    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Emit CSパイプラインをセット
    PipelineManager::GetInstance()->SetGPUParticleEmitPipeline(commandList);

    srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);           // u0: particles
    srvManager_->SetComputeRootDescriptorTable(1, uavFreeListIndexIdx_); // u1: freeListIndex
    srvManager_->SetComputeRootDescriptorTable(2, uavFreeListIdx_);     // u2: freeList
    srvManager_->SetComputeRootDescriptorTable(3, srvEmitRequestIdx_);  // t0: emitRequests
    commandList->SetComputeRootConstantBufferView(4, emitCBResource_->GetGPUVirtualAddress()); // b0

    UINT groupCount = (lastFrameEmitCount_ + 63) / 64;
    commandList->Dispatch(groupCount, 1, 1);

    // Emit CS と Update CS の間でUAVバリア
    D3D12_RESOURCE_BARRIER barriers[3] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[0].UAV.pResource = particleBuffer_.Get();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[1].UAV.pResource = freeListIndexBuffer_.Get();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barriers[2].UAV.pResource = freeListBuffer_.Get();
    commandList->ResourceBarrier(3, barriers);
}

// -------------------------------------------------------
//  Dispatch (Computeシェーダー実行)
// -------------------------------------------------------
void GPUParticleManager::Dispatch(ID3D12GraphicsCommandList* commandList){
    // 前フレームのUpdate CS書き込みが完了するまで待つ (FreeList含む全UAV)
    D3D12_RESOURCE_BARRIER uavBarriers[3] = {};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[0].UAV.pResource = particleBuffer_.Get();
    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[1].UAV.pResource = freeListIndexBuffer_.Get();
    uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarriers[2].UAV.pResource = freeListBuffer_.Get();
    commandList->ResourceBarrier(3, uavBarriers);

    // Emit CS: FreeListからスロットを取得してパーティクルを発生させる
    UploadEmitQueue(commandList);

    // DescriptorHeapをセット
    ID3D12DescriptorHeap* heaps[] = { srvManager_->GetDescriptorHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Update CS パイプラインをセット
    PipelineManager::GetInstance()->SetGPUParticleComputePipeline(commandList);

    srvManager_->SetComputeRootDescriptorTable(0, uavIndex_);            // [0]: u0 particles
    srvManager_->SetComputeRootDescriptorTable(1, uavFreeListIndexIdx_); // [1]: u1 freeListIndex
    srvManager_->SetComputeRootDescriptorTable(2, uavFreeListIdx_);      // [2]: u2 freeList
    commandList->SetComputeRootConstantBufferView(
        3, updateCBResource_->GetGPUVirtualAddress());                   // [3]: b0 UpdateCB

    // Dispatch (256スレッドのグループを必要数起動)
    UINT groupCount = (kMaxParticles + 255) / 256;
    commandList->Dispatch(groupCount, 1, 1);

    // UAV → SRV (描画パスで読むため状態遷移)
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
    if (cameraCBData_) {
        cameraCBResource_->Unmap(0, nullptr);
        cameraCBData_ = nullptr;
    }
    if (emitCBData_) {
        emitCBResource_->Unmap(0, nullptr);
        emitCBData_ = nullptr;
    }

    initBuffer_.Reset();
    freeListInitUpload0_.Reset();
    freeListInitUpload1_.Reset();
    particleBuffer_.Reset();
    freeListIndexBuffer_.Reset();
    freeListBuffer_.Reset();
    emitUploadBuffer_.Reset();
    updateCBResource_.Reset();
    emitCBResource_.Reset();
    cameraCBResource_.Reset();
    materialCBResource_.Reset();
    vertexResource_.Reset();

    // フラグをリセットして次回 Initialize() が正しく再実行されるようにする
    isInitialized_ = false;
}
