#include "Model.h"
// --- 標準ライブラリ ---
#include <cassert>
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
	// 4. マテリアルデータの読み込み
// テクスチャパスが空のマテリアルにデフォルトを設定
	for ( auto& mat : modelData_.materials ) {
		if ( mat.textureFilePath.empty() ) {
			mat.textureFilePath = "resources/uvChecker.png";
		}
	}
	// マテリアルが一つもない場合はデフォルトを追加
	if ( modelData_.materials.empty() ) {
		MaterialData defaultMat;
		defaultMat.textureFilePath = "resources/uvChecker.png";
		modelData_.materials.push_back(defaultMat);
	}
	// サブメッシュが一つもない場合は全インデックスをまとめて一つのサブメッシュとする
	if ( modelData_.subMeshes.empty() ) {
		SubMesh defaultSub;
		defaultSub.indexOffset = 0;
		defaultSub.indexCount = static_cast< uint32_t >( modelData_.indices.size() );
		defaultSub.materialIndex = 0;
		modelData_.subMeshes.push_back(defaultSub);
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
	{
		MaterialData matData;
		matData.textureFilePath = "resources/monsterBall.png";
		modelData_.materials.push_back(matData);
	}
	// 全インデックスを一つのサブメッシュとして登録
	{
		SubMesh subMesh;
		subMesh.indexOffset = 0;
		subMesh.indexCount = static_cast<uint32_t>(modelData_.indices.size());
		subMesh.materialIndex = 0;
		modelData_.subMeshes.push_back(subMesh);
	}

	CreateBuffers(); // バッファの作成
}

void Model::InitializeRing(ModelCommon* modelCommon, int subdivision) {
	this->modelCommon_ = modelCommon;
	modelData_ = {};

	const int segmentCount = (std::max)(subdivision, 8);
	const float outerRadius = 1.0f;
	const float innerRadius = 0.88f;
	const float angleStep = std::numbers::pi_v<float> * 2.0f / static_cast<float>(segmentCount);

	for (int index = 0; index <= segmentCount; ++index) {
		const float angle = angleStep * static_cast<float>(index);
		const float cosAngle = std::cos(angle);
		const float sinAngle = std::sin(angle);

		VertexData outerVertex{};
		outerVertex.position = { cosAngle * outerRadius, 0.0f, sinAngle * outerRadius, 1.0f };
		outerVertex.texcoord = { 1.0f, static_cast<float>(index) / static_cast<float>(segmentCount) };
		outerVertex.normal = { 0.0f, 1.0f, 0.0f };
		modelData_.vertices.push_back(outerVertex);

		VertexData innerVertex{};
		innerVertex.position = { cosAngle * innerRadius, 0.0f, sinAngle * innerRadius, 1.0f };
		innerVertex.texcoord = { 0.0f, static_cast<float>(index) / static_cast<float>(segmentCount) };
		innerVertex.normal = { 0.0f, 1.0f, 0.0f };
		modelData_.vertices.push_back(innerVertex);
	}

	for (int index = 0; index < segmentCount; ++index) {
		const uint32_t outerCurrent = static_cast<uint32_t>(index * 2);
		const uint32_t innerCurrent = outerCurrent + 1;
		const uint32_t outerNext = outerCurrent + 2;
		const uint32_t innerNext = outerCurrent + 3;

		modelData_.indices.push_back(outerCurrent);
		modelData_.indices.push_back(innerCurrent);
		modelData_.indices.push_back(outerNext);

		modelData_.indices.push_back(outerNext);
		modelData_.indices.push_back(innerCurrent);
		modelData_.indices.push_back(innerNext);
	}

	{
		MaterialData matData;
		matData.textureFilePath = "resources/white1x1.png";
		modelData_.materials.push_back(matData);
	}
	{
		SubMesh subMesh;
		subMesh.indexOffset = 0;
		subMesh.indexCount = static_cast<uint32_t>(modelData_.indices.size());
		subMesh.materialIndex = 0;
		modelData_.subMeshes.push_back(subMesh);
	}

	CreateBuffers();
}

