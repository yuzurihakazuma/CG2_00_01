#include "PipelineManager.h"
// --- 標準ライブラリ ---
#include <cassert>

// --- エンジン側のファイル ---
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ShaderCompiler.h"
#include "engine/graphics/RootSignatureBuilder.h"
#include "engine/graphics/GraphicsPipelineBuilder.h"
#include "engine/postEffect/PostEffect.h"

// 終了処理
void PipelineManager::Finalize(){
	// スマートポインタをリセットして、GPUのメモリリークを防ぐ
	spriteRootSignature_.Reset();
	spritePipelineState_.Reset();
	// 3Dオブジェクト用のリソースも忘れずに解放
	object3DRootSignature_.Reset();
	object3DPipelineState_.Reset();
	object3DPipelineStateNone_.Reset();

	// インスタンシング専用のリソースも忘れずに解放
	instancedObject3DRootSignature_.Reset();
	instancedObject3DPipelineState_.Reset();
	// パーティクル用のリソースも忘れずに解放
	particleRootSignature_.Reset();
	particlePipelineState_.Reset();
	// ポストエフェクト用のリソースも忘れずに解放
	postEffectPipelineState_.Reset();
	postEffectRootSignature_.Reset();
	// スキニングメッシュ用のリソースも忘れずに解放
	skinningObject3DRootSignature_.Reset();
	skinningObject3DPipelineState_.Reset();
	// GPUパーティクル用のリソースも忘れずに解放
	gpuParticleComputeRootSignature_.Reset();
	gpuParticleComputePipelineState_.Reset();
	gpuParticleEmitRootSignature_.Reset();
	gpuParticleEmitPipelineState_.Reset();
	gpuParticleDrawRootSignature_.Reset();
	gpuParticleDrawPipelineState_.Reset();

	skyboxRootSignature_.Reset();
	skyboxPipelineState_.Reset();

	object3DPipelineStateAdditive_.Reset();

	// ポストエフェクトの種類ごとのパイプラインステートも忘れずに解放
	for ( int i = 0; i < 14; ++i ) {
		if ( postEffectPipelineStates_[i] != nullptr ) {
			postEffectPipelineStates_[i].Reset();
		}
	}

}

// 初期化
void PipelineManager::Initialize(DirectXCommon* dxCommon){

	assert(dxCommon);
	assert(dxCommon->GetDevice());
	assert(dxCommon->GetSrvManager() != nullptr);
	this->dxCommon_ = dxCommon;



	// スプライト用ルートシグネチャの作成
	CreateSpriteRootSignature();
	// スプライト用グラフィックスパイプラインの作成
	CreateSpriteGraphicsPipeline();

	// 3Dオブジェクト用ルートシグネチャの作成
	CreateObject3DRootSignature();
	// 3Dオブジェクト用グラフィックスパイプラインの作成
	CreateObject3DGraphicsPipeline();

	// インスタンシング専用のルートシグネチャの作成
	CreateInstancedObject3DRootSignature();
	// インスタンシング専用のグラフィックスパイプラインの作成
	CreateInstancedObject3DGraphicsPipeline();

	// パーティクル用ルートシグネチャの作成
	CreateParticleRootSignature();
	// パーティクル用グラフィックスパイプラインの作成
	CreateParticleGraphicsPipeline();
	// ポストエフェクト用グラフィックスパイプラインの作成
	CreatePostEffectRootSignature();
	// ポストエフェクト用グラフィックスパイプラインの作成
	CreatePostEffectPipeline();

	// スキニングメッシュ用ルートシグネチャの作成
	CreateSkinningObject3DRootSignature();
	// スキニングメッシュ用グラフィックスパイプラインの作成
	CreateSkinningObject3DGraphicsPipeline();

	// GPUパーティクル用のルートシグネチャとパイプラインの作成
	CreateGPUParticleComputeRootSignature();
	CreateGPUParticleComputePipeline();
	CreateGPUParticleEmitRootSignature();
	CreateGPUParticleEmitPipeline();

	// Draw用のルートシグネチャとパイプラインの作成
	CreateGPUParticleDrawRootSignature();
	CreateGPUParticleDrawGraphicsPipeline();

	// スカイボックス用のルートシグネチャとパイプラインの作成
	CreateSkyboxRootSignature();
	CreateSkyboxGraphicsPipeline();

}

