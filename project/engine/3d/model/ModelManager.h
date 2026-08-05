#pragma once
// --- 標準ライブラリ ---
#include <cstddef>
#include <map>
#include <string>
#include <memory>
// 前方宣言
class Model;
class ModelCommon;
class DirectXCommon;

// モデルマネージャークラス
class ModelManager{
public:
	// / 初期化
	void Initialize(DirectXCommon* dxCommon);
	// 球モデルの作成
	void CreateSphereModel(const std::string& modelName, int subdivision);

	// 平面モデルの作成
	void CreatePlaneModel(const std::string& modelName, float width = 1.0f, float height = 1.0f);

	// 立方体モデルの作成
	void CreateCubeModel(const std::string& modelName, float size = 1.0f);
	// プリミティブモデルの作成
	void CreateRingModel(const std::string& modelName, int subdivision = 32, float outerRadius = 1.0f, float innerRadius = 0.2f);
	// 円柱（チューブ型）モデルの作成
	void CreateCylinderModel(const std::string& modelName, int subdivision = 32, float radius = 1.0f, float height = 2.0f);
	// 3D星型モデルの作成
	void CreateStarModel(const std::string& modelName, float outerRadius = 0.5f, float innerRadius = 0.2f, float depth = 0.15f);
	// 卵の殻の破片モデルの作成（歪んだ五角形をドーム状に湾曲させた薄い欠片。割れ演出用）
	void CreateEggShellModel(const std::string& modelName, float size = 0.3f);

	// シングルストーンのインスタンスを取得
	static ModelManager* GetInstance();

	// インスタンスの破棄
	void Finalize();
	// モデルのロード
	// keepOrigin=true でファイルの原点を維持（重心センタリングをしない。底面原点の配置物向け）
	void LoadModel(const std::string& modelName, const std::string& directoryPath, const std::string& filename, bool keepOrigin = false);
	// モデルの検索
	Model* FindModel(const std::string& filePath);

	// 実行時生成メッシュ用：ModelCommon を貸し出す（RoadMesh・舌など呼び出し側が
	// 自前の Model を作る時に使う。所有権は渡さない）
	ModelCommon* GetModelCommon() const { return modelCommon_.get(); }


private:

	// コンストラクタ
	ModelManager();

	// デストラクタ
	~ModelManager();
	// コピーコンストラクタ（禁止）
	ModelManager(const ModelManager&) = delete;
	// コピー代入演算子（禁止）
	ModelManager& operator=(const ModelManager&) = delete;
	// 静的メンバ変数の定義
	static ModelManager* instance_;


private:

	// ModelCommonのポインタ
	std::unique_ptr<ModelCommon> modelCommon_ = nullptr;

	// モデルの取得
	std::map<std::string, std::unique_ptr<Model>> models_;

};

