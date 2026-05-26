#include "Obj3d.h"

// --- 標準ライブラリ ---
#include <cassert>
#include <cmath>

// --- エンジン側のファイル ---
#include "Obj3dCommon.h" 
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "Animation.h" 
#include "engine/math/QuaternionMath.h"

using namespace MatrixMath;

std::unique_ptr<Obj3d> Obj3d::Create(const std::string& modelName, const Vector3& translate, const Vector3& rotate, const Vector3& scale) {

	// 1. モデルを検索
	Model* model = ModelManager::GetInstance()->FindModel(modelName);

	// モデルが見つからなかった場合は nullptr を返す
	if (!model) {
		return nullptr;
	}

	// 2. make_unique で生成（new の代わり）
	std::unique_ptr<Obj3d> obj3d = std::make_unique<Obj3d>();

	// 3. 初期化
	obj3d->Initialize(model);

	// 4. 各種パラメータをセット
	obj3d->SetTranslation(translate);
	obj3d->SetRotation(rotate);
	obj3d->SetScale(scale);

	// 5. そのまま返す（moveされるので所有権が移ります）
	return obj3d;
}


void Obj3d::Initialize(Model* model){

	// 引数の保存
	this->model_ = model;
	// Obj3dCommonの取得
	this->obj3dCommon_ = Obj3dCommon::GetInstance();
	this->model_ = model;

	auto dxCommon = obj3dCommon_->GetDxCommon();

	// ---------------------------------------------------------
	// WVP用のリソースを作る (場所の情報なので Obj3d が持つ)
	// ---------------------------------------------------------
	wvpResource_ = dxCommon->GetResourceFactory()->CreateBufferResource(sizeof(TransformationMatrix));
	assert(wvpResource_ != nullptr);

	// データを書き込むためのアドレスを取得
	wvpResource_->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatrixData_ ));

	// 単位行列を初期値として書き込んでおく
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();



	// ---------------------------------------------------------
	// ディゾルブエフェクト用のリソースを作る
	// ---------------------------------------------------------
	dissolveResource_ = obj3dCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(DissolveData));
	dissolveResource_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveData_));

	// 初期値は「0.0」（溶けていない状態）にしておく
	dissolveData_->threshold = 0.0f;

	// 初期のディゾルブ境界色（とりあえず赤）
	dissolveData_->edgeColor = { 1.0f, 0.2f, 0.05f };

	// paddingも0で初期化
	dissolveData_->padding = 0.0f;
	materialOverrideResource_.Reset();
	materialOverrideData_ = nullptr;
	useMaterialOverride_ = false;

	// ---------------------------------------------------------
	// Transform変数の初期化
	// ---------------------------------------------------------
	scale_ = { 1.0f, 1.0f, 1.0f };
	rotation_ = { 0.0f, 0.0f, 0.0f };
	translate_ = { 0.0f, 0.0f, 0.0f };
}

