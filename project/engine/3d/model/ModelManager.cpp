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
