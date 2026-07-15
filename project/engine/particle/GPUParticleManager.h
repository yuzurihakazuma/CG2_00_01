#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <string>
#include "engine/math/struct.h"
#include "engine/math/Matrix4x4.h"

class DirectXCommon;
class SrvManager;
class Camera;

// CSとVS両方で使うパーティクルのデータ (64バイト固定)
struct GPUParticleData{
    Vector3  position;    // 12
    float    lifeTime;    //  4
    Vector3  velocity;    // 12
    float    currentTime; //  4
    Vector4  color;       // 16
    float    scale;       //  4
    uint32_t alive;       //  4
    float    pad[2];      //  8
};

// Computeシェーダーに渡す定数バッファ
struct GPUParticleUpdateCB{
    float    deltaTime;
    float    gravityY;
    uint32_t maxParticles;
    float    pad;
};

// 発生用Computeシェーダーに渡す定数バッファ (ParticleEmit.CS.hlsl の EmitCB)
struct GPUParticleEmitCB{
    uint32_t emitCount; // このフレームに発生させる数
    float    pad[3];
};

// 描画時にVSに渡すカメラ行列
struct GPUParticleCameraCB{
    Matrix4x4 view;
    Matrix4x4 projection;
};

// マテリアル (Particle.hlsliのMaterial構造体に合わせる)
struct GPUParticleMaterial{
    Vector4   color;
    int32_t   enableLighting;
    float     pad[3];
    Matrix4x4 uvTransform;
};

class GPUParticleManager{
public:
    static GPUParticleManager* GetInstance(){
        static GPUParticleManager instance;
        return &instance;
    }

    // 初期化 (テクスチャパスを受け取る)
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
        const std::string& textureFilePath);

    // 毎フレーム更新 (定数バッファに書き込む)
    void Update(float deltaTime, Camera* camera);



    // Computeシェーダー実行 (描画の前に呼ぶ)
    void Dispatch(ID3D12GraphicsCommandList* commandList);

    // 描画 (Dispatchの後に呼ぶ)
    void Draw(ID3D12GraphicsCommandList* commandList);

    // 終了処理
    void Finalize();

    // パーティクルを発生させる
    void Emit(const Vector3& position, const Vector3& velocity,
        float lifeTime, float scale, const Vector4& color);

    void SetGravity(float gravityY){ gravityY_ = gravityY; }


    uint32_t GetLastFrameEmitCount() const{ return lastFrameEmitCount_; }
    uint32_t GetMaxParticles() const{ return kMaxParticles; }
    uint32_t GetTotalEmitted() const{ return totalEmitted_; }

    // FreeList の空きスロット数（GPUから読み戻した実測値。1フレーム遅れ）
    int32_t GetFreeCount() const{
        return freeListReadbackData_ ? ( *freeListReadbackData_ + 1 ) : 0;
    }

private:
    GPUParticleManager() = default;
    ~GPUParticleManager() = default;
    GPUParticleManager(const GPUParticleManager&) = delete;
    GPUParticleManager& operator=(const GPUParticleManager&) = delete;

    void CreateParticleBuffer();   // UAVバッファ作成
    void CreateFreeListBuffers();  // FreeList用バッファ作成 (gFreeList / gFreeListIndex)
    void CreateConstantBuffers();  // CB作成
    void CreateVertexBuffer();     // 板ポリ作成

    // 全Computeシェーダー共通のルートバインド (Dispatch内で使う)
    void BindComputeRoots(ID3D12GraphicsCommandList* commandList);

    // FreeListの初期化Dispatch (最初のフレームに1回だけ実行)
    void DispatchInit(ID3D12GraphicsCommandList* commandList);

    // 発生キューをGPUに転送し、Emit CSで FreeList から空きを取り出して発生させる
    void DispatchEmit(ID3D12GraphicsCommandList* commandList);

private:
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // パーティクルバッファ (Default Heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    uint32_t uavIndex_ = 0; // Compute時に使うUAVのインデックス
    uint32_t srvIndex_ = 0; // Draw時に使うSRVのインデックス

    // Emit用のUploadバッファ (CPUが発生リクエストを書き、Emit CSが t0 で読む)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitUploadBuffer_;
    GPUParticleData* emitUploadData_ = nullptr; // Mapしたポインタ
    uint32_t emitRequestSrvIndex_ = 0;          // Emit CSに渡すSRVのインデックス

    // ===== FreeList（パーティクルの使い回し）用バッファ =====
    // gFreeList: 空きインデックスを積むスタック本体 (uint × kMaxParticles)
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListBuffer_;
    uint32_t freeListUavIndex_ = 0;
    // gFreeListIndex: スタックトップ (int × 1)。-1 なら空きなし
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexBuffer_;
    uint32_t freeListIndexUavIndex_ = 0;
    // FreeListの初期化Dispatchを実行済みか
    bool freeListInitialized_ = false;

    // 空きスロット数の読み戻し用 (Readback Heap。デバッグ表示に使う)
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListReadbackBuffer_;
    int32_t* freeListReadbackData_ = nullptr;

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> updateCBResource_;
    GPUParticleUpdateCB* updateCBData_ = nullptr;

    // 発生用CB (emitCount)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitCBResource_;
    GPUParticleEmitCB* emitCBData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cameraCBResource_;
    GPUParticleCameraCB* cameraCBData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> materialCBResource_;

    // 板ポリ頂点バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    // テクスチャ
    uint32_t textureSrvIndex_ = 0;

    // 最大パーティクル数
    static constexpr uint32_t kMaxParticles = 100000;
    // 1フレームに発生できる最大数
    static constexpr uint32_t kMaxEmitPerFrame = 1000;

    // Emit用の発生リクエストキュー
    // （書き込み先スロットはCPUでは決めない。GPUのEmit CSが FreeList から取り出す）
    std::vector<GPUParticleData> emitQueue_;

	// 初期化用バッファ (全パーティクルをalive=0で初期化するための一時バッファ)
    Microsoft::WRL::ComPtr<ID3D12Resource> initBuffer_;

    float gravityY_ = -0.098f;

   
    uint32_t lastFrameEmitCount_ = 0;  // 直前フレームのEmit数
    uint32_t totalEmitted_ = 0;  // 累計Emit数
};