void PipelineManager::SetPipeline(
	ID3D12GraphicsCommandList* commandList,
	PipelineType type
){
	assert(commandList);

	switch ( type ) {
	case PipelineType::Sprite:
		commandList->SetGraphicsRootSignature(spriteRootSignature_.Get());
		commandList->SetPipelineState(spritePipelineState_.Get());
		commandList->IASetPrimitiveTopology(
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
		);
		break;
	case PipelineType::Object3D: // カリングあり
		commandList->SetGraphicsRootSignature(object3DRootSignature_.Get());
		commandList->SetPipelineState(object3DPipelineState_.Get());
		break;
	case PipelineType::Object3D_CullNone: // カリングなし
		commandList->SetGraphicsRootSignature(object3DRootSignature_.Get());
		commandList->SetPipelineState(object3DPipelineStateNone_.Get());
		break;
	case PipelineType::InstancedObject3D: // インスタンシング専用
		commandList->SetGraphicsRootSignature(instancedObject3DRootSignature_.Get());
		commandList->SetPipelineState(instancedObject3DPipelineState_.Get());
		break;
	case PipelineType::Particle: // パーティクル用
		commandList->SetGraphicsRootSignature(particleRootSignature_.Get());
		commandList->SetPipelineState(particlePipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;
	case PipelineType::PostEffect: // ポストエフェクト用
		commandList->SetGraphicsRootSignature(postEffectRootSignature_.Get());
		commandList->SetPipelineState(postEffectPipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;
	case PipelineType::SkinningObject3D:
		commandList->SetGraphicsRootSignature(skinningObject3DRootSignature_.Get());
		commandList->SetPipelineState(skinningObject3DPipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;
	case PipelineType::Skybox:
		commandList->SetGraphicsRootSignature(skyboxRootSignature_.Get());
		commandList->SetPipelineState(skyboxPipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;
	case PipelineType::Object3D_Additive:
		commandList->SetGraphicsRootSignature(object3DRootSignature_.Get());
		commandList->SetPipelineState(object3DPipelineStateAdditive_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		break;

		//s
	}
}
// ルートシグネチャの生成 Sprite用
void PipelineManager::CreateSpriteRootSignature(){

	RootSignatureBuilder builder;
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                // [0]: マテリアル (b0)
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);               // [1]: 座標 (b0)
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                // [3]: ライト1 (b1)
	builder.AddCBV(2, D3D12_SHADER_VISIBILITY_PIXEL);                // [4]: ライト2 (b2)
	builder.AddDefaultSampler(0);                                    // サンプラー (s0)

	// 構築して spriteRootSignature_ に入れる！
	builder.Build(dxCommon_->GetDevice(), spriteRootSignature_);


}
// グラフィックスパイプラインの生成 Sprite用
void PipelineManager::CreateSpriteGraphicsPipeline(){

	CreateGraphicsPipelineCommon(
		L"resources/shaders/Sprite/Sprite2d.VS.hlsl",
		L"resources/shaders/Sprite/Sprite2d.PS.hlsl",
		spriteRootSignature_.Get(),
		BlendMode::kNormal,         // 通常ブレンド
		D3D12_CULL_MODE_NONE,       // カリングなし
		false,                      // 深度書き込みしない
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
		spritePipelineState_
	);



}
// ノードエディタ等が生成したPSでObject3D互換PSOを作る
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineManager::CreateCustomObject3DPipeline(const std::wstring& psPath){
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	CreateGraphicsPipelineCommon(
		L"resources/shaders/Object3d/Object3d.VS.hlsl",
		psPath,
		object3DRootSignature_.Get(),
		BlendMode::kNormal,
		D3D12_CULL_MODE_BACK,
		true,
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
		pso
	);
	return pso;
}

// ルートシグネチャの生成 Object3D用
void PipelineManager::CreateObject3DRootSignature(){

	RootSignatureBuilder builder;
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                // [0]: マテリアル (b0)
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);               // [1]: 座標 (b0)
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                // [3]: ライト1 (b1)
	builder.AddCBV(2, D3D12_SHADER_VISIBILITY_PIXEL);                // [4]: カメラ (b2)
	builder.AddCBV(3, D3D12_SHADER_VISIBILITY_PIXEL);                // [5]: ポイントライト (b3)
	builder.AddCBV(4, D3D12_SHADER_VISIBILITY_PIXEL);                // [6]: スポットライト (b4)

	// ディゾルブ用のリソースを追加
	builder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [7]: ノイズ画像 (t1)
	builder.AddCBV(5, D3D12_SHADER_VISIBILITY_PIXEL);                // [8]: ディゾルブ進行度 (b5)

	builder.AddDescriptorTableSRV(3, D3D12_SHADER_VISIBILITY_PIXEL); // [9]: 環境マップ (t2)

	builder.AddDefaultSampler(0);                                    // サンプラー (s0)
	// 構築して object3DRootSignature_ に入れる！
	builder.Build(dxCommon_->GetDevice(), object3DRootSignature_);

}
// グラフィックスパイプラインの生成 Object3D用
void PipelineManager::CreateObject3DGraphicsPipeline(){
	// カリングあり用
	CreateGraphicsPipelineCommon(
		L"resources/shaders/Object3d/Object3d.VS.hlsl",
		L"resources/shaders/Object3d/Object3d.PS.hlsl",
		object3DRootSignature_.Get(),
		BlendMode::kNormal,         // 通常ブレンド
		D3D12_CULL_MODE_BACK,
		true,
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }, // RTVフォーマットを指定
		object3DPipelineState_
	);
	// カリングなし用
	CreateGraphicsPipelineCommon(
		L"resources/shaders/Object3d/Object3d.VS.hlsl",
		L"resources/shaders/Object3d/Object3d.PS.hlsl",
		object3DRootSignature_.Get(),
		BlendMode::kNormal,
		D3D12_CULL_MODE_NONE,
		true,
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }, // RTVフォーマットを指定
		object3DPipelineStateNone_
	);
	// 加算合成用
	CreateGraphicsPipelineCommon(
		L"resources/shaders/Object3d/Object3d.VS.hlsl",
		L"resources/shaders/Object3d/Object3d.PS.hlsl",
		object3DRootSignature_.Get(),
		BlendMode::kAdd,            // 加算合成（光る！）
		D3D12_CULL_MODE_NONE,       // カリングなし（裏面も描画）
		false,                      // 深度書き込みしない（フチが黒くならない！）
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
		object3DPipelineStateAdditive_
	);

}
// ルートシグネチャの生成 インスタンシング専用
void PipelineManager::CreateInstancedObject3DRootSignature(){


	RootSignatureBuilder builder;

	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                // [0]: マテリアル (b0)

	builder.AddSRV(0, D3D12_SHADER_VISIBILITY_VERTEX);               // [1]: 座標の配列 (t0)

	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                // [3]: 平行光源 (b1)
	builder.AddCBV(2, D3D12_SHADER_VISIBILITY_PIXEL);                // [4]: カメラ (b2)
	builder.AddCBV(3, D3D12_SHADER_VISIBILITY_PIXEL);                // [5]: 点光源 (b3)
	builder.AddCBV(4, D3D12_SHADER_VISIBILITY_PIXEL);                // [6]: スポットライト (b4)

	// ディゾルブ用
	builder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [7]: ノイズ画像 (t1)
	builder.AddCBV(5, D3D12_SHADER_VISIBILITY_PIXEL);                // [8]: ディゾルブ進行度 (b5)

	builder.AddDescriptorTableSRV(3, D3D12_SHADER_VISIBILITY_PIXEL); // [9]: 環境マップ (t3)

	builder.AddDefaultSampler(0);                                    // サンプラー (s0)
	builder.Build(dxCommon_->GetDevice(), instancedObject3DRootSignature_);


}

// グラフィックスパイプラインの生成 インスタンシング専用
void PipelineManager::CreateInstancedObject3DGraphicsPipeline(){
	auto vsBlob = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Object3d/InstancedObject.VS.hlsl", L"vs_6_0");
	auto psBlob = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Object3d/Object3d.PS.hlsl", L"ps_6_0");

	// 頂点レイアウト (Object3Dと同じ)
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	GraphicsPipelineBuilder builder;
	builder.SetRootSignature(instancedObject3DRootSignature_.Get())
		.SetShaders(vsBlob.Get(), psBlob.Get())
		.SetInputLayout(inputElementDescs, _countof(inputElementDescs))
		.SetBlendMode(BlendMode::kNormal)
		.SetCullMode(D3D12_CULL_MODE_BACK)
		.SetDepthStencil(true, true) // 深度書き込みON
		.SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }); // RTVフォーマットを指定

	builder.Build(dxCommon_->GetDevice(), instancedObject3DPipelineState_);


}



