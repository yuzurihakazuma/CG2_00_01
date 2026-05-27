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

    // 初期化済みかどうかを外部から確認できるようにする
    bool IsInitialized() const { return isInitialized_; }

    uint32_t GetLastFrameEmitCount() const{ return lastFrameEmitCount_; }
    uint32_t GetMaxParticles() const{ return kMaxParticles; }
    uint32_t GetTotalEmitted() const{ return totalEmitted_; }

private:
    GPUParticleManager() = default;
    ~GPUParticleManager() = default;
    GPUParticleManager(const GPUParticleManager&) = delete;
    GPUParticleManager& operator=(const GPUParticleManager&) = delete;

    void CreateParticleBuffer();   // UAVバッファ作成
    void CreateConstantBuffers();  // CB作成
    void CreateVertexBuffer();     // 板ポリ作成

    // 発生キューをGPUに転送する (Dispatch内で呼ぶ)
    void UploadEmitQueue(ID3D12GraphicsCommandList* commandList);

private:
    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;

    // パーティクルバッファ (Default Heap)
    Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
    uint32_t uavIndex_ = 0; // Compute時に使うUAVのインデックス
    uint32_t srvIndex_ = 0; // Draw時に使うSRVのインデックス

    // Emit用のUploadバッファ (CPU→GPU転送に使う)
    Microsoft::WRL::ComPtr<ID3D12Resource> emitUploadBuffer_;
    GPUParticleData* emitUploadData_ = nullptr; // Mapしたポインタ

    // 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> updateCBResource_;
    GPUParticleUpdateCB* updateCBData_ = nullptr;

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

    // Emit用 (スロット番号 + データ のペア)
    struct EmitEntry { uint32_t slot; GPUParticleData data; };
    std::vector<EmitEntry> emitQueue_;
    uint32_t nextFreeSlot_ = 0; // 循環インデックス

	// 初期化用バッファ (全パーティクルをalive=0で初期化するための一時バッファ)
    Microsoft::WRL::ComPtr<ID3D12Resource> initBuffer_;

    float gravityY_ = -0.098f;

    bool isInitialized_ = false; // Initialize()が完了したかのフラグ

    uint32_t lastFrameEmitCount_ = 0;  // 直前フレームのEmit数
    uint32_t totalEmitted_ = 0;  // 累計Emit数
};
