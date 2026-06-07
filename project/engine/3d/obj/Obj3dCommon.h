#pragma once
// --- 標準ライブラリ ---
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
// --- エンジン側のファイル ---
#include "engine/math/struct.h"

// 前方宣言
class DirectXCommon;
class Camera;


// 3Dオブジェクト共通クラス
class Obj3dCommon{
public:
	
	static Obj3dCommon* GetInstance(){
		static Obj3dCommon instance;
		return &instance;
	}
	
	static const uint32_t MAX_DIRECTIONAL_LIGHTS = 4;
	
	// ライト構造体
	struct DirectionalLight{
		Vector4 color;     // ライトの色
		Vector3 direction; // ライトの向き
		float intensity;   // 輝度
	};

	struct DirectionalLightData{
		DirectionalLight lights[MAX_DIRECTIONAL_LIGHTS];
		int32_t activeCount; // 現在有効なライトの数
		float padding[3];    // 16バイトアライメント用の空き箱（超重要）
	};

	// 点光源の構造体
	struct PointLight {
		Vector4 color;
		Vector3 position;
		float intensity;
		float radius;      // ライトが届く最大距離
		float decay;       // 減衰率
		float padding[2];
	};

	// スポットライトの構造体
	struct SpotLight {
		Vector4 color;           // ライトの色
		Vector3 position;        // ライトの位置
		float intensity;         // 輝度
		Vector3 direction;       // ライトの方向
		float distance;          // ライトの届く最大距離
		float decay;             // 減衰率
		float cosAngle;          // スポットライトの余弦
		float cosFalloffStart;   // フォールオフ開始の余弦
		float padding;           // 16バイトアライメント用
	};

public:
	// ライトデータの取得
	DirectionalLightData* GetLightData(){ return directionalLightData_; }
	// ライトリソースの取得
	ID3D12Resource* GetLightResource() const{ return directionalLightResource_.Get(); }
	// 点光源リソースの取得
	PointLight* GetPointLightData() { return pointLightData_; }
	ID3D12Resource* GetPointLightResource() const { return pointLightResource_.Get(); }
	// スポットライトデータの取得
	SpotLight* GetSpotLightData() { return spotLightData_; }
	ID3D12Resource* GetSpotLightResource() const { return spotLightResource_.Get(); }

	void SetEnvironmentTexture(uint32_t srvIndex){ environmentTextureSrvIndex_ = srvIndex; }
	uint32_t GetEnvironmentTextureSrvIndex() const{ return environmentTextureSrvIndex_; }

	// 既定（アクティブ）カメラ。これを設定しておくと Obj3d::Create() が自動でカメラを適用する
	// （オブジェクトごとに SetCamera を呼ぶ手間が省ける。個別に SetCamera すれば上書き可）
	void SetDefaultCamera(const Camera* camera){ defaultCamera_ = camera; }
	const Camera* GetDefaultCamera() const{ return defaultCamera_; }


	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(DirectXCommon* dxCommon);
	
	// デバッグUIの描画
	void DrawDebugUI();


	// 終了処理
	void Finalize();

	// 共通の描画設定
	void PreDraw(ID3D12GraphicsCommandList* commandList);
	DirectXCommon* GetDxCommon() const{ return dxCommon_; }

private:
	// コンストラクタを private にして外部からの生成を禁止
	Obj3dCommon() = default;
	~Obj3dCommon() = default;
	Obj3dCommon(const Obj3dCommon&) = delete;
	Obj3dCommon& operator=(const Obj3dCommon&) = delete;

private:
	DirectXCommon* dxCommon_ = nullptr; // 所有しない参照

	
	DirectionalLightData* directionalLightData_ = nullptr;

	// ライト用リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	// 点光源リソース
	Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_;
	PointLight* pointLightData_ = nullptr;
	// スポットライトリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_;
	SpotLight* spotLightData_ = nullptr;

	uint32_t environmentTextureSrvIndex_ = 0;

	// 既定（アクティブ）カメラ（所有しない）
	const Camera* defaultCamera_ = nullptr;
};