void Model::Draw(uint32_t instanceCount, D3D12_GPU_VIRTUAL_ADDRESS materialAddress) {
	// 1. コマンドリストを取得する
	// ModelCommon経由でDirectXCommonから取得
	ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

	// 2. 頂点バッファビュー(VBV)の設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 3. インデックスバッファビュー(IBV)の設定
	commandList->IASetIndexBuffer(&indexBufferView_);

	// プリミティブトポロジの設定（三角形リスト）
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 4. サブメッシュごとにマテリアルとテクスチャを切り替えながら描画
	for (const auto& subMesh : modelData_.subMeshes) {
		uint32_t matIdx = subMesh.materialIndex;

		// マテリアル定数バッファの設定 (RootParameter 0番)
		// ※元のObj3d.cppで 0番 に設定していたものです
		D3D12_GPU_VIRTUAL_ADDRESS matAddr = (materialAddress != 0)
			? materialAddress
			: materialResources_[matIdx]->GetGPUVirtualAddress();
		commandList->SetGraphicsRootConstantBufferView(0, matAddr);

		// 5. テクスチャの設定 (RootParameter 2番)
		// ※元のObj3d.cppで 2番 に設定していたものです
		if (textureHandles_[matIdx].ptr != 0) {
			commandList->SetGraphicsRootDescriptorTable(2, textureHandles_[matIdx]);
		}

		// 6. 描画コマンドの発行
		// このサブメッシュ分のインデックスのみ描画
		commandList->DrawIndexedInstanced(
			subMesh.indexCount,   // このサブメッシュのインデックス数
			instanceCount,
			subMesh.indexOffset,  // インデックスバッファの開始オフセット
			0, 0
		);
	}

}

// per-material 上書き用 Draw（Obj3d の個別色変更から呼ばれる）
void Model::Draw(uint32_t instanceCount, const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& perMaterialAddresses) {
	ID3D12GraphicsCommandList* commandList = modelCommon_->GetDxCommon()->GetCommandList();

	// 2. 頂点バッファビュー(VBV)の設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 3. インデックスバッファビュー(IBV)の設定
	commandList->IASetIndexBuffer(&indexBufferView_);

	// プリミティブトポロジの設定（三角形リスト）
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// サブメッシュごとに per-material アドレスを選択して描画
	for (const auto& subMesh : modelData_.subMeshes) {
		uint32_t matIdx = subMesh.materialIndex;

		// per-material 上書きがあればそれを使い、なければ元のマテリアル
		D3D12_GPU_VIRTUAL_ADDRESS matAddr =
			(matIdx < perMaterialAddresses.size() && perMaterialAddresses[matIdx] != 0)
			? perMaterialAddresses[matIdx]
			: materialResources_[matIdx]->GetGPUVirtualAddress();

		// マテリアル定数バッファの設定 (RootParameter 0番)
		commandList->SetGraphicsRootConstantBufferView(0, matAddr);

		// テクスチャの設定 (RootParameter 2番)
		if (textureHandles_[matIdx].ptr != 0) {
			commandList->SetGraphicsRootDescriptorTable(2, textureHandles_[matIdx]);
		}

		// 描画コマンドの発行
		commandList->DrawIndexedInstanced(subMesh.indexCount, instanceCount, subMesh.indexOffset, 0, 0);
	}
}

void Model::SetColor(const Vector4& color, uint32_t materialIndex){
	// マテリアルデータがGPUに作られていれば色を上書きする
	if (materialIndex < materialDatas_.size() && materialDatas_[materialIndex]) {
		materialDatas_[materialIndex]->color = color;
	}
}
// インデックスを指定して変更（すでにTextureManagerで読み込み済みのものを使う場合に高速）
void Model::SetTexture(uint32_t textureIndex, uint32_t materialIndex){
	if (materialIndex >= modelData_.materials.size()) return;

	// インデックスを更新
	modelData_.materials[materialIndex].textureIndex = textureIndex;

	// SrvManagerから新しいGPUハンドルを取得して、描画用の textureHandles_ を上書きする
	textureHandles_[materialIndex] = modelCommon_->GetDxCommon()->GetSrvManager()->GetGPUDescriptorHandle(textureIndex);
}

