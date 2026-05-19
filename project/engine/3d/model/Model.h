#pragma once
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

// --- 標準ライブラリ ---
#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

// --- エンジン側のファイル ---
#include "engine/math/struct.h"
#include "engine/math/Matrix4x4.h"
#include "engine/3d/animation/Skeleton.h"

// 前方宣言
class ModelCommon;
struct aiNode;

class Model{

public: // サブクラス定義

	// 1頂点に影響するボーン情報（最大4つ）
	struct VertexInfluence {
		float    weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		int32_t  jointIndices[4] = { 0, 0, 0, 0 };
	};

	// 頂点データ構造体
	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
		VertexInfluence influence;
	};

	// マテリアルデータ構造体
	struct MaterialData{
		std::string textureFilePath;
		uint32_t textureIndex = 0;
	};

	struct ModelData{
		std::vector<VertexData> vertices; // 頂点データ
		std::vector<uint32_t> indices;    // インデックスデータ
		MaterialData material;            // マテリアルデータ
		Node rootNode; // モデルの階層構造のルートノード
		std::map<std::string, Matrix4x4>     inverseBindPoseMap;
		std::vector<std::string> boneOrder; // ボーンの順番（頂点のjointIndicesと対応させるため）
	};
	// 定数バッファ用データ構造体
	struct Material{
		Vector4 color;// 材質の色
		int32_t enableLighting;// ライティングの有効無効
		float padding[3];       // 4 bytes * 3 = 12 bytes (行列を16バイト境界に合わせるための詰め物)
		Matrix4x4 uvTransform;
		float shininess;       
		float padding2[2];      // 8バイト (HLSLの float2 padding と一致)
		float emissive;         // 4バイト (HLSLの emissive と一致)
		float padding3;         // 4バイト (全体を16の倍数にするための詰め物)
	};

public: // メンバ関数

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(ModelCommon* modelCommon, const std::string& directoryPath, const std::string& filename);
	// <summary>
	/// 球モデルの初期化
	/// </summary>
	void InitializeSphere(ModelCommon* modelCommon, int subdivision);
	void InitializeRing(ModelCommon* modelCommon, int subdivision);


	/// <summary>
	/// 描画
	/// </summary>
	void Draw(uint32_t instanceCount = 1, D3D12_GPU_VIRTUAL_ADDRESS materialAddress = 0);

	Material* GetMaterial(){ return materialData_; }

	const Node& GetRootNode() const { return modelData_.rootNode; }

	const std::map<std::string, Matrix4x4>& GetInverseBindPoseMap() const {
		return modelData_.inverseBindPoseMap;
	}

	const std::vector<std::string>& GetBoneOrder() const { return modelData_.boneOrder; }


	// 現在のテクスチャインデックスを取得
	uint32_t GetTextureIndex() const{ return modelData_.material.textureIndex; }

	void SetColor(const Vector4& color);

	// テクスチャを「SRVインデックス」で上書き設定する
	void SetTexture(uint32_t textureIndex);

	// テクスチャを「ファイルパス」から読み込んで上書き設定する
	void SetTexture(const std::string& textureFilePath);

private: // 内部関数
	 
	// aiNodeからNode構造体を再帰的に作る関数
	static Node ParseNode(const aiNode* node);
	
	// モデルファイルの読み込み (拡張子に応じて適切なローダーを呼び出す)
	static ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);


	// モデルの頂点座標を中心（原点）に合わせる
	void AdjustModelCenter();
	// / バッファの作成
	void CreateBuffers();


	
private: // メンバ変数

	ModelCommon* modelCommon_ = nullptr;

	// モデルデータ (CPU側)
	ModelData modelData_;

	// バッファリソース (GPU側)
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

	// バッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_ {};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_ {};

	// データを書き込むためのポインタ (Map用)
	Material* materialData_ = nullptr;

	// テクスチャハンドル
	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle_ {};

};
