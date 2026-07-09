#pragma once
// --- 標準・外部ライブラリ ---
#include <cstdint>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl.h>


// 前方宣言
class DirectXCommon;

// SRVマネージャー
class SrvManager{
public:
	static SrvManager* GetInstance();

	// 初期化
	void Initialize(DirectXCommon* dxCommon);
	// 終了処理
	void Finalize();

	// SRV用ディスクリプタヒープからディスクリプタを1つ割り当てる
	uint32_t Allocate();

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

	// 2Dテクスチャ用SRVの作成
	void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

	void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements, UINT structureByteStride);

	void CreateSRVforTextureCube(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

	// 3Dテクスチャ（ボリューム）用SRVの作成（SDFボリュームのレイマーチングで使用）
	void CreateSRVforTexture3D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels);

	void PreDraw();

	void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

	ID3D12DescriptorHeap* GetDescriptorHeap() const{
		return descriptorHeaps_.Get();
	}

	// UAV用のディスクリプタを登録する
	void CreateUAVForStructuredBuffer(uint32_t index, ID3D12Resource* resource,
		UINT numElements, UINT stride);

	// ComputeシェーダーにDescriptorTableをセット
	void SetComputeRootDescriptorTable(UINT rootParamIndex, uint32_t srvIndex);

private:


	// ダイレクトX
	DirectXCommon* dxCommon_ = nullptr;

	// 最大SRV数
	static constexpr  uint32_t kMaxSrvCount_ = 2048;

	// 現在のSRV数
	uint32_t descriptorSize_;
	// ディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeaps_;

	// 使用中のインデックス
	uint32_t useIndex_ = 0;
private:
	// コンストラクタを private にして外部からの new を禁止
	SrvManager() = default;
	~SrvManager() = default;
	SrvManager(const SrvManager&) = delete;
	SrvManager& operator=(const SrvManager&) = delete;

};

