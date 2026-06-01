#pragma once

// --- 標準ライブラリ・外部ライブラリ ---
#include <memory>
#include <d3d12.h>
#include <string>
// --- エンジン側のファイル ---
#include "engine/graphics/RenderTexture.h"

class DirectXCommon;
class SrvManager;

enum class PostEffectType {
	None,           // エフェクトなし（そのまま表示）
	Grayscale,      // グレースケール
	Sepia,          // セピア
	Vignetting,     // ビネット（周辺減光）
	BoxFilter,      // ボックスフィルター（ぼかし）
	BoxFilter5x5,   // ボックスフィルター 5x5（強めのぼかし）
	GaussianFilter, // ガウシアンフィルター（綺麗なぼかし）
	RadialBlur,     // 放射状ブラー
	Outline,        // アウトライン
	RandomNoise,    // ランダムノイズ
	Count           // 種類の数
};


class PostEffect {
public:

	static PostEffect* GetInstance() {
		static PostEffect instance;
		return &instance;
	}

	~PostEffect() = default;

	// 初期化
	void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width, uint32_t height);
	
	// 更新
	void Update();

	// 終了処理
	void Finalize();

	// お絵かき開始（画用紙への切り替え）
	void PreDrawScene(ID3D12GraphicsCommandList* commandList);

	// お絵かき終了（メイン画面への切り替え）
	void PostDrawScene(ID3D12GraphicsCommandList* commandList);

	// ポストエフェクトの描画（巨大な三角形を描く）
	// drawToScreen=false にするとエフェクト処理だけ行い、バックバッファへの最終描画はスキップする
	void Draw(ID3D12GraphicsCommandList* commandList, bool drawToScreen = true);

	// 最終結果をバックバッファに書き出す（RTVのリセット込み）
	// editorActive=true のときは RTV だけセットして描画コマンドはスキップする
	void FinalBlit(ID3D12GraphicsCommandList* commandList, uint32_t finalSrv, bool editorActive = false);

	// ウィンドウサイズが変わったときの処理（画用紙のサイズも変える）
	void OnResize(uint32_t width, uint32_t height);

	// MRT（2枚同時書き込み）を開始する関数
	void PreDrawSceneMRT(ID3D12GraphicsCommandList* commandList);
	void PostDrawSceneMRT(ID3D12GraphicsCommandList* commandList);



	// デバッグ用UIの描画
	void DrawDebugUI();


	// 外から時間を進めるための関数
	void AddTime(float addValue) {
		time_ += addValue;
		if (timeData_) {
			*timeData_ = time_;
		}
	}

	// エフェクトの有効化・無効化
	void Save(const std::string& filePath = "resources/data/postEffect.json"); // 現在のエフェクト設定をJSONファイルに保存
	void Load(const std::string& filePath = "resources/data/postEffect.json"); // JSONファイルからエフェクト設定を読み込む
public: // ゲームプレイシーンなどから、描画に必要なSRVインデックスを取得するための関数

	uint32_t GetSrvIndex() const;

	//  特定のエフェクトだけを狙ってON/OFFする
	void SetEffectActive(PostEffectType type, bool isActive) {
		activeEffects_[static_cast<int>(type)] = isActive;
	}

	//  特定のエフェクトが今ONになっているか確認する
	bool GetEffectActive(PostEffectType type) const {
		return activeEffects_[static_cast<int>(type)];
	}

	//  大元（マスター）のスイッチをON/OFFする
	void SetMasterActive(bool isActive) {
		isActive_ = isActive;
	}

	//  全てのエフェクトを一括でOFFにする（シーン切り替え時などに便利！）
	void ClearAllEffects() {
		for (int i = 0; i < static_cast<int>(PostEffectType::Count); ++i) {
			activeEffects_[i] = false;
		}
	}

	// いつでも最終的な結果のSRVインデックスを取得できる関数
	uint32_t GetMaskSrvIndex() const{ return maskTexture_->GetSrvIndex(); }

private:

	PostEffect() = default;
	PostEffect(const PostEffect&) = delete;
	PostEffect& operator=(const PostEffect&) = delete;

private:

	// ポストエフェクトの描画（巨大な三角形を描く）※エフェクトの種類を指定して、ON/OFFも指定できるバージョン
	void ApplyEffect(ID3D12GraphicsCommandList* commandList, DirectXCommon* dxCommon, PostEffectType type, bool isEffectActive, uint32_t& src, uint32_t& dest);


private:
	// 2枚の画用紙を配列にして、交互に使い回す（ピンポン描画）
	std::unique_ptr<RenderTexture> renderTextures_[2];
	
	uint32_t finalResultIndex_ = 0; // 最終的な結果がどちらの画用紙にあるかを示すインデックス（0か1）

	// もしエフェクトの中で、時間経過で変化させたいものがあれば、そこに必要なリソースをここで用意しておく
	std::unique_ptr<RenderTexture> maskTexture_;

	bool isActive_ = true; // エフェクト全体の大元スイッチ
	// Enumの要素数(Count)の分だけ bool の配列を作る
	bool activeEffects_[static_cast<int>(PostEffectType::Count)] = { false };

	// 時間経過で変化するエフェクトのための時間管理
	Microsoft::WRL::ComPtr<ID3D12Resource> timeResource_;
	float* timeData_ = nullptr;
	float time_ = 0.0f;
	float timeSpeed_ = 0.05f;

	DirectXCommon* dxCommon_ = nullptr;

};