// ルートシグネチャの生成 Particle用
void PipelineManager::CreateParticleRootSignature(){

	RootSignatureBuilder builder;
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                 // [0]: マテリアル (b0)

	// パーティクルはインスタンシングで描画するため、座標用のCBVは用意せず、SRVでテクスチャバッファを渡す
	builder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_VERTEX);

	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL);  // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                 // [3]: ライト1 (b1)
	builder.AddCBV(2, D3D12_SHADER_VISIBILITY_PIXEL);                 // [4]: ライト2 (b2)
	builder.AddDefaultSampler(0);                                     // サンプラー (s0)

	// 構築して particleRootSignature_ に入れる！
	builder.Build(dxCommon_->GetDevice(), particleRootSignature_);

}
// グラフィックスパイプラインの生成 Particle用
void PipelineManager::CreateParticleGraphicsPipeline(){


	CreateGraphicsPipelineCommon(
		L"resources/shaders/Particle/Particle.VS.hlsl",
		L"resources/shaders/Particle/Particle.PS.hlsl",
		particleRootSignature_.Get(),
		BlendMode::kAdd,            // 加算ブレンド
		D3D12_CULL_MODE_NONE,       // カリングなし
		false,                      // 深度書き込みしない
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }, // RTVフォーマットを指定
		particlePipelineState_
	);



}