void Obj3d::Update(){

	// Transform変数から行列計算用の構造体へコピー
	transform.translate = translate_;
	transform.rotate = rotation_;
	transform.scale = scale_;

	auto dxCommon = obj3dCommon_->GetDxCommon();

	// ワールド行列の計算 (SRT)
	Matrix4x4 worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);
	if ( currentAnimation_ && !currentAnimation_->nodeAnimations.empty() ) {
		// 1. 時間を進める (60FPS固定として、毎フレーム 1/60秒 進める)
		animationTime_ += 1.0f / 60.0f;

		// 2. アニメーションをループさせる (全体の尺で割った余りを出す)
		animationTime_ = std::fmod(animationTime_, currentAnimation_->duration);

		// 3. 対象の骨（ノード）の動きを取得 (今回は一番最初の骨を強制的に使う)
		NodeAnimation& rootNodeAnimation = currentAnimation_->nodeAnimations.begin()->second;

		// 4. 今の時間の「位置・回転・スケール」を CalculateValue で計算！
		Vector3 animTranslate = CalculateValue(rootNodeAnimation.translate.keyframes, animationTime_);
		Quaternion animRotate = CalculateValue(rootNodeAnimation.rotate.keyframes, animationTime_);
		Vector3 animScale = CalculateValue(rootNodeAnimation.scale.keyframes, animationTime_);

		// 5. 計算した値から、それぞれ行列を作る
		Matrix4x4 scaleMat = MakeIdentity4x4();
		scaleMat.m[0][0] = animScale.x;
		scaleMat.m[1][1] = animScale.y;
		scaleMat.m[2][2] = animScale.z;

		Matrix4x4 rotMat = QuaternionMath::MakeRotateMatrix(animRotate);

		Matrix4x4 transMat = MakeIdentity4x4();
		transMat.m[3][0] = animTranslate.x;
		transMat.m[3][1] = animTranslate.y;
		transMat.m[3][2] = animTranslate.z;

		// 6. S × R × T を掛け合わせてワールド行列にする！
		worldMatrix = Multiply(Multiply(scaleMat, rotMat), transMat);

	} else {
		// =======================================================
		// アニメーションが無い場合は、今まで通りの変形を使う
		// =======================================================
		transform.translate = translate_;
		transform.rotate = rotation_;
		transform.scale = scale_;
		worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);
	}

	// ワールド × ビュー × プロジェクション行列
	Matrix4x4 worldViewProjectionMatrix;

	// カメラがあれば、カメラの行列を使う
	if ( camera_ ) {
		// カメラから「ビュー・プロジェクション行列」をもらう
		const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();

		// ワールド行列 × (ビュー × プロジェクション)
		worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
	} else {
		// カメラがないときはとりあえずワールド行列だけ入れておく
		worldViewProjectionMatrix = worldMatrix;
	}

	// ワールド行列の逆行列
	Matrix4x4 worldInverse = Inverse(worldMatrix);
	// 逆行列の転置行列
	Matrix4x4 worldInverseTranspose = Transpose(worldInverse);

	// 定数バッファへ転送
	transformationMatrixData_->World = worldMatrix;
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
	transformationMatrixData_->WorldInverseTranspose = worldInverseTranspose;

	matrixData_.World = worldMatrix;
	matrixData_.WVP = worldViewProjectionMatrix;
	matrixData_.WorldInverseTranspose = worldInverseTranspose;
}

void Obj3d::Draw(){

	// コマンドリスト取得
	ID3D12GraphicsCommandList* commandList =
		obj3dCommon_->GetDxCommon()->GetCommandList();
	assert(commandList != nullptr);

	// 1. 座標情報の転送 (WVP) -> RootParameter[1]
	// ※PipelineManagerの設定（RootSignature）と番号を合わせてください
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

	// 2. ライト情報の転送 -> RootParameter[3]
	commandList->SetGraphicsRootConstantBufferView(
		3, obj3dCommon_->GetLightResource()->GetGPUVirtualAddress()
	);

	if ( camera_ ) {
		commandList->SetGraphicsRootConstantBufferView(
			4, camera_->GetCameraResource()->GetGPUVirtualAddress()
		);
	}
	// 点光源の転送 -> RootParameter[5]
	commandList->SetGraphicsRootConstantBufferView(5, Obj3dCommon::GetInstance()->GetPointLightResource()->GetGPUVirtualAddress());
	// スポットライトの転送 -> RootParameter[6]
	commandList->SetGraphicsRootConstantBufferView(6, Obj3dCommon::GetInstance()->GetSpotLightResource()->GetGPUVirtualAddress());
	// ディゾルブエフェクトの転送 -> RootParameter[8]
	commandList->SetGraphicsRootConstantBufferView(8, dissolveResource_->GetGPUVirtualAddress());
	// ノイズ画像を転送->RootParameter[7]
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(7, noiseTextureIndex_);

	// 3. モデルの描画処理を呼び出す
	// (ここで頂点、インデックス、マテリアル、テクスチャの設定とDrawCallが行われる)
	if ( model_ ) {
		if (useMaterialOverride_ && materialOverrideResource_) {
			// 全マテリアル一括上書き（既存の SetColor(color) を使った場合）
			model_->Draw(1, materialOverrideResource_->GetGPUVirtualAddress());
		} else if (!perMaterialOverrideResources_.empty()) {
			// per-material 上書き（SetColor(color, materialIndex) を使った場合）
			std::vector<D3D12_GPU_VIRTUAL_ADDRESS> addrs(perMaterialOverrideResources_.size(), 0);
			for (size_t i = 0; i < perMaterialOverrideResources_.size(); ++i) {
				if (i < perMaterialOverrideActive_.size() && perMaterialOverrideActive_[i]
					&& perMaterialOverrideResources_[i]) {
					addrs[i] = perMaterialOverrideResources_[i]->GetGPUVirtualAddress();
				}
			}
			model_->Draw(1, addrs);
		} else {
			model_->Draw(1, 0);
		}
	}
}

void Obj3d::SetDissolveThreshold(float threshold) {
	if (dissolveData_) {
		dissolveData_->threshold = threshold;
	}
}

