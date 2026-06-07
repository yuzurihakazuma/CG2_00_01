#include "SkinnedObj3d.h"

#include <cassert>
#include <cmath>

#include "Obj3dCommon.h"
#include "engine/3d/model/Model.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"
#include "engine/math/QuaternionMath.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/TimeManager.h"
#include "engine/utils/RenderStats.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/PipelineType.h"
#include <externals/imgui/imgui.h>

using namespace MatrixMath;

// --------------------------------------------------
// 静的生成関数
// --------------------------------------------------
std::unique_ptr<SkinnedObj3d> SkinnedObj3d::Create(
	const std::string& modelName,
	const std::string& directoryPath,
	const std::string& animFilename
){
	// ModelManager から登録済みモデルを探す
	Model* model = ModelManager::GetInstance()->FindModel(modelName);
	if ( !model ) { return nullptr; }

	auto obj = std::make_unique<SkinnedObj3d>();
	obj->Initialize(model, directoryPath, animFilename);

	// 既定（アクティブ）カメラが登録されていれば自動でバインド（個別 SetCamera で上書き可）
	if ( const Camera* defaultCamera = Obj3dCommon::GetInstance()->GetDefaultCamera() ) {
		obj->SetCamera(defaultCamera);
	}
	return obj;
}

// --------------------------------------------------
// 初期化
// --------------------------------------------------
void SkinnedObj3d::Initialize(
	Model* model,
	const std::string& directoryPath,
	const std::string& animFilename
){
	model_ = model;
	obj3dCommon_ = Obj3dCommon::GetInstance();

	auto dxCommon = obj3dCommon_->GetDxCommon();

	// WVP 定数バッファの作成（Obj3d と同じ）
	wvpResource_ = dxCommon->GetResourceFactory()->CreateBufferResource(sizeof(TransformationMatrix));
	assert(wvpResource_ != nullptr);
	wvpResource_->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatrixData_ ));
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();

	// ディゾルブ定数バッファの作成（Obj3d と同じ）
	dissolveResource_ = dxCommon->GetResourceFactory()->CreateBufferResource(sizeof(DissolveData));
	dissolveResource_->Map(0, nullptr, reinterpret_cast< void** >( &dissolveData_ ));
	dissolveData_->threshold = 0.0f;

	// スケルトンの作成
	skeleton_ = CreateSkeleton(model_->GetRootNode());

	// スキンクラスターの作成（inverseBindPose も設定される）
	skinCluster_ = CreateSkinCluster(
		skeleton_,
		model_->GetInverseBindPoseMap(),
		model_->GetBoneOrder(),
		dxCommon
	);

	// アニメーションの読み込み
	animation_ = LoadAnimationFromFile(directoryPath, animFilename);
}

// --------------------------------------------------
// 更新（毎フレーム）
// --------------------------------------------------
void SkinnedObj3d::Update(){

	if ( !isPause_ ) {
		// --- アニメーション時間を進める（デルタタイム基準）---
		animationTime_ += Time::GetInstance()->GetDeltaTime();
		if ( isLoop_ ) {
			animationTime_ = std::fmod(animationTime_, animation_.duration);
		} else {
			if ( animationTime_ > animation_.duration ) {
				animationTime_ = animation_.duration;
			}
		}
		// --- スケルトンにアニメーションを適用して行列を更新 ---
		UpdateSkeleton(skeleton_, animation_, animationTime_);
	} else {
		// --- 手動操作モード（ここが重要！） ---
		for ( auto& joint : skeleton_.joints ) {
			// 1. いじった Transform から LocalMatrix を作り直す
			joint.localMatrix = MakeAffineMatrix(joint.transform.scale, joint.transform.rotate, joint.transform.translate);

			// 2. 親の行列と掛け合わせて SkeletonSpaceMatrix を作り直す
			if ( joint.parent ) { // ※ Skeleton.h の構造によって joint.parentIndex の場合もあります
				joint.skeletonSpaceMatrix = Multiply(joint.localMatrix, skeleton_.joints[*joint.parent].skeletonSpaceMatrix);
			} else {
				joint.skeletonSpaceMatrix = joint.localMatrix;
			}
		}
	}

	// --- スケルトンにアニメーションを適用して行列を更新 ---
	UpdateSkeleton(skeleton_, animation_, animationTime_);

	// --- MatrixPalette を計算して GPU バッファに書き込む ---
	UpdateSkinCluster(skinCluster_, skeleton_, model_->GetBoneOrder());

	// --- WVP 行列の計算（Obj3d と同じ処理） ---
	Matrix4x4 worldMatrix = MakeAffine(scale_, rotation_, translate_);

	Matrix4x4 worldViewProjectionMatrix;
	if ( camera_ ) {
		const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
		worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
	} else {
		worldViewProjectionMatrix = worldMatrix;
	}

	Matrix4x4 worldInverseTranspose = Transpose(Inverse(worldMatrix));

	transformationMatrixData_->World = worldMatrix;
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
	transformationMatrixData_->WorldInverseTranspose = worldInverseTranspose;
}