// ルートシグネチャの生成 PostEffect用
void PipelineManager::CreatePostEffectRootSignature(){

	RootSignatureBuilder builder;

	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL);  // [0]: テクスチャ (t0)
	builder.AddDefaultSampler(0);                                     // サンプラー (s0)
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [1]: エフェクト共通定数バッファ (b0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [2]: マスク系エフェクト用定数バッファ (b1)

	// 構築して postEffectRootSignature_ に入れる！
	builder.Build(dxCommon_->GetDevice(), postEffectRootSignature_);

}

// グラフィックスパイプラインの生成 PostEffect用
void PipelineManager::CreatePostEffectPipeline(){

	// 頂点シェーダーは全エフェクトで共通
	auto vsBlob = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/PostEffect/Fullscreen.VS.hlsl", L"vs_6_0");

	// ピクセルシェーダーのパスを配列にしておく（PostEffectType の順番と合わせる）
	const std::wstring psPaths[] = {
		L"resources/shaders/PostEffect/Fullscreen.PS.hlsl",       // 0: None (そのまま表示)
		L"resources/shaders/PostEffect/Grayscale.PS.hlsl",      // 1: Grayscale
		L"resources/shaders/PostEffect/Sepia.PS.hlsl",          // 2: Sepia
		L"resources/shaders/PostEffect/Vignetting.PS.hlsl",     // 3: Vignetting
		L"resources/shaders/PostEffect/BoxFilter.PS.hlsl",      // 4: BoxFilter
		L"resources/shaders/PostEffect/BoxFilter5x5.PS.hlsl",   // 5: BoxFilter5x5
		L"resources/shaders/PostEffect/GaussianFilter.PS.hlsl" , // 6: GaussianFilter
		L"resources/shaders/PostEffect/RadialBlur.PS.hlsl", // 7: RadialBlur
		L"resources/shaders/PostEffect/LuminanceBasedOutline.PS.hlsl", // 8: LuminanceBasedOutline
		L"resources/shaders/PostEffect/RandomNoise.PS.hlsl", // 9: RandomNoise
		L"resources/shaders/PostEffect/ColorTint.PS.hlsl",        // 10: ColorTint
		L"resources/shaders/PostEffect/MaskedDistortion.PS.hlsl", // 11: MaskedDistortion
		L"resources/shaders/PostEffect/MaskedGlow.PS.hlsl",       // 12: MaskedGlow
		L"resources/shaders/PostEffect/MaskedSepia.PS.hlsl",      // 13: MaskedSepia
	};

	// パイプラインの共通設定（ブレンドやカリングなど）
	GraphicsPipelineBuilder builder;
	builder.SetRootSignature(postEffectRootSignature_.Get())
		.SetInputLayoutEmpty()
		.SetCullMode(D3D12_CULL_MODE_NONE)
		.SetDepthStencil(false)
		.SetBlendMode(BlendMode::kNormal);

	// for文で7個のシェーダーを一気にコンパイルして配列に保存！
	for ( int i = 0; i < 14; ++i ) {
		auto psBlob = dxCommon_->GetShaderCompiler().CompileShader(psPaths[i], L"ps_6_0");
		builder.SetShaders(vsBlob.Get(), psBlob.Get());
		builder.Build(dxCommon_->GetDevice(), postEffectPipelineStates_[i]);
	}
}


