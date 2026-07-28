#include "Model.h"
// --- 標準ライブラリ ---
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <numbers>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>



// --- エンジン側のファイル ---
#include "ModelCommon.h" 
#include "engine/math/Matrix4x4.h"
#include "engine/graphics/TextureManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/3d/obj/Obj3dCommon.h" 
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"

using namespace MatrixMath;

void Model::Initialize(ModelCommon* modelCommon, const std::string& directoryPath, const std::string& filename){

	// 1. ModelCommonのポインタを記録
	this->modelCommon_ = modelCommon;



	// 2. モデルデータをファイルから読み込む (引数のパスを使う)
	modelData_ = LoadModelFile(directoryPath, filename);

	// 3. モデルの頂点座標を中心（原点）に合わせる
	if (modelData_.boneOrder.empty()) {
		AdjustModelCenter();
	}
	// 4. マテリアルデータの読み込み (.mtlファイル)
	if ( modelData_.material.textureFilePath.empty() ) {
		modelData_.material.textureFilePath = "resources/uvChecker.png";
	}

	// 5. バッファの作成
	CreateBuffers();

}
// 球モデルの初期化
void Model::InitializeSphere(ModelCommon* modelCommon, int subdivision){

	this->modelCommon_ = modelCommon;
	modelData_ = {};


	// 2. 球体のデータを計算で生成
	const float kLonEvery = std::numbers::pi_v<float> *2.0f / float(subdivision);
	const float kLatEvery = std::numbers::pi_v<float> / float(subdivision);
	// 頂点データ生成
	for ( int latIndex = 0; latIndex < ( subdivision + 1 ); ++latIndex ) {
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;
		// 経度の方向に分割しながら線を描く
		for ( int lonIndex = 0; lonIndex < ( subdivision + 1 ); ++lonIndex ) {
			float lon = lonIndex * kLonEvery;
			VertexData vertex;

			// 座標
			vertex.position.x = std::cos(lat) * std::cos(lon);
			vertex.position.y = std::sin(lat);
			vertex.position.z = std::cos(lat) * std::sin(lon);
			vertex.position.w = 1.0f;
			// UV
			vertex.texcoord.x = float(lonIndex) / float(subdivision);
			vertex.texcoord.y = 1.0f - float(latIndex) / float(subdivision);
			// 法線
			vertex.normal.x = vertex.position.x;
			vertex.normal.y = vertex.position.y;
			vertex.normal.z = vertex.position.z;
			// 頂点データを追加
			modelData_.vertices.push_back(vertex);
		}
	}

	// インデックスデータ生成
	int vertexCountX = subdivision + 1;
	for ( int latIndex = 0; latIndex < subdivision; ++latIndex ) {
		for ( int lonIndex = 0; lonIndex < subdivision; ++lonIndex ) {
			uint32_t topLeft = latIndex * vertexCountX + lonIndex;
			uint32_t bottomLeft = ( latIndex + 1 ) * vertexCountX + lonIndex;
			uint32_t topRight = latIndex * vertexCountX + ( lonIndex + 1 );
			uint32_t bottomRight = ( latIndex + 1 ) * vertexCountX + ( lonIndex + 1 );

			modelData_.indices.push_back(topLeft);
			modelData_.indices.push_back(bottomLeft);
			modelData_.indices.push_back(topRight);
			modelData_.indices.push_back(topRight);
			modelData_.indices.push_back(bottomLeft);
			modelData_.indices.push_back(bottomRight);
		}
	}
	// テクスチャファイルパスはデフォルトのチェッカーテクスチャにしておく
	modelData_.material.textureFilePath = "resources/monsterBall.png";

	CreateBuffers(); // バッファの作成
}

