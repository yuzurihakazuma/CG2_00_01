#include "SrvManager.h"
// -- - 標準ライブラリ-- -
#include <cassert>

// --- エンジン側のファイル ---
#include "engine/base/DirectXCommon.h"

SrvManager* SrvManager::GetInstance() {
	static SrvManager instance;
	return &instance;
}


void SrvManager::Initialize(DirectXCommon* dxCommon){

	this->dxCommon_ = dxCommon;
	// ディスクリプタサイズの取得
	descriptorHeaps_ = dxCommon_->CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		kMaxSrvCount_,
		true
	);
	// デスクリプタサイズの取得
	descriptorSize_ = dxCommon_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	

}


uint32_t SrvManager::Allocate(){
	
	assert(useIndex_ < kMaxSrvCount_);
	
	// 使用中のインデックスを返してインクリメント
	int index = useIndex_;
	// Maxを超えたらエラー
	useIndex_++;
	
	return index;// 戻り値
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index){

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeaps_->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += ( descriptorSize_ * index );
	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index){
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeaps_->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += ( descriptorSize_ * index );
	return handleGPU;
}

void SrvManager::CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels){

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = MipLevels;

	dxCommon_->GetDevice()->CreateShaderResourceView(
		pResource,
		&srvDesc,
		GetCPUDescriptorHandle(srvIndex) 
	);

}

void SrvManager::CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT numElements, UINT structureByteStride){


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = numElements;
	srvDesc.Buffer.StructureByteStride = structureByteStride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	dxCommon_->GetDevice()->CreateShaderResourceView(
		pResource,
		&srvDesc,
		GetCPUDescriptorHandle(srvIndex));



}

void SrvManager::CreateSRVforTextureCube(uint32_t index, ID3D12Resource* resource, DXGI_FORMAT format, UINT mipLevels){
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	//  ここを TextureCube に設定
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = mipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

	dxCommon_->GetDevice()->CreateShaderResourceView(resource, &srvDesc, GetCPUDescriptorHandle(index));
}

// 3Dテクスチャ（ボリューム）用SRV。SDFボリュームのレイマーチングで使用する
void SrvManager::CreateSRVforTexture3D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT Format, UINT MipLevels){
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Texture3D.MostDetailedMip = 0;
	srvDesc.Texture3D.MipLevels = MipLevels;
	srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;

	dxCommon_->GetDevice()->CreateShaderResourceView(pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::PreDraw(){
		// SRVヒープの設定
	ID3D12DescriptorHeap* descriptorHeap[] = {descriptorHeaps_.Get()};
	dxCommon_->GetCommandList()->SetDescriptorHeaps(1, descriptorHeap);
}

void SrvManager::SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex){

		dxCommon_->GetCommandList()->SetGraphicsRootDescriptorTable(
			RootParameterIndex,
			GetGPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateUAVForStructuredBuffer(
	uint32_t index, ID3D12Resource* resource, UINT numElements, UINT stride){
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = numElements;
	uavDesc.Buffer.StructureByteStride = stride;

	dxCommon_->GetDevice()->CreateUnorderedAccessView(
		resource, nullptr, &uavDesc,
		GetCPUDescriptorHandle(index)
	);
}

void SrvManager::SetComputeRootDescriptorTable(UINT rootParamIndex, uint32_t srvIndex){
	// SrvManagerのヒープはGraphics/Compute両用で使える
	dxCommon_->GetCommandList()->SetComputeRootDescriptorTable(
		rootParamIndex,
		GetGPUDescriptorHandle(srvIndex)
	);
}


void SrvManager::Finalize() {
	descriptorHeaps_.Reset(); // ヒープを解放
}