// ファイルパスを指定して変更（新しく読み込む、またはパス指定で楽をしたい場合）
void Model::SetTexture(const std::string& textureFilePath, uint32_t materialIndex){
	if (materialIndex >= modelData_.materials.size()) return;

	// ファイルパスを更新
	modelData_.materials[materialIndex].textureFilePath = textureFilePath;

	auto dxCommon = modelCommon_->GetDxCommon();
	auto commandList = dxCommon->GetCommandList();

	// TextureManagerを使ってテクスチャを読み込み、新しいインデックスを取得
	modelData_.materials[materialIndex].textureIndex = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
		textureFilePath,
		commandList
	).srvIndex;

	// SrvManagerから新しいGPUハンドルを取得して、描画用の textureHandles_ を上書きする
	textureHandles_[materialIndex] = dxCommon->GetSrvManager()->GetGPUDescriptorHandle(
		modelData_.materials[materialIndex].textureIndex
	);
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

	// 1. ファイルを読み込む (三角形化、UV反転、面順反転のオプション付き)
	const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate);
	assert(scene != nullptr && scene->HasMeshes()); // 読み込めない、またはメッシュがない場合はエラー

	// 2. メッシュの解析
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals()); // 法線がないモデルは今回は非対応
		assert(mesh->HasTextureCoords(0)); // UVがないモデルは今回は非対応

		// サブメッシュの開始位置をここで記録（インデックスを追加する前）
		SubMesh subMesh;
		subMesh.indexOffset = static_cast<uint32_t>(modelData.indices.size());
		subMesh.materialIndex = mesh->mMaterialIndex; // Assimpのマテリアルインデックス

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

		// インデックスを追加し終わったのでindexCountを確定してサブメッシュを登録
		subMesh.indexCount = static_cast<uint32_t>(modelData.indices.size()) - subMesh.indexOffset;
		modelData.subMeshes.push_back(subMesh);
	}

	// 5. Material(マテリアル)の解析
	// Assimpが内部でデフォルト素材を index 0 に挿入することがあるため、
	// 「実際にサブメッシュが参照しているマテリアル」だけを登録し、
	// インデックスを 0 番から詰め直す（リマップ）
	// ※これにより GetMaterial(0) / SetTexture(path,0) が正しく機能する

	// Assimpのマテリアルインデックス → 自分たちのインデックス
	std::vector<uint32_t> assimpToOurIndex(scene->mNumMaterials, UINT32_MAX);

	for (const auto& subMesh : modelData.subMeshes) {
		uint32_t ai_idx = subMesh.materialIndex; // この時点はまだ Assimp のインデックス

		if (assimpToOurIndex[ai_idx] == UINT32_MAX) {
			// 初登場のマテリアル → 自分たちの配列に追加
			assimpToOurIndex[ai_idx] = static_cast<uint32_t>(modelData.materials.size());

			aiMaterial* material = scene->mMaterials[ai_idx];
			MaterialData matData;

			// テクスチャ（ディフューズマップ）があるか確認
			if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
				aiString textureFilePath;
				material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);

				// パスを結合して自分たちの構造体に保存
				matData.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
			}
			modelData.materials.push_back(matData);
		}
	}

	// SubMeshのmaterialIndexをリマップ後のインデックスに変換
	for (auto& subMesh : modelData.subMeshes) {
		subMesh.materialIndex = assimpToOurIndex[subMesh.materialIndex];
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

	// 1. マテリアルの数だけテクスチャ読み込み・定数バッファを作成
	uint32_t matCount = static_cast<uint32_t>(modelData_.materials.size());
	materialResources_.resize(matCount);
	materialDatas_.resize(matCount, nullptr);
	textureHandles_.resize(matCount);

	for (uint32_t i = 0; i < matCount; ++i) {
		// SRV作成とインデックス取得
		modelData_.materials[i].textureIndex = TextureManager::GetInstance()->LoadTextureAndCreateSRV(
			modelData_.materials[i].textureFilePath,
			commandList
		).srvIndex;

		// テクスチャハンドルを取得
		textureHandles_[i] = dxCommon->GetSrvManager()->GetGPUDescriptorHandle(
			modelData_.materials[i].textureIndex
		);

		// マテリアル用のリソースを作る
		materialResources_[i] = resourceFactory->CreateBufferResource(sizeof(Material));
		assert(materialResources_[i] != nullptr);

		// データを書き込むためのポインタを取得（Unmapしない実装）
		materialResources_[i]->Map(0, nullptr, reinterpret_cast<void**>(&materialDatas_[i]));

		// デフォルト値
		materialDatas_[i]->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		materialDatas_[i]->enableLighting = true;
		materialDatas_[i]->uvTransform = MakeIdentity4x4();
		materialDatas_[i]->shininess = 32.0f;
		materialDatas_[i]->emissive = 0.0f;
	}

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

}