// 
// ルートシグネチャの生成 スキニングメッシュ用
void PipelineManager::CreateSkinningObject3DRootSignature(){
	RootSignatureBuilder builder;
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                // [0]: マテリアル (b0)
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);               // [1]: 座標変換行列 (b0)
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL); // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                // [3]: 平行光源 (b1)
	builder.AddCBV(2, D3D12_SHADER_VISIBILITY_PIXEL);                // [4]: カメラ (b2)
	builder.AddCBV(3, D3D12_SHADER_VISIBILITY_PIXEL);                // [5]: 点光源 (b3)
	builder.AddCBV(4, D3D12_SHADER_VISIBILITY_PIXEL);                // [6]: スポットライト (b4)
	builder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_PIXEL); // [7]: ノイズ画像 (t1)
	builder.AddCBV(5, D3D12_SHADER_VISIBILITY_PIXEL);                // [8]: ディゾルブ (b5)
	builder.AddDescriptorTableSRV(2, D3D12_SHADER_VISIBILITY_VERTEX);// [9]: MatrixPalette (t2) ← 追加
	builder.AddDescriptorTableSRV(3, D3D12_SHADER_VISIBILITY_PIXEL); // [10]: 環境マップ (t3)

	builder.AddDefaultSampler(0);
	builder.Build(dxCommon_->GetDevice(), skinningObject3DRootSignature_);
}

