#pragma once
// --- 標準ライブラリ ---
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

// --- エンジン側のファイル ---
#include "engine/math/struct.h"
#include "engine/math/Matrix4x4.h"
#include "engine/3d/model/Model.h"
#include "Animation.h"
#include "IAnimatable.h"

// 前方宣言
class Obj3dCommon;
class Camera;
class DirectXCommon;

// モデル3Dクラス
class Obj3d: public IAnimatable{
public:
	// 定数バッファ用データ構造体
	// ※座標（WVP/World）はオブジェクトごとに違うのでここに残す
	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
		Matrix4x4 WorldInverseTranspose;
	};

	// ディゾルブエフェクト用の定数バッファ構造体
	struct DissolveData {
		float threshold; // 進行度（0.0で通常、1.0で完全に消える）
		Vector3 edgeColor; // ディゾルブ境界の色（Bloom用）
		float padding; // 16バイト境界に合わせるための空き
	};

	/// <summary>
	/// 静的生成・初期化短縮関数
	///  <summary>
	static std::unique_ptr<Obj3d> Create(const std::string& modelName, const Vector3& translate = { 0,0,0 }, const Vector3& rotate = { 0,0,0 }, const Vector3& scale = { 1,1,1 });

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(Model* model);

	/// <summary>
	/// 更新
	/// </summary>
	void Update() override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw()   override;

	
	void PlayAnimation(Animation* animation);

public:
	// -------------------------------------------------
	// Getter / Setter
	// -------------------------------------------------

	const Vector3& GetScale() const override { return scale_; }
	const Vector3& GetRotation() const override { return rotation_; }
	const Vector3& GetTranslation() const override { return translate_; }

	void SetScale (const Vector3& scale) override { scale_ = scale; }
	void SetRotation(const Vector3& rotation) override { rotation_ = rotation; }
	void SetTranslation(const Vector3& translate) override { translate_ = translate; }

	// モデルを差し替えるための関数
	void SetModel(Model* model){ model_ = model; }
	void SetCamera(const Camera* camera){ this->camera_ = camera; }

	// ディゾルブエフェクトのパラメータをセットする関数
	void SetDissolveThreshold(float threshold);

	// ディゾルブ境界の色を設定する
	void SetDissolveColor(const Vector3& color);
	void SetNoiseTexture(uint32_t textureIndex);
	void SetColor(const Vector4& color);
	void ClearColorOverride();

	// 座標変換行列を取得する関数
	const TransformationMatrix& GetMatrixData() const { return matrixData_; }

	Model* GetModel() const { return model_; }

	// UI表示用の名前
	const std::string& GetName() const override { return name_; }
	void SetName(const std::string& name) override { name_ = name; }

private:

	// -------------------------------------------------
	// メンバ変数
	// -------------------------------------------------

	// トランスフォーム（場所・回転・大きさ）
	Vector3 scale_ = { 1.0f, 1.0f, 1.0f };       
	Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };    // 回転なし
	Vector3 translate_ = { 0.0f, 0.0f, 0.0f };   // 原点

	// 行列計算用（計算を楽にするために保持）
	Transform transform { {1.0f,1.0f,1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f} };

	// 外部参照
	Obj3dCommon* obj3dCommon_ = nullptr; // 所有しない参照
	Model* model_ = nullptr;             // 描画するモデル（所有しない）

	// 座標変換行列リソース (WVP / World)
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	const Camera* camera_ = nullptr; // 所有しない参照
	// ディゾルブエフェクト用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource_;
	DissolveData* dissolveData_ = nullptr;
	uint32_t noiseTextureIndex_ = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialOverrideResource_;
	Model::Material* materialOverrideData_ = nullptr;
	bool useMaterialOverride_ = false;

	// 再生中のアニメーションデータ
	Animation* currentAnimation_ = nullptr; 

	// 現在の再生時間（秒）
	float animationTime_ = 0.0f;            

	// 座標
	TransformationMatrix matrixData_;

	// UI表示用の名前
	std::string name_ = "Obj3d";
};