void Obj3d::SetDissolveColor(const Vector3& color) {
	if (dissolveData_) {
		dissolveData_->edgeColor = color;
	}
}

void Obj3d::SetNoiseTexture(uint32_t textureIndex) {
	noiseTextureIndex_ = textureIndex;
}

void Obj3d::SetColor(const Vector4& color) {
	if (!model_) {
		return;
	}

	if (!materialOverrideResource_) {
		materialOverrideResource_ = obj3dCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(Model::Material));
		materialOverrideResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialOverrideData_));

		if (materialOverrideData_) {
			if (Model::Material* source = model_->GetMaterial()) {
				*materialOverrideData_ = *source;
			}
			else {
				materialOverrideData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
				materialOverrideData_->enableLighting = true;
				materialOverrideData_->uvTransform = MatrixMath::MakeIdentity4x4();
				materialOverrideData_->shininess = 32.0f;
				materialOverrideData_->emissive = 0.0f;
			}
		}
	}

	if (materialOverrideData_) {
		materialOverrideData_->color = color;
		useMaterialOverride_ = true;
	}
}

// per-material バッファの作成・初期化をまとめたヘルパー（内部使用）
void Obj3d::EnsurePerMaterialBuffer(uint32_t materialIndex) {
	// 必要に応じてベクターを拡張
	if (materialIndex >= perMaterialOverrideResources_.size()) {
		perMaterialOverrideResources_.resize(materialIndex + 1);
		perMaterialOverrideDatas_.resize(materialIndex + 1, nullptr);
		perMaterialOverrideActive_.resize(materialIndex + 1, false);
	}

	// 初回のみバッファを作成
	if (!perMaterialOverrideResources_[materialIndex]) {
		perMaterialOverrideResources_[materialIndex] =
			obj3dCommon_->GetDxCommon()->GetResourceFactory()->CreateBufferResource(sizeof(Model::Material));
		perMaterialOverrideResources_[materialIndex]->Map(
			0, nullptr, reinterpret_cast<void**>(&perMaterialOverrideDatas_[materialIndex]));

		// 元のマテリアルの設定値（ライティング・UV・shininess 等）をコピー
		if (perMaterialOverrideDatas_[materialIndex]) {
			if (Model::Material* source = model_->GetMaterial(materialIndex)) {
				*perMaterialOverrideDatas_[materialIndex] = *source;
			} else {
				perMaterialOverrideDatas_[materialIndex]->color = { 1.0f, 1.0f, 1.0f, 1.0f };
				perMaterialOverrideDatas_[materialIndex]->enableLighting = true;
				perMaterialOverrideDatas_[materialIndex]->uvTransform = MatrixMath::MakeIdentity4x4();
				perMaterialOverrideDatas_[materialIndex]->shininess = 32.0f;
				perMaterialOverrideDatas_[materialIndex]->emissive = 0.0f;
			}
		}
	}
}

// マテリアルごとに個別の色を上書きする（マルチマテリアル用）
void Obj3d::SetColor(const Vector4& color, uint32_t materialIndex) {
	if (!model_) return;
	if (materialIndex >= model_->GetMaterialCount()) return;

	EnsurePerMaterialBuffer(materialIndex);

	if (perMaterialOverrideDatas_[materialIndex]) {
		perMaterialOverrideDatas_[materialIndex]->color = color;
		perMaterialOverrideActive_[materialIndex] = true;
	}
}

// マテリアルごとに発光強度を設定する（マルチマテリアル用）
void Obj3d::SetEmissive(float emissive, uint32_t materialIndex) {
	if (!model_) return;
	if (materialIndex >= model_->GetMaterialCount()) return;

	EnsurePerMaterialBuffer(materialIndex);

	if (perMaterialOverrideDatas_[materialIndex]) {
		perMaterialOverrideDatas_[materialIndex]->emissive = emissive;
		perMaterialOverrideActive_[materialIndex] = true;
	}
}

// 特定マテリアルの個別上書きを解除する
void Obj3d::ClearMaterialOverride(uint32_t materialIndex) {
	if (materialIndex < perMaterialOverrideActive_.size()) {
		perMaterialOverrideActive_[materialIndex] = false;
	}
}

void Obj3d::ClearColorOverride() {
	useMaterialOverride_ = false;
}

void Obj3d::PlayAnimation(Animation* animation){
	currentAnimation_ = animation;
	animationTime_ = 0.0f; // 再生時間をリセット
}
