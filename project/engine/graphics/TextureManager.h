#pragma once
// --- 標準・外部ライブラリ ---
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "externals/DirectXTex/d3dx12.h"
#include "externals/DirectXTex/DirectXTex.h"

// --- エンジン側のファイル ---

#include "engine/utils/LogManager.h"



// 前方宣言 
class DirectXCommon;
class SrvManager;
class ResourceFactory;


struct TextureData{
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint32_t srvIndex;
	float width;
	float height;

};

class TextureManager{

public:

	/// <summary>
	/// 初期化
	// </summary>
    void Initialize(Microsoft::WRL::ComPtr<ID3D12Device> device,DirectXCommon* dxCommon,SrvManager* srvManager);

   static  TextureManager* GetInstance(){
        static TextureManager instance;
        return &instance;
	}

   // 終了処理
   void Finalize();

    // -------------------- テクスチャ読み込み --------------------

       /// <summary>
       /// 画像ファイルを読み込み、ScratchImage 形式で返す
       /// </summary>
    DirectX::ScratchImage LoadTexture(const std::string& filePath);

	
    const TextureData& GetTextureDataBySrvIndex(uint32_t srvIndex);

    //   -------------------- SRV作成までをまとめた関数 -------------------- 

    TextureData LoadTextureAndCreateSRV(
        const std::string& filePath,
        ID3D12GraphicsCommandList* commandList
    );

    TextureData LoadTextureAndCreateSRVCube(
        const std::string& filePath,
        ID3D12GraphicsCommandList* commandList
    );

    // -------------------- 短縮版（commandList を内部取得） --------------------
    // 例: textures_["x"] = TextureManager::GetInstance()->Load("resources/x.png");
    // commandList を毎回渡す必要がなくなる（内部で DirectXCommon から取得）
    TextureData Load(const std::string& filePath);
    TextureData LoadCube(const std::string& filePath);


    // -------------------- GPU テクスチャリソース生成 --------------------

    /// <summary>
    /// TexMetadata をもとに、GPU テクスチャリソース(Resource)を生成
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata& metadata);


    // 既存の CreateTextureResource
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateCubemapResource(const DirectX::TexMetadata& metadata);

    // -------------------- テクスチャデータ転送 --------------------

    /// <summary>
    /// ScratchImage 内のピクセルデータを GPU テクスチャへアップロード
    /// </summary>
   // [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
        const  Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
        const DirectX::ScratchImage& mipImages,
        ID3D12GraphicsCommandList* commandList
    );


    // -------------------- 深度ステンシルテクスチャ生成 --------------------

    /// <summary>
    /// 深度ステンシル用の GPU テクスチャリソースを生成
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(int32_t width, int32_t height);


    // -------------------- デバイス・依存コンポーネント設定 --------------------




    /// <summary>
    /// DirectX12 Device を設定する
    /// </summary>
    void SetDevice(Microsoft::WRL::ComPtr<ID3D12Device> device){
        device_ = device;
    }

    /// <summary>
    /// 外部のリソースファクトリをセット
    /// </summary>
    void SetResourceFactory(ResourceFactory* resourceBuffer){
        resourceBuffer_ = resourceBuffer;
    }

  /*  /// ディスクリプタサイズを手動で設定する
    void SetDescriptorSize(UINT size){
        this->descriptorSizeSRV_ = size;
    }*/

private:

    // -------------------- 内部リソース --------------------

    Microsoft::WRL::ComPtr<ID3D12Device> device_ = nullptr;   // DX12 デバイス
    logs::LogManager logManager;                    // ログ出力用
    ResourceFactory* resourceBuffer_ = nullptr; // バッファ生成の補助クラス

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
   /* ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT descriptorSizeSRV_ = 0;
    UINT currentIndex_ = 1;*/



    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> textureMap;

    std::unordered_map<std::string, TextureData> textureDatas;

    std::vector< Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;

};