// グラフィックスパイプラインの生成 スキニングメッシュ用
void PipelineManager::CreateSkinningObject3DGraphicsPipeline(){
	auto vsBlob = dxCommon_->GetShaderCompiler().CompileShader(
		L"resources/shaders/Object3d/SkinningObject3d.VS.hlsl", L"vs_6_0");
	auto psBlob = dxCommon_->GetShaderCompiler().CompileShader(
		L"resources/shaders/Object3d/Object3d.PS.hlsl", L"ps_6_0");

	//  スキニング用に WEIGHT と INDEX を追加した入力レイアウト
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,        0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,     0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "WEIGHT",   0, DXGI_FORMAT_R32G32B32A32_FLOAT,  0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "INDEX",    0, DXGI_FORMAT_R32G32B32A32_SINT,   0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	GraphicsPipelineBuilder builder;
	builder.SetRootSignature(skinningObject3DRootSignature_.Get())
		.SetShaders(vsBlob.Get(), psBlob.Get())
		.SetInputLayout(inputElementDescs, _countof(inputElementDescs))
		.SetBlendMode(BlendMode::kNormal)
		.SetCullMode(D3D12_CULL_MODE_NONE)
		.SetDepthStencil(true, true)
		.SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB });

	builder.Build(dxCommon_->GetDevice(), skinningObject3DPipelineState_);
}

// ルートシグネチャの生成 GPUパーティクル用 Computeシェーダー用
void PipelineManager::CreateGPUParticleComputeRootSignature(){
	RootSignatureBuilder builder;
	builder.AddDescriptorTableUAV(0);                          // [0]: u0 パーティクル (RWStructuredBuffer)
	builder.AddDescriptorTableUAV(1);                          // [1]: u1 FreeList（空きスロット番号）
	builder.AddDescriptorTableUAV(2);                          // [2]: u2 FreeListIndex（空き数カウンタ）
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_ALL);            // [3]: b0 (deltaTime等)
	builder.BuildForCompute(dxCommon_->GetDevice(), gpuParticleComputeRootSignature_);
}

// グラフィックスパイプラインの生成 GPUパーティクル用 Computeシェーダー用
void PipelineManager::CreateGPUParticleComputePipeline(){
	auto csBlob = dxCommon_->GetShaderCompiler().CompileShader(
		L"resources/shaders/Particle/ParticleUpdate.CS.hlsl", L"cs_6_0");
	assert(csBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = gpuParticleComputeRootSignature_.Get();
	psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

	HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&psoDesc, IID_PPV_ARGS(&gpuParticleComputePipelineState_));
	assert(SUCCEEDED(hr));
}

// ルートシグネチャの生成 GPUパーティクル用 Emit（発生）Computeシェーダー用
void PipelineManager::CreateGPUParticleEmitRootSignature(){
	RootSignatureBuilder builder;
	builder.AddDescriptorTableUAV(0);                                 // [0]: u0 パーティクル
	builder.AddDescriptorTableUAV(1);                                 // [1]: u1 FreeList
	builder.AddDescriptorTableUAV(2);                                 // [2]: u2 FreeListIndex
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_ALL);    // [3]: t0 発生リクエスト
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_ALL);                   // [4]: b0 (emitCount)
	builder.BuildForCompute(dxCommon_->GetDevice(), gpuParticleEmitRootSignature_);
}

// パイプラインの生成 GPUパーティクル用 Emit（発生）Computeシェーダー用
void PipelineManager::CreateGPUParticleEmitPipeline(){
	auto csBlob = dxCommon_->GetShaderCompiler().CompileShader(
		L"resources/shaders/Particle/ParticleEmit.CS.hlsl", L"cs_6_0");
	assert(csBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = gpuParticleEmitRootSignature_.Get();
	psoDesc.CS = { csBlob->GetBufferPointer(), csBlob->GetBufferSize() };

	HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&psoDesc, IID_PPV_ARGS(&gpuParticleEmitPipelineState_));
	assert(SUCCEEDED(hr));
}

// ルートシグネチャの生成 GPUパーティクル用 Drawシェーダー用
void PipelineManager::CreateGPUParticleDrawRootSignature(){
	RootSignatureBuilder builder;
	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);                 // [0]: マテリアル (b0)
	builder.AddDescriptorTableSRV(1, D3D12_SHADER_VISIBILITY_VERTEX); // [1]: パーティクルデータ (t1)
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL);  // [2]: テクスチャ (t0)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_PIXEL);                 // [3]: ライト (b1)
	builder.AddCBV(1, D3D12_SHADER_VISIBILITY_VERTEX);                // [4]: カメラ行列 (b1 VERTEX)
	builder.AddDefaultSampler(0);
	builder.Build(dxCommon_->GetDevice(), gpuParticleDrawRootSignature_);
}

