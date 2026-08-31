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
	ColorTint,        // 全面カラーティント（死亡フラッシュ等）
	MaskedDistortion, // 指定円周囲の熱波歪み
	MaskedGlow,       // 指定円周囲のスポットライト
	MaskedSepia,      // 指定エリアのセピア
	PictureBook,      // 絵本風（ポスタライズ＋輪郭線）
	TiltShift,        // ティルトシフト（画面上下をぼかしてジオラマ風。中央の帯はシャープ）
	PaperGrain,       // 紙の質感（繊維ノイズをうっすら乗算。クラフト感の底上げ）
	IrisWipe,         // アイリスワイプ（指定点中心の円の外側を紙色で覆う。リスポーン演出）
	Count           // 種類の数
};

// ポストエフェクト共通の定数バッファ (b0) ／ HLSL の PostEffectParams と一致させる。
//   既存シェーダーは先頭8floatだけを宣言して読む（CBが大きい分には問題ない）
struct PostEffectParams {
	float time = 0.0f;       // 経過時間（ノイズ・歪みアニメ等）
	float param0 = 0.0f;     // 汎用パラメータ（ColorTint の alpha など）
	float colorR = 1.0f;     // ティント色 R
	float colorG = 1.0f;     // ティント色 G
	float colorB = 1.0f;     // ティント色 B
	// --- アイリスワイプ ---
	float irisRadius = 2.0f;   // 円の半径（画面高さ=1基準。2=全開＝実質無効）
	float irisCX = 0.5f;       // 円の中心UV
	float irisCY = 0.5f;
	// --- ティルトシフト ---（既定値＝Play用に調整した本番ルック）
	float tiltStrength = 7.2f;   // ぼかし強さ(px)
	float tiltCenterY = 0.55f;   // ピントが合う帯の中心（UV）
	float tiltHalfWidth = 0.17f; // 帯の半幅（この範囲はシャープ）
	// --- 紙の質感 ---
	float grainStrength = 0.17f; // 繊維ノイズの強さ
	// --- 絵本風 ---
	float posterLevels = 14.0f;  // 色の段階数
	float posterEdge = 0.8f;     // 輪郭線の濃さ
	float padding[2] = { 0.0f, 0.0f };
};

// マスク系ポストエフェクト用の定数バッファ (b1) ／ HLSL の MaskParams と一致させる
struct PostEffectMaskParams {
	float slot0X = 0.0f;       // スロット0（例：ボス）スクリーンUV X
	float slot0Y = 0.0f;       // スロット0 スクリーンUV Y
	float slot0Radius = 0.0f;  // スロット0 半径
	float slot0Pad = 0.0f;     // 予備（用途はシェーダー依存）
	float slot1X = 0.0f;       // スロット1（例：プレイヤー）スクリーンUV X
	float slot1Y = 0.0f;       // スロット1 スクリーンUV Y
	float slot1Radius = 0.0f;  // スロット1 半径
	float aspectRatio = 1.0f;  // アスペクト比（正円補正）
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
		if (paramData_) {
			paramData_->time = time_;
		}
	}

	// 全面カラーティント（ColorTint）のパラメータ設定。alpha=0で無効、1で全面塗り
	void SetColorTint(float alpha, float r, float g, float b) {
		if (paramData_) {
			paramData_->param0 = alpha;
			paramData_->colorR = r;
			paramData_->colorG = g;
			paramData_->colorB = b;
		}
	}

	// マスク系エフェクト（MaskedDistortion / MaskedGlow / MaskedSepia）のパラメータ設定
	void SetMaskParams(const PostEffectMaskParams& params) {
		if (maskData_) {
			*maskData_ = params;
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

	// --- 新エフェクトのパラメータ設定（persistent map な b0 へ直接書く）---
	void SetTiltShiftParams(float strengthPx, float centerY, float halfWidth){
		paramData_->tiltStrength = strengthPx; paramData_->tiltCenterY = centerY; paramData_->tiltHalfWidth = halfWidth;
	}
	void SetPaperGrainStrength(float strength){ paramData_->grainStrength = strength; }
	void SetPictureBookParams(float levels, float edge){ paramData_->posterLevels = levels; paramData_->posterEdge = edge; }
	// アイリスワイプ：radius=円の半径（画面高さ=1基準。0=全閉/1.4=全開）、cx/cy=中心UV
	void SetIrisParams(float radius, float cx, float cy){
		paramData_->irisRadius = radius; paramData_->irisCX = cx; paramData_->irisCY = cy;
	}

	//  全てのエフェクトを一括でOFFにする（シーン切り替え時などに便利！）
	void ClearAllEffects() {
		for (int i = 0; i < static_cast<int>(PostEffectType::Count); ++i) {
			activeEffects_[i] = false;
		}
	}

	// いつでも最終的な結果のSRVインデックスを取得できる関数
	uint32_t GetMaskSrvIndex() const{ return maskTexture_->GetSrvIndex(); }

	// 最終結果の RenderTexture 本体（SDF焼き込みなど「この上に描き足す」用途向け）
	// ※ finalResultIndex_ は Draw() 内で更新されるため、Draw() 実行後にのみ有効
	RenderTexture* GetFinalTexture() const { return renderTextures_[finalResultIndex_].get(); }

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

	// ポストエフェクト共通定数バッファ (b0)。先頭が time なので既存の時間参照シェーダーとも互換
	Microsoft::WRL::ComPtr<ID3D12Resource> timeResource_;
	PostEffectParams* paramData_ = nullptr;
	float time_ = 0.0f;
	float timeSpeed_ = 0.05f;

	// マスク系エフェクト用定数バッファ (b1)
	Microsoft::WRL::ComPtr<ID3D12Resource> maskResource_;
	PostEffectMaskParams* maskData_ = nullptr;

	DirectXCommon* dxCommon_ = nullptr;

};