// --------------------------------------------------
// 描画
// --------------------------------------------------
void SkinnedObj3d::Draw(){
	// 描画統計に1回計上
	RenderStats::GetInstance()->AddDrawCall();

	ID3D12GraphicsCommandList* commandList = obj3dCommon_->GetDxCommon()->GetCommandList();
	// SkinnedObj3d::Draw() の先頭付近
	assert(skinCluster_.srvIndex != 0 && "SkinCluster SRV index is invalid!");
	assert(skinCluster_.srvHandle.ptr != 0 && "SkinCluster SRV handle is null!");

	// スキニング専用パイプラインをセット
	PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::SkinningObject3D);

	// [1] WVP 定数バッファ
	commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());

	// [3] 平行光源
	commandList->SetGraphicsRootConstantBufferView(3, obj3dCommon_->GetLightResource()->GetGPUVirtualAddress());

	// [4] カメラ
	if ( camera_ ) {
		commandList->SetGraphicsRootConstantBufferView(4, camera_->GetCameraResource()->GetGPUVirtualAddress());
	}

	// [5] 点光源
	commandList->SetGraphicsRootConstantBufferView(5, Obj3dCommon::GetInstance()->GetPointLightResource()->GetGPUVirtualAddress());

	// [6] スポットライト
	commandList->SetGraphicsRootConstantBufferView(6, Obj3dCommon::GetInstance()->GetSpotLightResource()->GetGPUVirtualAddress());

	// [7] ノイズテクスチャ
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(7, noiseTextureIndex_);

	// [8] ディゾルブ
	commandList->SetGraphicsRootConstantBufferView(8, dissolveResource_->GetGPUVirtualAddress());

	// [9] MatrixPalette（スキニング用・ここが Obj3d との違い）
	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(9, skinCluster_.srvIndex);

	SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(10, skyboxTextureIndex_);

	// モデル描画（[0] マテリアル・[2] テクスチャ・DrawCall は内部でやってくれる）
	if ( model_ ) {
		model_->Draw();
	}
}

void SkinnedObj3d::DrawDebugUI(){
	if ( ImGui::Begin("Skinning Only Control") ) {
		// アニメーションを止めるかどうかのチェックボックス
		ImGui::Checkbox("Manual Mode (Pause Animation)", &isPause_);

		// 手動モードの時だけ骨をいじれるようにする
		if ( isPause_ ) {
			for ( auto& joint : skeleton_.joints ) {
				if ( ImGui::TreeNode(joint.name.c_str()) ) {
					// skeleton_ の transform を直接操作
					ImGui::DragFloat3("Translation", &joint.transform.translate.x, 0.01f);
					ImGui::DragFloat3("Rotation", &joint.transform.rotate.x, 0.01f);
					ImGui::DragFloat3("Scale", &joint.transform.scale.x, 0.01f);
					ImGui::TreePop();
				}
			}
		}
	}
	ImGui::End();
}

// --------------------------------------------------
// Setter
// --------------------------------------------------
void SkinnedObj3d::SetDissolveThreshold(float threshold){
	if ( dissolveData_ ) {
		dissolveData_->threshold = threshold;
	}
}
