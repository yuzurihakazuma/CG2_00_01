#pragma once

// --- 標準ライブラリ・外部ライブラリ ---
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <wrl/client.h>

// --- エンジン側のファイル ---
#include "engine/math/struct.h"


// 前方宣言
class DirectXCommon;
class SrvManager;


// レンダーテクスチャクラス
class RenderTexture{
public:

	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);

	// 描画先をこのテクスチャ（オフスクリーン）に切り替える
	void PreDrawScene(ID3D12GraphicsCommandList* commandList, DirectXCommon* dxCommon);
	// 描画先をメイン画面（スワップチェーン）に戻す
	void PostDrawScene(ID3D12GraphicsCommandList* commandList, DirectXCommon* dxCommon);


	// ウィンドウサイズが変わったときの処理（テクスチャのサイズも変える）
	void Resize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height); 


public: // ゲッター・セッター

	// 解放
	Microsoft::WRL::ComPtr<ID3D12Resource> GetResource() const{ return resource_; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const{ return rtvHandle_; }
	uint32_t GetSrvIndex() const{ return srvIndex_; }

	const Vector4& GetClearColor() const{ return clearColor_; }

	void SetClearColor(float r, float g, float b, float a){
		clearColor_.x = r;
		clearColor_.y = g;
		clearColor_.z = b;
		clearColor_.w = a;
	}



private:

	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_; // 書き込み用（RTV）
	uint32_t srvIndex_;                     // 読み込み用（SRV）

	Vector4 clearColor_ = { 0.04f, 0.03f, 0.05f, 1.0f }; // 洞窟の闇（暗紫黒）


};

