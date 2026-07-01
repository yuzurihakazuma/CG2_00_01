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

// 卵の殻の破片モデル：球の一部を切り取った「湾曲した薄いカケラ」を頂点から手作りする。
//   ・実際の殻と同じく球面の一部なので、ライティングで丸い陰影がつき殻らしく見える。
//   ・割れ口(外周)はギザギザに欠けさせ、薄い厚みを付ける。全部コード生成＝専用アート不要。
//   ・薄いので両面表示前提（描画側で Object3D_CullNone を使う）。
void ModelManager::CreateEggShellModel(const std::string& modelName, float size){
	if ( models_.contains(modelName) ) { return; } // 重複防止

	Model::ModelData modelData;

	const int   segs      = 10;             // 円周方向の分割数
	const float R         = size * 2.2f;    // 曲率半径（カケラより大きめ＝ゆるやかに湾曲）
	const float thickness = size * 0.05f;   // 殻の薄さ
	const float thetaMax  = 0.52f;          // キャップの開き角(rad)＝カケラの大きさ
	const float Ri        = R - thickness;  // 内側（背面）の半径
	const float PI        = 3.14159265f;

	// キャップ中央の高さぶん下げて、原点付近を中心に回るようにする
	const float zShift = R * std::cos(thetaMax * 0.5f);

	// 球面上(半径rad, 極角th, 方位ph)の点を頂点として追加（法線は外向き単位ベクトル×sign）
	auto addOnSphere = [&](float rad, float th, float ph, float nSign) -> uint32_t {
		float sx = std::sin(th) * std::cos(ph);
		float sy = std::sin(th) * std::sin(ph);
		float sz = std::cos(th);
		Model::VertexData v;
		v.position = { rad * sx, rad * sy, rad * sz - zShift, 1.0f };
		v.normal   = { sx * nSign, sy * nSign, sz * nSign }; // 外側+1 / 内側-1
		v.texcoord = { 0.5f, 0.5f }; // 白テクスチャなのでUVは中央固定でOK
		modelData.vertices.push_back(v);
		return ( uint32_t ) modelData.vertices.size() - 1;
	};

	// 割れ口(外周)のギザギザ：方位ごとに開き角を変える
	auto edgeTheta = [&](int j) -> float {
		float jag = 0.55f + 0.45f * ( 0.5f + 0.5f * std::sin(( float ) j * 2.39996f + 0.7f ) );
		return thetaMax * jag;
	};

	// 極（前面=外側 / 背面=内側）
	uint32_t pf = addOnSphere(R,  0.0f, 0.0f,  1.0f);
	uint32_t pb = addOnSphere(Ri, 0.0f, 0.0f, -1.0f);

	// リング頂点：mid(なめらか) と out(ギザギザ外周) を前面/背面ぶん確保
	std::vector<uint32_t> fMid(segs), fOut(segs), bMid(segs), bOut(segs);
	for ( int j = 0; j < segs; ++j ) {
		float ph = ( float ) j / segs * 2.0f * PI;
		float te = edgeTheta(j);
		float tm = te * 0.5f;
		fMid[j] = addOnSphere(R,  tm, ph,  1.0f);
		fOut[j] = addOnSphere(R,  te, ph,  1.0f);
		bMid[j] = addOnSphere(Ri, tm, ph, -1.0f);
		bOut[j] = addOnSphere(Ri, te, ph, -1.0f);
	}

	auto tri = [&](uint32_t a, uint32_t b, uint32_t c){
		modelData.indices.push_back(a); modelData.indices.push_back(b); modelData.indices.push_back(c);
	};

	// 両面描画(CullNone)前提なので巻き順は気にせず、面を張る。
	for ( int j = 0; j < segs; ++j ) {
		int k = ( j + 1 ) % segs;
		// 前面：極→中リング→外リング(バンド)
		tri(pf, fMid[j], fMid[k]);
		tri(fMid[j], fOut[j], fMid[k]);
		tri(fMid[k], fOut[j], fOut[k]);
		// 背面（内側）
		tri(pb, bMid[j], bMid[k]);
		tri(bMid[j], bOut[j], bMid[k]);
		tri(bMid[k], bOut[j], bOut[k]);
		// 割れ口の厚み（前面外周と背面外周をつなぐ）
		tri(fOut[j], bOut[j], fOut[k]);
		tri(fOut[k], bOut[j], bOut[k]);
	}

	// 外部アート非依存：汎用の白テクスチャ＋形状の陰影だけで殻を表現する。
	modelData.material.textureFilePath = "resources/block/white1x1.png";

	std::unique_ptr<Model> newModel = std::make_unique<Model>();
	newModel->InitializePrimitive(modelCommon_.get(), modelData);

	// 殻らしいクリーム色＋ライティングONで湾曲の陰影を出す（共有マテリアルにデフォルトとして焼き込む）。
	if ( auto* mat = newModel->GetMaterial() ) {
		mat->color          = { 0.97f, 0.95f, 0.86f, 1.0f };
		mat->enableLighting = 1;
	}

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
