#include "TextureManager.h"
// --- 標準ライブラリ ---
#include <cassert>
#include <format>

// --- エンジン側のファイル ---
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/utils/ImGuiManager.h"

// ヘッダーで using を消した分、cpp 側で宣言しておく（元のコードを書き換えないため）
using Microsoft::WRL::ComPtr;
using namespace logs;


// 初期化
void TextureManager::Initialize(ComPtr<ID3D12Device> device, DirectXCommon* dxCommon,SrvManager* srvManager){
	this->device_ = device;
	this->dxCommon_ = dxCommon;
	this->srvManager_ = srvManager;

	assert(device_ != nullptr && "TextureManager: device is null");
	assert(dxCommon_ != nullptr && "TextureManager: dxCommon is null");
	assert(srvManager_ != nullptr && "TextureManager: srvManager is null");

}

// 終了処理
void TextureManager::Finalize(){
	// テクスチャデータ解放
	textureDatas.clear();
	// 中間リソース解放
	intermediateResources_.clear();

	device_.Reset();

}

// 画像ファイルを読み込み、ScratchImage 形式で返す
DirectX::ScratchImage TextureManager::LoadTexture(const std::string& filePath){

	// 1. 画像ファイルを読み込む
	DirectX::ScratchImage image {};
	std::wstring filePathw = logManager.ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathw.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);

	if ( filePath.ends_with(".dds") || filePath.ends_with(".DDS") ) {
		// DDS用読み込み (CubeMapやMipMapを保持したまま読み込める)
		hr = DirectX::LoadFromDDSFile(filePathw.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
	} else {
		// PNG/JPGなど WIC用読み込み
		hr = DirectX::LoadFromWICFile(filePathw.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	}


	if ( FAILED(hr) ) {
		logManager.Log("Failed to load texture: " + filePath + "\n");
		assert(false);
		return image;
	}

	// 2. 圧縮されていたら展開
	if ( !filePath.ends_with(".dds")&& !filePath.ends_with(".DDS") ){

		if ( DirectX::IsCompressed(image.GetMetadata().format) ) {
			DirectX::ScratchImage decompressedImage {};
			hr = DirectX::Decompress(
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), DXGI_FORMAT_UNKNOWN, decompressedImage);
			if ( SUCCEEDED(hr) ) {
				image = std::move(decompressedImage);
			}
		}

	}
	
	

	// 3. Format:91 (BGRA) 問題を回避するため、RGBAに変換
	if ( image.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) {
		DirectX::ScratchImage convertedImage {};
		hr = DirectX::Convert(
			image.GetImages(), image.GetImageCount(),
			image.GetMetadata(),
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DirectX::TEX_FILTER_SRGB,
			0,
			convertedImage
		);
		if ( SUCCEEDED(hr) ) {
			image = std::move(convertedImage);
		}
	}

	return std::move(image);
}
// 短縮版：commandList を内部（DirectXCommon）から取得して読み込む
TextureData TextureManager::Load(const std::string& filePath){
	return LoadTextureAndCreateSRV(filePath, dxCommon_->GetCommandList());
}
TextureData TextureManager::LoadCube(const std::string& filePath){
	return LoadTextureAndCreateSRVCube(filePath, dxCommon_->GetCommandList());
}

 [[nodiscard]]
TextureData TextureManager::LoadTextureAndCreateSRV(const std::string& filePath,ID3D12GraphicsCommandList* commandList){
	// すでにロード済みならそれを返す
	auto it = textureDatas.find(filePath);
	if ( it != textureDatas.end() ) {
		return it->second;
	}

	TextureData texData {};

	// 1. 画像読み込み
	DirectX::ScratchImage image = LoadTexture(filePath);
	const DirectX::TexMetadata& metadata = image.GetMetadata();


	// 2. GPUリソース生成
	if ( metadata.IsCubemap() ) {
		texData.resource = CreateCubemapResource(metadata);
	} else {
		texData.resource = CreateTextureResource(metadata);
	}
	// 3. GPUへデータ転送
	UploadTextureData(texData.resource, image, commandList);

	// 4. SRV用インデックスを確保
	texData.srvIndex = srvManager_->Allocate();

	// 4. テクスチャサイズを保存
	texData.width = static_cast< float >( metadata.width );
	texData.height = static_cast< float >( metadata.height );

	// --- SRV作成も振り分ける ---
	if ( metadata.IsCubemap() ) {
		srvManager_->CreateSRVforTextureCube(
			texData.srvIndex, texData.resource.Get(), metadata.format, static_cast< UINT >( metadata.mipLevels )
		);
	} else {
		srvManager_->CreateSRVforTexture2D(
			texData.srvIndex, texData.resource.Get(), metadata.format, static_cast< UINT >( metadata.mipLevels )
		);
	}

	// 6. キャッシュに保存
	textureDatas[filePath] = texData;

	return texData;
}