// グラフィックスパイプラインの生成 GPUパーティクル用 Drawシェーダー用
void PipelineManager::CreateGPUParticleDrawGraphicsPipeline(){
	CreateGraphicsPipelineCommon(
		L"resources/shaders/Particle/GPUParticle.VS.hlsl",
		L"resources/shaders/Particle/Particle.PS.hlsl",
		gpuParticleDrawRootSignature_.Get(),
		BlendMode::kAdd,
		D3D12_CULL_MODE_NONE,
		false,
		{ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
		gpuParticleDrawPipelineState_
	);
}

// ルートシグネチャの生成 スカイボックス用
void PipelineManager::CreateSkyboxRootSignature(){

	RootSignatureBuilder builder;

	builder.AddCBV(0, D3D12_SHADER_VISIBILITY_VERTEX);                // [0]: 座標変換行列 (b0)
	builder.AddDescriptorTableSRV(0, D3D12_SHADER_VISIBILITY_PIXEL);  // [0]: テクスチャ (t0)
	builder.AddDefaultSampler(0); // サンプラー (s0)

	builder.Build(dxCommon_->GetDevice(), skyboxRootSignature_); // 構築して skyboxRootSignature_ に入れる！




}

// グラフィックスパイプラインの生成 スカイボックス用
void PipelineManager::CreateSkyboxGraphicsPipeline(){

	auto vsBlob = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Skybox/Skybox.VS.hlsl", L"vs_6_0");
	auto psBlob = dxCommon_->GetShaderCompiler().CompileShader(L"resources/shaders/Skybox/Skybox.PS.hlsl", L"ps_6_0");

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	GraphicsPipelineBuilder builder;
	builder.SetRootSignature(skyboxRootSignature_.Get())
		.SetShaders(vsBlob.Get(), psBlob.Get())
		.SetInputLayout(inputElementDescs, _countof(inputElementDescs))
		.SetBlendMode(BlendMode::kNormal)
		.SetCullMode(D3D12_CULL_MODE_NONE) // カリングなし
		.SetDepthStencil(true, false)      // 深度テストON、深度書き込みOFF (DepthFuncはビルダーの初期値 LESS_EQUAL が効く)
		.SetRenderTargets({ DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB }); // ※Object3D等と同じRTVフォーマットに合わせる

	builder.Build(dxCommon_->GetDevice(), skyboxPipelineState_);

}

// GPUパーティクル用のComputeシェーダーパイプラインをコマンドリストにセットする関数
void PipelineManager::SetGPUParticleComputePipeline(ID3D12GraphicsCommandList* commandList){
	commandList->SetComputeRootSignature(gpuParticleComputeRootSignature_.Get());
	commandList->SetPipelineState(gpuParticleComputePipelineState_.Get());
}

// GPUパーティクルのEmit（発生）用Computeパイプラインをコマンドリストにセットする関数
void PipelineManager::SetGPUParticleEmitPipeline(ID3D12GraphicsCommandList* commandList){
	commandList->SetComputeRootSignature(gpuParticleEmitRootSignature_.Get());
	commandList->SetPipelineState(gpuParticleEmitPipelineState_.Get());
}

// GPUパーティクル用のDrawシェーダーパイプラインをコマンドリストにセットする関数
void PipelineManager::SetGPUParticleDrawPipeline(ID3D12GraphicsCommandList* commandList){
	commandList->SetGraphicsRootSignature(gpuParticleDrawRootSignature_.Get());
	commandList->SetPipelineState(gpuParticleDrawPipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}


// グラフィックスパイプラインの共通生成
void PipelineManager::CreateGraphicsPipelineCommon(
	const std::wstring& vsPath, // 頂点シェーダーのパス
	const std::wstring& psPath, // ピクセルシェーダーのパス
	ID3D12RootSignature* rootSig, 		 // ルートシグネチャ
	BlendMode blendMode, 					// ブレンドモード
	D3D12_CULL_MODE cullMode, 				// カリングモード
	bool isDepthWrite, 					// 深度書き込みの有無
	const std::vector<DXGI_FORMAT>& rtvFormats,
	Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState// 生成結果
){
	// 1. シェーダーコンパイル
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
		dxCommon_->GetShaderCompiler().CompileShader(vsPath, L"vs_6_0");
	assert(vertexShaderBlob != nullptr);
	// ピクセルシェーダー
	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
		dxCommon_->GetShaderCompiler().CompileShader(psPath, L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// 2. InputLayout 
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};// 頂点レイアウト
	inputElementDescs[0] = { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }; // xyz座標(4要素)
	inputElementDescs[1] = { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }; // uv座標(2要素)
	inputElementDescs[2] = { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }; // 法線ベクトル(3要素)
	// InputLayout設定
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc {}; // 入力レイアウト
	inputLayoutDesc.pInputElementDescs = inputElementDescs; // 頂点レイアウト配列へのポインタ
	inputLayoutDesc.NumElements = _countof(inputElementDescs); // 配列の長さ

	// 3. BlendState (引数から取得)
	D3D12_BLEND_DESC blendDesc = GetBlendDesc(blendMode);

	// 4. RasterizerState (引数を使用)
	D3D12_RASTERIZER_DESC rasterizerDesc {}; // ラスタライザステート
	rasterizerDesc.CullMode = cullMode; // カリングモード
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID; // 塗りつぶし

	// 5. DepthStencilState (引数を使用)
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc {}; // デプスステンシルステート
	depthStencilDesc.DepthEnable = true; // 深度テスト有効
	// trueならALL(書き込む)、falseならZERO(書き込まない)
	depthStencilDesc.DepthWriteMask = isDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;// 深度書き込みの有無
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;// 深度比較関数
	depthStencilDesc.StencilEnable = false; // ステンシルテスト無効

	// 6. PSO生成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc {};// グラフィックスパイプラインステート設定
	psoDesc.pRootSignature = rootSig;// ルートシグネチャ
	psoDesc.InputLayout = inputLayoutDesc; // 入力レイアウト
	psoDesc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() }; // 頂点シェーダー
	psoDesc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() }; // ピクセルシェーダー
	psoDesc.BlendState = blendDesc; // ブレンドステート
	psoDesc.RasterizerState = rasterizerDesc; // ラスタライザステート
	psoDesc.NumRenderTargets = static_cast< UINT >( rtvFormats.size() ); // 配列の数にする
	for ( UINT i = 0; i < rtvFormats.size(); ++i ) {
		psoDesc.RTVFormats[i] = rtvFormats[i]; // 配列の中身を入れる
	}
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // プリミティブ形状
	psoDesc.SampleDesc.Count = 1; // サンプリング数
	psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK; // サンプルマスク
	psoDesc.DepthStencilState = depthStencilDesc; // デプスステンシルステート
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // DSVのフォーマット

	// 結果を保存
	pipelineState = nullptr;
	HRESULT hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)); // PSO生成
	assert(SUCCEEDED(hr));
}

// ブレンドステートの共通関数
void PipelineManager::SetPostEffectPipeline(ID3D12GraphicsCommandList* commandList, PostEffectType effectType){
	// ルートシグネチャをセット
	commandList->SetGraphicsRootSignature(postEffectRootSignature_.Get());

	// enumの番号（0～6）を使って、対応するパイプラインをセット
	int index = static_cast< int >( effectType );

	if ( postEffectPipelineStates_[index] != nullptr ) {
		commandList->SetPipelineState(postEffectPipelineStates_[index].Get());
	}
}