// 平面モデルの初期化
void Model::InitializePlane(ModelCommon* modelCommon, float width, float height){

	// ModelCommonのポインタを記録
	this->modelCommon_ = modelCommon;
	modelData_ = {};

	// 1. 頂点データ (4頂点)
	VertexData v0, v1, v2, v3;
	float hw = width / 2.0f;
	float hh = height / 2.0f;

	// 左下
	v0.position = { -hw, -hh, 0.0f, 1.0f };
	v0.texcoord = { 0.0f, 1.0f };
	v0.normal = { 0.0f, 0.0f, -1.0f }; // 法線は手前向き

	// 左上
	v1.position = { -hw,  hh, 0.0f, 1.0f };
	v1.texcoord = { 0.0f, 0.0f };
	v1.normal = { 0.0f, 0.0f, -1.0f };

	// 右下
	v2.position = { hw, -hh, 0.0f, 1.0f };
	v2.texcoord = { 1.0f, 1.0f };
	v2.normal = { 0.0f, 0.0f, -1.0f };

	// 右上
	v3.position = { hw,  hh, 0.0f, 1.0f };
	v3.texcoord = { 1.0f, 0.0f };
	v3.normal = { 0.0f, 0.0f, -1.0f };

	modelData_.vertices = { v0, v1, v2, v3 };

	// 2. インデックスデータ (2ポリゴン = 6つのインデックス)
	modelData_.indices = { 0, 1, 2,  2, 1, 3 };

	// デフォルトのテクスチャ（エラー回避用）
	modelData_.material.textureFilePath = "resources/uvChecker.png";

	// 3. データを元にGPUのバッファを作る
	CreateBuffers();

}

void Model::InitializeCube(ModelCommon* modelCommon, float size){
	this->modelCommon_ = modelCommon;
	modelData_ = {};

	float hs = size / 2.0f;

	// 1. 頂点データ (6面 × 4頂点 = 24頂点)
	// 各面ごとに法線とUVが異なるため、頂点を共有せずに独立させます
	modelData_.vertices = {
		// 前面 (Front: -Z)
		{ {-hs, -hs, -hs, 1.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} }, { {-hs,  hs, -hs, 1.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f, -1.0f} },
		{ { hs, -hs, -hs, 1.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f, -1.0f} }, { { hs,  hs, -hs, 1.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f, -1.0f} },
		// 背面 (Back: +Z)
		{ { hs, -hs,  hs, 1.0f}, {0.0f, 1.0f}, { 0.0f,  0.0f,  1.0f} }, { { hs,  hs,  hs, 1.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f,  1.0f} },
		{ {-hs, -hs,  hs, 1.0f}, {1.0f, 1.0f}, { 0.0f,  0.0f,  1.0f} }, { {-hs,  hs,  hs, 1.0f}, {1.0f, 0.0f}, { 0.0f,  0.0f,  1.0f} },
		// 左面 (Left: -X)
		{ {-hs, -hs,  hs, 1.0f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f} }, { {-hs,  hs,  hs, 1.0f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f} },
		{ {-hs, -hs, -hs, 1.0f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f} }, { {-hs,  hs, -hs, 1.0f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f} },
		// 右面 (Right: +X)
		{ { hs, -hs, -hs, 1.0f}, {0.0f, 1.0f}, { 1.0f,  0.0f,  0.0f} }, { { hs,  hs, -hs, 1.0f}, {0.0f, 0.0f}, { 1.0f,  0.0f,  0.0f} },
		{ { hs, -hs,  hs, 1.0f}, {1.0f, 1.0f}, { 1.0f,  0.0f,  0.0f} }, { { hs,  hs,  hs, 1.0f}, {1.0f, 0.0f}, { 1.0f,  0.0f,  0.0f} },
		// 上面 (Top: +Y)
		{ {-hs,  hs, -hs, 1.0f}, {0.0f, 1.0f}, { 0.0f,  1.0f,  0.0f} }, { {-hs,  hs,  hs, 1.0f}, {0.0f, 0.0f}, { 0.0f,  1.0f,  0.0f} },
		{ { hs,  hs, -hs, 1.0f}, {1.0f, 1.0f}, { 0.0f,  1.0f,  0.0f} }, { { hs,  hs,  hs, 1.0f}, {1.0f, 0.0f}, { 0.0f,  1.0f,  0.0f} },
		// 下面 (Bottom: -Y)
		{ {-hs, -hs,  hs, 1.0f}, {0.0f, 1.0f}, { 0.0f, -1.0f,  0.0f} }, { {-hs, -hs, -hs, 1.0f}, {0.0f, 0.0f}, { 0.0f, -1.0f,  0.0f} },
		{ { hs, -hs,  hs, 1.0f}, {1.0f, 1.0f}, { 0.0f, -1.0f,  0.0f} }, { { hs, -hs, -hs, 1.0f}, {1.0f, 0.0f}, { 0.0f, -1.0f,  0.0f} }
	};

	// 2. インデックスデータ
	for ( uint32_t i = 0; i < 6; ++i ) {
		uint32_t base = i * 4;
		modelData_.indices.push_back(base + 0);
		modelData_.indices.push_back(base + 1);
		modelData_.indices.push_back(base + 2);
		modelData_.indices.push_back(base + 2);
		modelData_.indices.push_back(base + 1);
		modelData_.indices.push_back(base + 3);
	}

	modelData_.material.textureFilePath = "resources/uvChecker.png";
	CreateBuffers();
}