// キャッシュを無視して再読み込み（SDFアトラス等のホットリロード用）
TextureData TextureManager::ReloadTextureAndCreateSRV(const std::string& filePath, ID3D12GraphicsCommandList* commandList){

	// 既存エントリがあれば SRV スロットを引き継ぐ（参照側のインデックスを無効にしない）
	uint32_t reuseSrvIndex = UINT32_MAX;
	auto it = textureDatas.find(filePath);
	if ( it != textureDatas.end() ) {
		reuseSrvIndex = it->second.srvIndex;
		textureDatas.erase(it);
	}

	TextureData texData {};

	// 1. 画像読み込み
	DirectX::ScratchImage image = LoadTexture(filePath);
	const DirectX::TexMetadata& metadata = image.GetMetadata();

	// 2. GPUリソース生成
	texData.resource = CreateTextureResource(metadata);

	// 3. GPUへデータ転送
	UploadTextureData(texData.resource, image, commandList);

	// 4. SRVインデックス（再利用 or 新規確保）
	texData.srvIndex = ( reuseSrvIndex != UINT32_MAX ) ? reuseSrvIndex : srvManager_->Allocate();

	texData.width = static_cast< float >( metadata.width );
	texData.height = static_cast< float >( metadata.height );

	// 5. SRVを同じスロットに上書き作成
	srvManager_->CreateSRVforTexture2D(
		texData.srvIndex, texData.resource.Get(), metadata.format, static_cast< UINT >( metadata.mipLevels )
	);

	// 6. キャッシュ更新
	textureDatas[filePath] = texData;

	return texData;
}

TextureData TextureManager::LoadTextureAndCreateSRVCube(const std::string& filePath, ID3D12GraphicsCommandList* commandList){
	// すでにロード済みならそれを返す
	auto it = textureDatas.find(filePath);
	if ( it != textureDatas.end() ) {
		return it->second;
	}

	TextureData texData {};

	// 1. 画像読み込み (DDS対応済みの LoadTexture が呼ばれる)
	DirectX::ScratchImage image = LoadTexture(filePath);
	const DirectX::TexMetadata& metadata = image.GetMetadata();

	// 2. GPUテクスチャ作成 (★キューブマップ専用リソース作成を呼ぶ)
	texData.resource = CreateCubemapResource(metadata);

	// 3. GPUへデータ転送
	UploadTextureData(texData.resource, image, commandList);

	// 4. SRV用インデックスを確保
	texData.srvIndex = srvManager_->Allocate();
	texData.width = static_cast< float >( metadata.width );
	texData.height = static_cast< float >( metadata.height );

	// 5. SRV作成 (★さきほど作ったキューブマップ専用SRV作成を呼ぶ)
	srvManager_->CreateSRVforTextureCube(
		texData.srvIndex,
		texData.resource.Get(),
		metadata.format,
		static_cast< UINT >( metadata.mipLevels )
	);

	// 6. キャッシュに保存
	textureDatas[filePath] = texData;

	return texData;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateTextureResource(const DirectX::TexMetadata& metadata){
	// 1. metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc {};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);
	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Alignment = 0;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COPY_DEST;

	// 3. Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr_ = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		initialState,
		nullptr, IID_PPV_ARGS(&resource));

	if ( FAILED(hr_) ) {
		// 基本ログ
		std::string log = std::format(
			"CreateTextureResource Failed. hr=0x{:08X}\nWidth:{} Height:{} MipLevels:{} Format:{}\n",
			static_cast<unsigned>(hr_),
			resourceDesc.Width, resourceDesc.Height, resourceDesc.MipLevels, static_cast<int>(resourceDesc.Format)
		);
		logManager.Log(log);

		// デバイス削除理由を問い合わせてログに残す
		if ( device_ ) {
			HRESULT removedReason = device_->GetDeviceRemovedReason();
			logManager.Log(std::format("GetDeviceRemovedReason: 0x{:08X}\n", static_cast<unsigned>(removedReason)));

#if defined(_DEBUG)
			// InfoQueue をダンプ（デバッグビルド限定）
			Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
			if ( SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))) ) {
				UINT64 count = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
				for ( UINT64 i = 0; i < count; ++i ) {
					SIZE_T msgLen = 0;
					infoQueue->GetMessage(i, nullptr, &msgLen);
					std::vector<char> buf(msgLen);
					auto msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
					infoQueue->GetMessage(i, msg, &msgLen);
					if ( msg->pDescription ) {
						logManager.Log(std::string("[D3D12] ") + msg->pDescription + "\n");
					}
				}
			}
