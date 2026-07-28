#pragma once
// --- 標準・外部ライブラリ ---
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <vector>

// --- エンジン側のファイル ---
#include "engine/graphics/PipelineType.h"
#include "engine/graphics/BlendMode.h"

class DirectXCommon;

enum class PostEffectType;

// パイプラインマネージャー
class PipelineManager{
public:
	static PipelineManager* GetInstance(){
		static PipelineManager instance;
		return &instance;
	}

	void Finalize();

	void Initialize(DirectXCommon* dxCommon);

	void SetPipeline(
		ID3D12GraphicsCommandList* commandList,
		PipelineType type
	);

	// 汎用グラフィックスパイプライン生成
	void SetPostEffectPipeline(ID3D12GraphicsCommandList* commandList, PostEffectType effectType);

	// ノードエディタ等が生成したピクセルシェーダーで Object3D 互換 PSO を作る
	// （VS/ルートシグネチャ/フォーマットは通常の Object3D と同じ）
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCustomObject3DPipeline(const std::wstring& psPath);


	// Computeパイプラインをセット
	void SetGPUParticleComputePipeline(ID3D12GraphicsCommandList* commandList);

	// GPUパーティクルのEmit（発生）用Computeパイプラインをセット（FreeListから空きを取り出して書き込む）
	void SetGPUParticleEmitPipeline(ID3D12GraphicsCommandList* commandList);
	// Draw用パイプラインをセット
	void SetGPUParticleDrawPipeline(ID3D12GraphicsCommandList* commandList);


private:
	PipelineManager() = default;

	// Sprite用の初期化
	void CreateSpriteRootSignature();
	void CreateSpriteGraphicsPipeline();
	// OBJ用の初期化
	void CreateObject3DRootSignature();
	void CreateObject3DGraphicsPipeline();

	// インスタンシング専用の初期化関数
	void CreateInstancedObject3DRootSignature();
	void CreateInstancedObject3DGraphicsPipeline();

	// パーティクル用の初期化
	void CreateParticleRootSignature();
	void CreateParticleGraphicsPipeline();
	// ポストエフェクト用の初期化
	void CreatePostEffectRootSignature();
	void CreatePostEffectPipeline();
	
	// スキニングアニメーション用変数
	void CreateSkinningObject3DRootSignature();
	void CreateSkinningObject3DGraphicsPipeline();

	// GPUパーティクル用の初期化関数
	void CreateGPUParticleComputeRootSignature();
	void CreateGPUParticleEmitRootSignature();
	void CreateGPUParticleEmitPipeline();
	
	// GPUパーティクル用の初期化関数（Computeシェーダー用）
	void CreateGPUParticleComputePipeline();
	void CreateGPUParticleDrawRootSignature();
	void CreateGPUParticleDrawGraphicsPipeline();

	// スカイボックス用の初期化関数
	void CreateSkyboxRootSignature();
	void CreateSkyboxGraphicsPipeline();


	// 汎用グラフィックスパイプライン生成
	// 引数で「違い」を受け取ることで、あらゆるパイプラインに対応します
	void CreateGraphicsPipelineCommon(
		const std::wstring& vsPath,   // VS(頂点シェーダー)のファイルパス
		const std::wstring& psPath,   // PS(ピクセルシェーダー)のファイルパス
		ID3D12RootSignature* rootSig, // 使用するルートシグネチャ
		BlendMode blendMode,          // ブレンドモード (Normal, Addなど)
		D3D12_CULL_MODE cullMode,     // カリングモード (None, Backなど)
		bool isDepthWrite,            // 深度を書き込むか (Spriteはfalse, 3Dはtrue)
		const std::vector<DXGI_FORMAT>& rtvFormats,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState // 結果を入れる変数
	);



private:
	DirectXCommon* dxCommon_ = nullptr;

	// スプライト用変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> spriteRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> spritePipelineState_;


	// 3Dオブジェクト用変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> object3DRootSignature_; // ルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12PipelineState> object3DPipelineState_; // カリングあり用
	Microsoft::WRL::ComPtr<ID3D12PipelineState> object3DPipelineStateNone_; // カリングなし用

	// インスタンシング専用の変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> instancedObject3DRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> instancedObject3DPipelineState_;

	// パーティクル用変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> particleRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> particlePipelineState_;

	// ポストエフェクト用変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> postEffectRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> postEffectPipelineState_;

	// スキニングアニメーション用変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningObject3DRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningObject3DPipelineState_;

	// ポストエフェクトの種類ごとのパイプラインステートを格納する配列
	Microsoft::WRL::ComPtr<ID3D12PipelineState> postEffectPipelineStates_[14];

	// GPUパーティクルのComputeシェーダー用の変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleComputeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  gpuParticleComputePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleEmitRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  gpuParticleEmitPipelineState_;

	// GPUパーティクルのDrawシェーダー用の変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> gpuParticleDrawRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState>  gpuParticleDrawPipelineState_;

	// スカイボックス用の変数
	Microsoft::WRL::ComPtr<ID3D12RootSignature> skyboxRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> skyboxPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12PipelineState> object3DPipelineStateAdditive_; // 加算合成エフェクト用

};