void Model::InitializePrimitive(ModelCommon* modelCommon, const ModelData& modelData){

	this->modelCommon_ = modelCommon;
	this->modelData_ = modelData;

	// テクスチャ未指定の保険（CreateBuffers はパスを無条件に読むため、
	// 空のまま渡されると空パスのロードで assert してしまう。後で SetTexture で差し替え可能）
	if ( modelData_.material.textureFilePath.empty() ) {
		modelData_.material.textureFilePath = "resources/uvChecker.png";
	}

	CreateBuffers();
}

void Model::InitializeRing(ModelCommon* modelCommon, int subdivision, float outerRadius, float innerRadius){
	this->modelCommon_ = modelCommon;
	modelData_ = {};

	const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(subdivision);

	for ( int index = 0; index < subdivision; ++index ) {
		float sin = std::sin(index * radianPerDivide);
		float cos = std::cos(index * radianPerDivide);
		float sinNext = std::sin(( index + 1 ) * radianPerDivide);
		float cosNext = std::cos(( index + 1 ) * radianPerDivide);

		// U成分 (横方向のUV) は円周に沿って 0.0 ～ 1.0 へ変化
		float u = float(index) / float(subdivision);
		float uNext = float(index + 1) / float(subdivision);

		VertexData v[4];

		// ① 外側・現在の角度
		v[0].position = { -sin * outerRadius, cos * outerRadius, 0.0f, 1.0f };
		v[0].texcoord = { u, 0.0f }; // 外側を V=0 にする
		v[0].normal = { 0.0f, 0.0f, -1.0f };

		// ② 外側・次の角度
		v[1].position = { -sinNext * outerRadius, cosNext * outerRadius, 0.0f, 1.0f };
		v[1].texcoord = { uNext, 0.0f };
		v[1].normal = { 0.0f, 0.0f, -1.0f };

		// ③ 内側・現在の角度
		v[2].position = { -sin * innerRadius, cos * innerRadius, 0.0f, 1.0f };
		v[2].texcoord = { u, 1.0f }; // 内側を V=1 にする
		v[2].normal = { 0.0f, 0.0f, -1.0f };

		// ④ 内側・次の角度
		v[3].position = { -sinNext * innerRadius, cosNext * innerRadius, 0.0f, 1.0f };
		v[3].texcoord = { uNext, 1.0f };
		v[3].normal = { 0.0f, 0.0f, -1.0f };

		// 頂点をバッファに追加し、基準となるインデックス番号を記憶
		uint32_t baseIndex = static_cast< uint32_t >( modelData_.vertices.size() );
		for ( int i = 0; i < 4; ++i ) {
			modelData_.vertices.push_back(v[i]);
		}

		// インデックスデータを追加 (時計回りで三角形を2つ作る)
		// 1つ目の三角形: ① -> ② -> ③
		modelData_.indices.push_back(baseIndex + 0);
		modelData_.indices.push_back(baseIndex + 1);
		modelData_.indices.push_back(baseIndex + 2);
		// 2つ目の三角形: ③ -> ② -> ④
		modelData_.indices.push_back(baseIndex + 2);
		modelData_.indices.push_back(baseIndex + 1);
		modelData_.indices.push_back(baseIndex + 3);
	}

	// デフォルトのテクスチャ（適当なもの）を指定してバッファ作成
	modelData_.material.textureFilePath = "resources/uvChecker.png";
	CreateBuffers();
}