#endif
		}

		// 失敗時は nullptr を返す（呼び出し元で判定して安全に処理）
		return nullptr;
	}

	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateCubemapResource(const DirectX::TexMetadata& metadata){
	// 1. キューブマップ専用の設定を行う
	D3D12_RESOURCE_DESC resourceDesc {};
	resourceDesc.Width = UINT(metadata.width);
	resourceDesc.Height = UINT(metadata.height);

	//  DDSから読み取ったミップマップ数をそのまま使う
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);

	//  ここが「6枚」の肝！
	// キューブマップの場合、metadata.arraySize は必ず 6 になっています。
	resourceDesc.DepthOrArraySize = 6;

	resourceDesc.Format = metadata.format;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // キューブマップも2Dテクスチャの配列
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	// 2. 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 3. Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr, IID_PPV_ARGS(&resource));

	if ( FAILED(hr) ) {
		logManager.Log("CreateCubemapResource Failed\n");
		return nullptr;
	}

	return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages, ID3D12GraphicsCommandList* commandList){
	// nullptr ガード
	if ( !texture ) {
		logManager.Log("UploadTextureData: destination texture is nullptr. Aborting upload.\n");
		return nullptr;
	}

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device_.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast< UINT >( subresources.size() ));
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = resourceBuffer_->CreateBufferResource(intermediateSize);

	intermediateResources_.push_back(intermediateResource);

	/*D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	commandList->ResourceBarrier(1, &barrier);*/

	UpdateSubresources(commandList, texture.Get(), intermediateResource.Get(), 0, 0, static_cast< UINT >( subresources.size() ), subresources.data());

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	// StateBefore を COPY_DEST に合わせる (作成時の状態と一致させる)
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;

	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> TextureManager::CreateDepthStencilTextureResource( int32_t width, int32_t height){
	
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc {};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue {};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource));

	if ( FAILED(hr) ) {
		logManager.Log(std::format("CreateDepthStencilTextureResource Failed. hr=0x{:08X} Width:{} Height:{} Format:{}\n",
			static_cast<unsigned>(hr), resourceDesc.Width, resourceDesc.Height, static_cast<int>(resourceDesc.Format)));
		if ( device_ ) {
			HRESULT removed = device_->GetDeviceRemovedReason();
			logManager.Log(std::format("GetDeviceRemovedReason: 0x{:08X}\n", static_cast<unsigned>(removed)));
#if defined(_DEBUG)
			Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
			if ( SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))) ) {
				UINT64 cnt = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
				for ( UINT64 i = 0; i < cnt; ++i ) {
					SIZE_T len = 0;
					infoQueue->GetMessage(i, nullptr, &len);
					std::vector<char> buf(len);
					auto msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
					infoQueue->GetMessage(i, msg, &len);
					if ( msg->pDescription ) logManager.Log(std::string("[D3D12] ") + msg->pDescription + "\n");
				}
			}
#endif
		}
		return nullptr;
	}

	return resource;
}
// SRVインデックスからテクスチャデータを取得
const TextureData& TextureManager::GetTextureDataBySrvIndex(uint32_t srvIndex){
	for ( auto& pair : textureDatas ) {
		if ( pair.second.srvIndex == srvIndex ) {
			return pair.second;
		}
	}
	// 見つからなかった場合のダミー
	static TextureData dummy {};
	return dummy;
}