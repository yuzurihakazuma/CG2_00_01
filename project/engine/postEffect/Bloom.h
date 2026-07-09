#pragma once
#include "engine/graphics/RenderTexture.h"
#include <memory>
#include <d3d12.h>
#include <wrl.h>
#include <string>

class DirectXCommon;
class SrvManager;

// Bloom（光の溢れ）エフェクト専用クラス
class Bloom {
public:
	// シェーダーに送る設定データ
	struct BloomData {
		float threshold;  // 光らせる基準値
		float padding[3];
	};

	static Bloom* GetInstance() {
		static Bloom instance;
		return &instance;
	}


	// ブラー用の設定データ構造体
	struct BlurData {
		float texelSize[2]; // 1ピクセルのサイズ
		float direction[2]; // ぼかす方向
	};

	// ぼかし工程を描画する関数
	void DrawBlur(ID3D12GraphicsCommandList* commandList);

	// ぼかし終わった最終結果のSRVを取得
	uint32_t GetBlurSrvIndex() const { return blurTextures_[1]->GetSrvIndex(); }
	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);

	void OnResize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);

	// 終了処理
	void Finalize();
	// 高輝度抽出を描画する関数
	void Render(ID3D12GraphicsCommandList* commandList, uint32_t colorSrvIndex, uint32_t maskSrvIndex);
	// 高輝度抽出を描画する関数
	void DrawExtract(ID3D12GraphicsCommandList* commandList, uint32_t maskSrvIndex);

	
	// 抽出した画像（SRV）の番号を取得
	uint32_t GetExtractSrvIndex() const { return extractTexture_->GetSrvIndex(); }


	// 元の画像と光を合成する！
	void DrawCombine(ID3D12GraphicsCommandList* commandList, uint32_t mainSrvIndex);

	// すべての処理が終わった「最終結果」のSRVを取得
	uint32_t GetCombineSrvIndex() const { return combineTexture_->GetSrvIndex(); }

	// 合成結果のレンダーテクスチャ本体（SDF の文字/画像を焼き込む先として使う）
	RenderTexture* GetCombineTexture() const { return combineTexture_.get(); }

	void SetTargetEmissivePower(float* emissivePower){ targetEmissivePower_ = emissivePower; }

	void DrawDebugUI();
public:
	// ON/OFFスイッチと、結果のSRV番号
	bool IsEnabled() const { return isEnabled_; }
	void SetEnabled(bool enabled) { isEnabled_ = enabled; }
	uint32_t GetResultSrvIndex() const { return resultSrvIndex_; }

	// JSONへの保存と読み込み
	void Save(const std::string& filePath);
	void Load(const std::string& filePath);
private:
	Bloom() = default;
	~Bloom() = default;


	// ON/OFFを管理するフラグと、最終的に画面に映す画像の番号
	bool isEnabled_ = true;
	uint32_t resultSrvIndex_ = 0;


private:
	// 抽出した画像を保存するための専用キャンバス
	std::unique_ptr<RenderTexture> extractTexture_;

	Microsoft::WRL::ComPtr<ID3D12Resource> bloomDataResource_;
	BloomData* bloomData_ = nullptr;

	// パイプラインステートとルートシグネチャ
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;


	// ぼかし用のキャンバス [0]:横ぼかし用, [1]:縦ぼかし用
	std::unique_ptr<RenderTexture> blurTextures_[2];

	// ブラー用のパイプライン
	Microsoft::WRL::ComPtr<ID3D12RootSignature> blurRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPipelineState_;

	// ブラー用の定数バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> blurDataResource_;
	BlurData* blurData_ = nullptr;

	std::unique_ptr<RenderTexture> combineTexture_; // 最終結果を保存するキャンバス
	Microsoft::WRL::ComPtr<ID3D12RootSignature> combineRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> combinePipelineState_;

	float* targetEmissivePower_ = nullptr; // 初期値はNULL

};