void Model::InitializeCylinder(ModelCommon* modelCommon, int subdivision, float radius, float height){
	this->modelCommon_ = modelCommon;
	modelData_ = {};

	const float radianPerDivide = 2.0f * std::numbers::pi_v<float> / float(subdivision);
	const float halfHeight = height / 2.0f;

	// 頂点とインデックスの生成
	for ( int index = 0; index < subdivision; ++index ) {
		float sin = std::sin(index * radianPerDivide);
		float cos = std::cos(index * radianPerDivide);
		float sinNext = std::sin(( index + 1 ) * radianPerDivide);
		float cosNext = std::cos(( index + 1 ) * radianPerDivide);

		float u = float(index) / float(subdivision);
		float uNext = float(index + 1) / float(subdivision);

		VertexData v[4];

		// ① 下面・現在の角度
		v[0].position = { sin * radius, -halfHeight, cos * radius, 1.0f };
		v[0].texcoord = { u, 1.0f };
		v[0].normal = { sin, 0.0f, cos }; // 外側を向く法線

		// ② 下面・次の角度
		v[1].position = { sinNext * radius, -halfHeight, cosNext * radius, 1.0f };
		v[1].texcoord = { uNext, 1.0f };
		v[1].normal = { sinNext, 0.0f, cosNext };

		// ③ 上面・現在の角度
		v[2].position = { sin * radius, halfHeight, cos * radius, 1.0f };
		v[2].texcoord = { u, 0.0f };
		v[2].normal = { sin, 0.0f, cos };

		// ④ 上面・次の角度
		v[3].position = { sinNext * radius, halfHeight, cosNext * radius, 1.0f };
		v[3].texcoord = { uNext, 0.0f };
		v[3].normal = { sinNext, 0.0f, cosNext };

		uint32_t baseIndex = static_cast< uint32_t >( modelData_.vertices.size() );
		for ( int i = 0; i < 4; ++i ) {
			modelData_.vertices.push_back(v[i]);
		}

		// 側面を構成する2つの三角形（時計回り）
		// 三角形1: ① -> ③ -> ②
		modelData_.indices.push_back(baseIndex + 0);
		modelData_.indices.push_back(baseIndex + 2);
		modelData_.indices.push_back(baseIndex + 1);
		// 三角形2: ② -> ③ -> ④
		modelData_.indices.push_back(baseIndex + 1);
		modelData_.indices.push_back(baseIndex + 2);
		modelData_.indices.push_back(baseIndex + 3);
	}

	modelData_.material.textureFilePath = "resources/uvChecker.png";
	CreateBuffers();
}
void Model::Draw(uint32_t instanceCount) {
	// 描くものが無い（動的メッシュが空）なら何もしない
	if ( drawIndexCount_ == 0 ) { return; }
	// 1. コマンドリストを取得する
	// ModelCommon経由でDirectXCommonから取得
	ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

	// 2. 頂点バッファビュー(VBV)の設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 3. インデックスバッファビュー(IBV)の設定
	commandList->IASetIndexBuffer(&indexBufferView_);

	// 4. マテリアル定数バッファの設定 (RootParameter 0番)
	// ※元のObj3d.cppで 0番 に設定していたものです
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());
	// プリミティブトポロジの設定（三角形リスト）
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	// 5. テクスチャの設定 (RootParameter 2番)
	// ※元のObj3d.cppで 2番 に設定していたものです
	if ( textureHandle_.ptr != 0 ) {
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle_);
	}
	
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 6. 描画コマンドの発行
	// インデックスを使って描画
	commandList->DrawIndexedInstanced(drawIndexCount_, instanceCount, 0, 0, 0);


}

