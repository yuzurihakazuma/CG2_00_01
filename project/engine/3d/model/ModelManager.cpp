#include "ModelManager.h"
// 基盤システムのヘッダー
#include "Model.h"
#include "ModelCommon.h"
#include "DirectXCommon.h"


ModelManager* ModelManager::instance_ = nullptr;

ModelManager::ModelManager(){}

ModelManager::~ModelManager(){}


ModelManager* ModelManager::GetInstance() {
	// 関数内で static 変数を宣言すると、プログラム終了まで生き残り、
	static ModelManager instance;
	return &instance;
}

void ModelManager::Finalize() {
	models_.clear();
	modelCommon_.reset();
}

// 初期化
void ModelManager::Initialize(DirectXCommon* dxCommon){ 
	modelCommon_ = std::make_unique<ModelCommon>();
	modelCommon_->Initialize(dxCommon);

}

// 球モデルの作成
void ModelManager::CreateSphereModel(const std::string& modelName, int subdivision){

	// 重複読み込み防止：すでに同じ名前で登録されていたら何もしない
	if ( models_.contains(modelName) ) {
		return;
	}

	// 1. モデル生成
	std::unique_ptr<Model> newModel = std::make_unique<Model>();

	// 2. モデル初期化 (ModelManagerが持っているModelCommonを渡す)
	newModel->InitializeSphere(modelCommon_.get(), subdivision);
	// 3. マップに登録 (moveで所有権をマップに移す)
	models_.insert(std::make_pair(modelName, std::move(newModel)));



}

void ModelManager::CreatePlaneModel(const std::string& modelName, float width, float height){
	// 重複読み込み防止：すでに同じ名前で登録されていたら何もしない
	if ( models_.contains(modelName) ) { return; }

	// 1. モデル生成
	std::unique_ptr<Model> newModel = std::make_unique<Model>();

	// 2. モデル自身に「平面になれ！」と指示を出す
	newModel->InitializePlane(modelCommon_.get(), width, height);

	// 3. マップに登録（倉庫に保管）
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}


void ModelManager::CreateCubeModel(const std::string& modelName, float size){
	if ( models_.contains(modelName) ) { return; } // 重複防止

	std::unique_ptr<Model> newModel = std::make_unique<Model>();
	newModel->InitializeCube(modelCommon_.get(), size); // 立方体になれ！と指示
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}

void ModelManager::CreateRingModel(const std::string& modelName, int subdivision, float outerRadius, float innerRadius){
	if ( models_.contains(modelName) ) { return; } // 重複防止

	std::unique_ptr<Model> newModel = std::make_unique<Model>();
	newModel->InitializeRing(modelCommon_.get(), subdivision, outerRadius, innerRadius);
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}

void ModelManager::CreateCylinderModel(const std::string& modelName, int subdivision, float radius, float height){
	if ( models_.contains(modelName) ) { return; }

	std::unique_ptr<Model> newModel = std::make_unique<Model>();
	newModel->InitializeCylinder(modelCommon_.get(), subdivision, radius, height);
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}

