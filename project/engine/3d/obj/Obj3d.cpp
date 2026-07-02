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
#include "engine/base/TimeManager.h"
#include "engine/utils/RenderStats.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "Animation.h" 
#include "engine/math/QuaternionMath.h"
#include "engine/graphics/PipelineManager.h"

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

	// 4-2. 既定（アクティブ）カメラが登録されていれば自動でバインド
	//      （シーンで SetDefaultCamera を1回呼べば、毎回 SetCamera する必要がなくなる。
	//        個別に SetCamera すれば従来どおり上書き可能）
	if ( const Camera* defaultCamera = Obj3dCommon::GetInstance()->GetDefaultCamera() ) {
		obj3d->SetCamera(defaultCamera);
	}

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
	dissolveData_->threshold = 0.0f; //最初は溶けていない状態(0.0)にしておく

	// 初期値は「0.0」（溶けていない状態）にしておく
	dissolveData_->threshold = 0.0f;

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
		// 1. 時間を進める（デルタタイム基準。ポーズ中(timeScale=0)は止まる）
		animationTime_ += Time::GetInstance()->GetDeltaTime();

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
		if ( isBillboard_ && camera_ ) {
			// 全方位ビルボード
			Matrix4x4 scaleMat = MakeScale(transform.scale);
			Matrix4x4 billboardMat = MakeBillboardMatrix(camera_->GetWorldPosition(), transform.translate);
			Matrix4x4 transMat = MakeTranslate(transform.translate);
			worldMatrix = Multiply(Multiply(scaleMat, billboardMat), transMat);
		} else if ( isBillboardY_ && camera_ ) {
			// Y軸ビルボード
			Matrix4x4 scaleMat = MakeScale(transform.scale);
			Matrix4x4 billboardMat = MakeBillboardYMatrix(camera_->GetWorldPosition(), transform.translate);
			Matrix4x4 transMat = MakeTranslate(transform.translate);
			worldMatrix = Multiply(Multiply(scaleMat, billboardMat), transMat);
		} else {
			// 通常の計算
			worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);
		}
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

	// 描画統計に1回計上
	RenderStats::GetInstance()->AddDrawCall();

	// コマンドリスト取得
	ID3D12GraphicsCommandList* commandList =
		obj3dCommon_->GetDxCommon()->GetCommandList();
	assert(commandList != nullptr);

	PipelineManager::GetInstance()->SetPipeline(commandList, pipelineType_);

	// ノードエディタ製の専用PSOがあれば差し替える
	// （ルートシグネチャはObject3D互換なのでバインドはそのまま使える）
	if ( customPipeline_ ) {
		commandList->SetPipelineState(customPipeline_.Get());
	}

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

	uint32_t envIndex = ( skyboxTextureIndex_ != 0 )
		? skyboxTextureIndex_
		: obj3dCommon_->GetEnvironmentTextureSrvIndex();
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(9, envIndex);

	// 3. モデルの描画処理を呼び出す 
	// (ここで頂点、インデックス、マテリアル、テクスチャの設定とDrawCallが行われる)
	if ( model_ ) {
		model_->Draw();
	}
}

void Obj3d::SetDissolveThreshold(float threshold) {
	if (dissolveData_) {
		dissolveData_->threshold = threshold;
	}
}

void Obj3d::SetNoiseTexture(uint32_t textureIndex) {
	noiseTextureIndex_ = textureIndex;
}

void Obj3d::PlayAnimation(Animation* animation){
	currentAnimation_ = animation;
	animationTime_ = 0.0f; // 再生時間をリセット
}