void Model::SetColor(const Vector4& color){
	// マテリアルデータがGPUに作られていれば色を上書きする
	if ( materialData_ ) {
		materialData_->color = color;
	}
}
// インデックスを指定して変更（すでにTextureManagerで読み込み済みのものを使う場合に高速）
void Model::SetTexture(uint32_t textureIndex){
	// インデックスを更新
	modelData_.material.textureIndex = textureIndex;

	// SrvManagerから新しいGPUハンドルを取得して、描画用の textureHandle_ を上書きする
	textureHandle_ = modelCommon_->GetDxCommon()->GetSrvManager()->GetGPUDescriptorHandle(textureIndex);
}

// ファイルパスを指定して変更（新しく読み込む、またはパス指定で楽をしたい場合）
void Model::SetTexture(const std::string& textureFilePath){
	// ファイルパスを更新
	modelData_.material.textureFilePath = textureFilePath;

	auto dxCommon = modelCommon_->GetDxCommon();
	auto commandList = dxCommon->GetCommandList();

	// TextureManagerを使ってテクスチャを読み込み、新しいインデックスを取得
	modelData_.material.textureIndex = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
		textureFilePath,
		commandList
	).srvIndex;

	// SrvManagerから新しいGPUハンドルを取得して、描画用の textureHandle_ を上書きする
	textureHandle_ = dxCommon->GetSrvManager()->GetGPUDescriptorHandle(
		modelData_.material.textureIndex
	);
}

void Model::DrawOnly(uint32_t instanceCount){
	if ( drawIndexCount_ == 0 ) { return; }
	ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->IASetIndexBuffer(&indexBufferView_);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	// 描画 (テクスチャのセット処理は飛ばす)
	commandList->DrawIndexedInstanced(drawIndexCount_, instanceCount, 0, 0, 0);
}

Node Model::ParseNode(const aiNode* node) {
	Node result;

	// 行列を直接コピーするのをやめて、SRT に分解する
	aiVector3D   aiScale, aiTranslate;
	aiQuaternion aiRotate;
	node->mTransformation.Decompose(aiScale, aiRotate, aiTranslate);

	// 右手系 → 左手系への変換をしながら transform に入れる
	result.transform.scale = { aiScale.x, aiScale.y, aiScale.z };
	result.transform.rotate = { aiRotate.x, -aiRotate.y, -aiRotate.z, aiRotate.w }; // Y,Z を反転
	result.transform.translate = { -aiTranslate.x, aiTranslate.y, aiTranslate.z };     // X を反転

	// transform から localMatrix を計算する
	result.localMatrix = MakeAffineMatrix(
		result.transform.scale,
		result.transform.rotate,
		result.transform.translate
	);

	result.name = node->mName.C_Str();

	result.children.resize(node->mNumChildren);
	for (uint32_t i = 0; i < node->mNumChildren; ++i) {
		result.children[i] = ParseNode(node->mChildren[i]);
	}

	return result;
}