void ModelManager::CreateStarModel(const std::string& modelName, float outerRadius, float innerRadius, float depth) {
	if ( models_.contains(modelName) ) { return; } // 重複防止

	Model::ModelData modelData;
	
	// 前面と後面の中心
	Model::VertexData centerFront;
	centerFront.position = { 0.0f, 0.0f, depth * 0.5f, 1.0f };
	centerFront.normal = { 0.0f, 0.0f, 1.0f };
	centerFront.texcoord = { 0.5f, 0.5f };
	centerFront.influence = {};
	modelData.vertices.push_back(centerFront); // Index 0

	Model::VertexData centerBack;
	centerBack.position = { 0.0f, 0.0f, -depth * 0.5f, 1.0f };
	centerBack.normal = { 0.0f, 0.0f, -1.0f };
	centerBack.texcoord = { 0.5f, 0.5f };
	centerBack.influence = {};
	modelData.vertices.push_back(centerBack); // Index 1

	// 外周5個、内周5個の計10点の星型頂点を生成
	// 前面10点(Index 2..11) と 後面10点(Index 12..21)
	for (int i = 0; i < 10; ++i) {
		float angle = i * (3.14159265f / 5.0f);
		float r = (i % 2 == 0) ? outerRadius : innerRadius;
		float x = std::sin(angle) * r;
		float y = std::cos(angle) * r;

		// 前面
		Model::VertexData vf;
		vf.position = { x, y, depth * 0.5f, 1.0f };
		vf.normal = { 0.0f, 0.0f, 1.0f };
		vf.texcoord = { (x / outerRadius + 1.0f) * 0.5f, (y / outerRadius + 1.0f) * 0.5f };
		vf.influence = {};
		modelData.vertices.push_back(vf);

		// 後面
		Model::VertexData vb;
		vb.position = { x, y, -depth * 0.5f, 1.0f };
		vb.normal = { 0.0f, 0.0f, -1.0f };
		vb.texcoord = { (x / outerRadius + 1.0f) * 0.5f, (y / outerRadius + 1.0f) * 0.5f };
		vb.influence = {};
		modelData.vertices.push_back(vb);
	}

	// インデックスの組み立て
	// vf_curr: 2 + i * 2,  vb_curr: 2 + i * 2 + 1
	// vf_next: 2 + next * 2, vb_next: 2 + next * 2 + 1
	
	// 1. 前面ポリゴン (扇状に10個の三角形)
	for (int i = 0; i < 10; ++i) {
		int next = (i + 1) % 10;
		modelData.indices.push_back(0); // 中心
		modelData.indices.push_back(2 + next * 2);
		modelData.indices.push_back(2 + i * 2);
	}

	// 2. 後面ポリゴン (裏返しの巻き順)
	for (int i = 0; i < 10; ++i) {
		int next = (i + 1) % 10;
		modelData.indices.push_back(1); // 中心
		modelData.indices.push_back(2 + i * 2 + 1);
		modelData.indices.push_back(2 + next * 2 + 1);
	}

	// 3. 側面ポリゴン (前面と後面をつなぐ四角形x10 ➔ 三角形x20)
	for (int i = 0; i < 10; ++i) {
		int next = (i + 1) % 10;
		int vf_curr = 2 + i * 2;
		int vb_curr = 2 + i * 2 + 1;
		int vf_next = 2 + next * 2;
		int vb_next = 2 + next * 2 + 1;

		// 三角形1
		modelData.indices.push_back(vf_curr);
		modelData.indices.push_back(vf_next);
		modelData.indices.push_back(vb_curr);

		// 三角形2
		modelData.indices.push_back(vb_curr);
		modelData.indices.push_back(vf_next);
		modelData.indices.push_back(vb_next);
	}

	modelData.material.textureFilePath = "resources/uvChecker.png";

	std::unique_ptr<Model> newModel = std::make_unique<Model>();
	newModel->InitializePrimitive(modelCommon_.get(), modelData);
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}

void ModelManager::LoadModel(const std::string& modelName, const std::string& directoryPath, const std::string& filename){
	// 重複読み込み防止：すでに同じ名前で登録されていたら何もしない
	if ( models_.contains(modelName) ) {
		return;
	}

	// 1. モデル生成
	std::unique_ptr<Model> newModel = std::make_unique<Model>();

	// 2. モデル初期化 (ModelManagerが持っているModelCommonを渡す)
	newModel->Initialize(modelCommon_.get(), directoryPath, filename);

	// 3. マップに登録 (moveで所有権をマップに移す)
	models_.insert(std::make_pair(modelName, std::move(newModel)));
}
// モデルの検索
Model* ModelManager::FindModel(const std::string& filePath){
	
	// 読み込みファイルを検索
	if ( models_.contains(filePath) ){
		// 見つかった場合はポインタを返す
		return models_.at(filePath).get();
	}
	
	
	// マップから検索
	return nullptr;
}