// モデルファイルの読み込み (拡張子に応じて適切なローダーを呼び出す)
Model::ModelData Model::LoadModelFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;
	OutputDebugStringA(("LoadModelFile: " + filePath + "\n").c_str()); // クラッシュ時にどのモデルか分かるように

	// 1. ファイルを読み込む (三角形化、UV反転、面順反転のオプション付き)。
	//    GenNormals: 法線を持たないモデル（スクリプト生成のOBJ等）にはフラット法線を自動生成する。
	//    これが無いと Release では assert が消えて mNormals(null) を読みアクセス違反で落ちる
	const aiScene* scene = importer.ReadFile(filePath.c_str(),
		aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_GenNormals);
	assert(scene != nullptr && scene->HasMeshes()); // 読み込めない、またはメッシュがない場合はエラー

	// 2. メッシュの解析
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals()); // 法線がないモデルは今回は非対応
		assert(mesh->HasTextureCoords(0)); // UVがないモデルは今回は非対応


		// --- スキンデータの収集 ---
		std::vector<VertexInfluence> influences(mesh->mNumVertices);
		// ボーンがある場合は、頂点ごとの影響データを収集
		if (mesh->HasBones()) {
			// 各ボーンについて
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
				aiBone* bone = mesh->mBones[boneIndex];

				// ボーン名を取得
				std::string boneName = bone->mName.C_Str();

				// 全ボーン名を一意なリストに登録し、そのグローバルインデックスを取得する
				auto it = std::find(modelData.boneOrder.begin(), modelData.boneOrder.end(), boneName);
				uint32_t globalBoneIndex = 0;
				if (it == modelData.boneOrder.end()) {
					globalBoneIndex = static_cast<uint32_t>(modelData.boneOrder.size());
					modelData.boneOrder.push_back(boneName);
				} else {
					globalBoneIndex = static_cast<uint32_t>(std::distance(modelData.boneOrder.begin(), it));
				}

				// mOffsetMatrix（バインドポーズの逆行列）を自分たちの Matrix4x4 に変換して保存
				aiMatrix4x4& m = bone->mOffsetMatrix;
				Matrix4x4 invBindPose;
				// Assimp の行列は行優先メモリ配置だが、DirectX Math の行ベクトル演算に合わせるため転置してコピー
				invBindPose.m[0][0] = m.a1; invBindPose.m[0][1] = m.b1; invBindPose.m[0][2] = m.c1; invBindPose.m[0][3] = m.d1;
				invBindPose.m[1][0] = m.a2; invBindPose.m[1][1] = m.b2; invBindPose.m[1][2] = m.c2; invBindPose.m[1][3] = m.d2;
				invBindPose.m[2][0] = m.a3; invBindPose.m[2][1] = m.b3; invBindPose.m[2][2] = m.c3; invBindPose.m[2][3] = m.d3;
				invBindPose.m[3][0] = m.a4; invBindPose.m[3][1] = m.b4; invBindPose.m[3][2] = m.c4; invBindPose.m[3][3] = m.d4;

				// 右手系 → 左手系への変換をしながら保存
				invBindPose.m[0][1] *= -1.0f;
				invBindPose.m[0][2] *= -1.0f;
				invBindPose.m[0][3] *= -1.0f;
				invBindPose.m[1][0] *= -1.0f;
				invBindPose.m[2][0] *= -1.0f;
				invBindPose.m[3][0] *= -1.0f;


				modelData.inverseBindPoseMap[boneName] = invBindPose;


				for (uint32_t wi = 0; wi < bone->mNumWeights; ++wi) {
					uint32_t vid = bone->mWeights[wi].mVertexId;
					float    weight = bone->mWeights[wi].mWeight;

					// 空きスロット（weight==0）を探して追加（最大4つ）
					for (int slot = 0; slot < 4; ++slot) {
						if (influences[vid].weights[slot] == 0.0f) {
							influences[vid].weights[slot] = weight;
							influences[vid].jointIndices[slot] = static_cast<int32_t>(globalBoneIndex);
							break;
						}
					}
				}
			}
		}

		// 頂点の解析
		uint32_t baseVertex = static_cast<uint32_t>(modelData.vertices.size());

		for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
			aiVector3D& position = mesh->mVertices[vertexIndex];
			aiVector3D& normal = mesh->mNormals[vertexIndex];
			aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

			VertexData vertex;
			// 座標、法線、UVを自分たちの構造体に詰め替える
			vertex.position = { position.x, position.y, position.z, 1.0f };
			vertex.normal = { normal.x, normal.y, normal.z };
			vertex.texcoord = { texcoord.x, texcoord.y };
			vertex.influence = influences[vertexIndex];

			// もしボーン影響が全くない頂点なら、ルート（またはインデックス0）に100%影響とする
			if (vertex.influence.weights[0] == 0.0f && vertex.influence.weights[1] == 0.0f &&
				vertex.influence.weights[2] == 0.0f && vertex.influence.weights[3] == 0.0f) {
				vertex.influence.weights[0] = 1.0f;
				vertex.influence.jointIndices[0] = 0;
			}

			// DirectX(左手系)に合わせるためにX軸を反転
			vertex.position.x *= -1.0f;
			vertex.normal.x *= -1.0f;

			// ウェイトの正規化 (合計が1.0にならない場合への対処)
			float weightSum = vertex.influence.weights[0] + vertex.influence.weights[1] + 
							  vertex.influence.weights[2] + vertex.influence.weights[3];
			if (weightSum > 0.0f) {
				vertex.influence.weights[0] /= weightSum;
				vertex.influence.weights[1] /= weightSum;
				vertex.influence.weights[2] /= weightSum;
				vertex.influence.weights[3] /= weightSum;
			}

			// 頂点データを追加
			modelData.vertices.push_back(vertex);
		}

		// 3. Face(面)の解析
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3); // オプションで三角形化しているので必ず3になるはず

			// インデックスデータを追加
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				modelData.indices.push_back(baseVertex + face.mIndices[element]);
			}
		}
	}

	// 5. Material(マテリアル)の解析
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];

		// テクスチャ（ディフューズマップ）があるか確認
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);

			// パスを結合して自分たちの構造体に保存
			modelData.material.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
			break; // 複数のマテリアルがあっても、一旦最初のものだけ使う
		}
	}
	// ノード階層の解析
	modelData.rootNode = ParseNode(scene->mRootNode);


	return modelData;
}


// モデルの頂点座標を中心（原点）に合わせる
void Model::AdjustModelCenter(){

	// 1. すべての頂点の合計を求める
	Vector3 center = { 0.0f, 0.0f, 0.0f };
	for ( const auto& v : modelData_.vertices ) {
		center.x += v.position.x;
		center.y += v.position.y;
		center.z += v.position.z;
	}

	// 2. 頂点数で割って中心座標を求める
	float vertexCount = static_cast< float >( modelData_.vertices.size() );
	if ( vertexCount > 0 ) {
		center.x /= vertexCount;
		center.y /= vertexCount;
		center.z /= vertexCount;
	}

	// 3. すべての頂点座標から中心座標を引く
	for ( auto& v : modelData_.vertices ) {
		v.position.x -= center.x;
		v.position.y -= center.y;
		v.position.z -= center.z;
	}




}
// バッファの作成をまとめた関数
void Model::CreateBuffers(){

	auto dxCommon = modelCommon_->GetDxCommon();
	auto commandList = dxCommon->GetCommandList();
	auto resourceFactory = dxCommon->GetResourceFactory();

		// SRV作成とインデックス取得
	modelData_.material.textureIndex = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
		modelData_.material.textureFilePath,
		commandList
	).srvIndex;

	// テクスチャハンドルを取得
	textureHandle_ = dxCommon->GetSrvManager()->GetGPUDescriptorHandle(
		modelData_.material.textureIndex
	);


	// 2. 頂点リソースを作る
	vertexResource_ = resourceFactory->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
	assert(vertexResource_ != nullptr);

	// VBV
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// データ書き込み
	VertexData* vertexMap = nullptr;
	vertexResource_->Map(0, nullptr, reinterpret_cast< void** >( &vertexMap ));
	std::copy(modelData_.vertices.begin(), modelData_.vertices.end(), vertexMap);
	
	// 3. インデックスリソースを作る
	indexResource_ = resourceFactory->CreateBufferResource(sizeof(uint32_t) * modelData_.indices.size());
	assert(indexResource_ != nullptr);

	// IBV
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * modelData_.indices.size());
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	// データ書き込み
	uint32_t* indexMap = nullptr;
	indexResource_->Map(0, nullptr, reinterpret_cast< void** >( &indexMap ));
	std::copy(modelData_.indices.begin(), modelData_.indices.end(), indexMap);
	
	// 4. マテリアル用のリソースを作る
	materialResource_ = resourceFactory->CreateBufferResource(sizeof(Material));
	assert(materialResource_ != nullptr);

	// データ書き込み（ここは書き換え頻度が高いのでMapしっぱなしでもOKですが、今回はUnmapしない実装にします）
	// クラスメンバに Material* materialData_ がある前提です
	materialResource_->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));

	// デフォルト値
	materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData_->enableLighting = true;
	materialData_->uvTransform = MakeIdentity4x4();
	materialData_->shininess = 32.0f;
	materialData_->emissive = 0.0f;
	materialData_->environmentCoefficient = 0.0f;

	// 静的モデルの描画数はここで確定（Draw は drawIndexCount_ を参照する）
	drawIndexCount_ = static_cast< uint32_t >( modelData_.indices.size() );

}

// 動的メッシュとして初期化：容量ぶんのバッファを一度だけ確保して Map を保持する
void Model::InitializeDynamic(ModelCommon* modelCommon, uint32_t vertexCapacity, uint32_t indexCapacity,
                              const std::string& textureFilePath){
	modelCommon_ = modelCommon;

	// テクスチャ未指定の保険（空パスのロードは起動時 assert になる）
	modelData_.material.textureFilePath = textureFilePath.empty() ? "resources/uvChecker.png" : textureFilePath;

	auto dxCommon = modelCommon_->GetDxCommon();
	auto resourceFactory = dxCommon->GetResourceFactory();

	// テクスチャ（呼び出し側が先読みしておくこと。ここではキャッシュヒット前提）
	modelData_.material.textureIndex = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
		modelData_.material.textureFilePath,
		dxCommon->GetCommandList()
	).srvIndex;
	textureHandle_ = dxCommon->GetSrvManager()->GetGPUDescriptorHandle(modelData_.material.textureIndex);

	// 頂点/インデックスは容量ぶんを確保して Map しっぱなしにする
	vertexCapacity_ = ( std::max )( vertexCapacity, 3u );
	indexCapacity_  = ( std::max )( indexCapacity, 3u );

	vertexResource_ = resourceFactory->CreateBufferResource(sizeof(VertexData) * vertexCapacity_);
	assert(vertexResource_ != nullptr);
	vertexResource_->Map(0, nullptr, reinterpret_cast< void** >( &vertexMap_ ));
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertexCapacity_);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	indexResource_ = resourceFactory->CreateBufferResource(sizeof(uint32_t) * indexCapacity_);
	assert(indexResource_ != nullptr);
	indexResource_->Map(0, nullptr, reinterpret_cast< void** >( &indexMap_ ));
	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * indexCapacity_);
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	// マテリアル（静的パスと同じ既定値）
	materialResource_ = resourceFactory->CreateBufferResource(sizeof(Material));
	assert(materialResource_ != nullptr);
	materialResource_->Map(0, nullptr, reinterpret_cast< void** >( &materialData_ ));
	materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData_->enableLighting = true;
	materialData_->uvTransform = MakeIdentity4x4();
	materialData_->shininess = 32.0f;
	materialData_->emissive = 0.0f;
	materialData_->environmentCoefficient = 0.0f;

	drawIndexCount_ = 0; // UpdateMesh されるまで何も描かない
}

// 動的メッシュの内容を差し替える（容量超過時のみ2倍に伸ばして再確保）
void Model::UpdateMesh(const std::vector<VertexData>& vertices, const std::vector<uint32_t>& indices){
	auto resourceFactory = modelCommon_->GetDxCommon()->GetResourceFactory();

	// 容量超過は2倍ずつ伸ばして作り直す（稀。毎フレーム WaitForGPU 運用なのでフレーム間なら安全）
	if ( vertices.size() > vertexCapacity_ ) {
		while ( vertexCapacity_ < vertices.size() ) { vertexCapacity_ *= 2; }
		vertexResource_ = resourceFactory->CreateBufferResource(sizeof(VertexData) * vertexCapacity_);
		assert(vertexResource_ != nullptr);
		vertexResource_->Map(0, nullptr, reinterpret_cast< void** >( &vertexMap_ ));
		vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
		vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertexCapacity_);
	}
	if ( indices.size() > indexCapacity_ ) {
		while ( indexCapacity_ < indices.size() ) { indexCapacity_ *= 2; }
		indexResource_ = resourceFactory->CreateBufferResource(sizeof(uint32_t) * indexCapacity_);
		assert(indexResource_ != nullptr);
		indexResource_->Map(0, nullptr, reinterpret_cast< void** >( &indexMap_ ));
		indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
		indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * indexCapacity_);
	}

	if ( !vertices.empty() ) { std::memcpy(vertexMap_, vertices.data(), sizeof(VertexData) * vertices.size()); }
	if ( !indices.empty() )  { std::memcpy(indexMap_,  indices.data(),  sizeof(uint32_t)   * indices.size()); }
	drawIndexCount_ = static_cast< uint32_t >( indices.size() );
}
