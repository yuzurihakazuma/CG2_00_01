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
#include "engine/graphics/ResourceFactory.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/graphics/PipelineType.h"

using namespace MatrixMath;
using namespace QuaternionMath;

namespace {
// GUIから編集したEuler角をスケルトン用Quaternionへ変換する
Quaternion MakeQuaternionFromEulerXYZ(const Vector3& rotation) {
    Quaternion qx = MakeRotateAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, rotation.x);
    Quaternion qy = MakeRotateAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, rotation.y);
    Quaternion qz = MakeRotateAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, rotation.z);
    return Normalize(Multiply(Multiply(qx, qy), qz));
}
}

std::unique_ptr<SkinnedObj3d> SkinnedObj3d::Create(
    const std::string& modelName,
    const std::string& directoryPath,
    const std::string& animFilename
) {
    // ModelManager から登録済みモデルを探す
    Model* model = ModelManager::GetInstance()->FindModel(modelName);
    if (!model) {
        return nullptr;
    }

    auto obj = std::make_unique<SkinnedObj3d>();
    obj->Initialize(model, directoryPath, animFilename);
    return obj;
}

void SkinnedObj3d::Initialize(
    Model* model,
    const std::string& directoryPath,
    const std::string& animFilename
) {
    model_ = model;
    obj3dCommon_ = Obj3dCommon::GetInstance();

    auto dxCommon = obj3dCommon_->GetDxCommon();

    // WVP 定数バッファの作成（Obj3d と同じ）
    wvpResource_ = dxCommon->GetResourceFactory()->CreateBufferResource(sizeof(TransformationMatrix));
    assert(wvpResource_ != nullptr);
    wvpResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
    transformationMatrixData_->WVP = MakeIdentity4x4();
    transformationMatrixData_->World = MakeIdentity4x4();

    // ディゾルブ定数バッファの作成（Obj3d と同じ）
    dissolveResource_ = dxCommon->GetResourceFactory()->CreateBufferResource(sizeof(DissolveData));
    dissolveResource_->Map(0, nullptr, reinterpret_cast<void**>(&dissolveData_));
    dissolveData_->threshold = 0.0f;

    // スケルトンの作成
    skeleton_ = CreateSkeleton(model_->GetRootNode());
    jointNames_.clear();
    baseJointTransforms_.clear();
    jointOffsets_.clear();
    jointNames_.reserve(skeleton_.joints.size());
    baseJointTransforms_.reserve(skeleton_.joints.size());
    // デバッグポーズ用に初期姿勢を保持しておく
    for (const Joint& joint : skeleton_.joints) {
        jointNames_.push_back(joint.name);
        baseJointTransforms_.push_back(joint.transform);
    }

    // ボーン情報が無いモデルはスキニングしない
    if (model_->GetBoneOrder().empty()) {
        assert(false && "SkinnedObj3d::Initialize: this model has no bones");
        return;
    }

    // スキンクラスターの作成
    skinCluster_ = CreateSkinCluster(
        skeleton_,
        model_->GetInverseBindPoseMap(),
        model_->GetBoneOrder(),
        dxCommon
    );

    // アニメーションはプログラムで制御するので、ここでは読み込まない
    animation_.duration = 0.0f;
    animation_.nodeAnimations.clear();
}

void SkinnedObj3d::Update() {
    // 基準姿勢を毎フレーム作り直してから、歩行やデバッグポーズを重ねる
    if (animation_.duration > 0.0f) {
        animationTime_ += 1.0f / 60.0f;

        if (isLoop_) {
            animationTime_ = std::fmod(animationTime_, animation_.duration);
        } else if (animationTime_ > animation_.duration) {
            animationTime_ = animation_.duration;
        }

        // スケルトンにアニメーションを適用して行列を更新
        UpdateSkeleton(skeleton_, animation_, animationTime_);
    } else {
        // 編集オフセットが乗る前の基準姿勢を毎フレーム復元する
        if (baseJointTransforms_.size() == skeleton_.joints.size()) {
            for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
                skeleton_.joints[i].transform = baseJointTransforms_[i];
            }
        }

        // プログラムで歩きアニメーションを作る
        if (isWalking_) {
            walkTimer_ += 1.0f / 60.0f;

            float t = walkTimer_ * (walkSpeed_ * 1.35f);
            float swingL = std::sinf(t);
            float swingR = std::sinf(t + 3.14159f);
            float armAmplitude = walkAmplitude_ * 0.5f;
            float legAmplitude = walkAmplitude_ * 0.65f;
            float legBackScale = 0.35f;

            // 腕
            auto armLIt = skeleton_.jointMap.find("UpperArm.L");
            auto armRIt = skeleton_.jointMap.find("UpperArm.R");
            if (armLIt != skeleton_.jointMap.end()) {
                skeleton_.joints[armLIt->second].transform.rotate.x = -swingL * armAmplitude;
            }
            if (armRIt != skeleton_.jointMap.end()) {
                skeleton_.joints[armRIt->second].transform.rotate.x = -swingR * armAmplitude;
            }

            // 足
            auto legLIt = skeleton_.jointMap.find("UpperLeg.L");
            auto legRIt = skeleton_.jointMap.find("UpperLeg.R");
            float legL = (swingL > 0.0f) ? (swingL * legAmplitude) : (swingL * legAmplitude * legBackScale);
            float legR = (swingR > 0.0f) ? (swingR * legAmplitude) : (swingR * legAmplitude * legBackScale);
            if (legLIt != skeleton_.jointMap.end()) {
                skeleton_.joints[legLIt->second].transform.rotate.z = legL;
            }
            if (legRIt != skeleton_.jointMap.end()) {
                skeleton_.joints[legRIt->second].transform.rotate.z = -legR;
            }
        }
    }

    // デバッグポーズは最終的にジョイントへ上書きする
    for (const JointTransformOffset& offset : jointOffsets_) {
        auto jointIt = skeleton_.jointMap.find(offset.jointName);
        if (jointIt == skeleton_.jointMap.end()) {
            continue;
        }

        Joint& joint = skeleton_.joints[jointIt->second];

        // GUIの回転値は「そのジョイント本来の姿勢に足す差分」として扱う
        if (static_cast<size_t>(jointIt->second) < baseJointTransforms_.size()) {
            const QuaternionTransform& baseTransform = baseJointTransforms_[jointIt->second];
            Quaternion offsetRotation = MakeQuaternionFromEulerXYZ(offset.rotation);
            joint.transform.rotate = Normalize(Multiply(baseTransform.rotate, offsetRotation));
            joint.transform.translate = baseTransform.translate + offset.translation;
        } else {
            joint.transform.rotate = MakeQuaternionFromEulerXYZ(offset.rotation);
            joint.transform.translate = offset.translation;
        }
    }

    // 現在のローカル姿勢から行列だけ更新
    UpdateSkeleton(skeleton_);
    UpdateSkinCluster(skinCluster_, skeleton_, model_->GetBoneOrder());

    // WVP 行列の計算
    Matrix4x4 worldMatrix = MakeAffine(scale_, rotation_, translate_);

    Matrix4x4 worldViewProjectionMatrix;
    if (camera_) {
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

void SkinnedObj3d::Draw() {
    ID3D12GraphicsCommandList* commandList = obj3dCommon_->GetDxCommon()->GetCommandList();
    assert(commandList != nullptr);

    // スキニング専用パイプラインをセット
    PipelineManager::GetInstance()->SetPipeline(commandList, PipelineType::SkinningObject3D);

    // [1] WVP 定数バッファ
    commandList->SetGraphicsRootConstantBufferView(1, wvpResource_->GetGPUVirtualAddress());
    // [3] 平行光源
    commandList->SetGraphicsRootConstantBufferView(3, obj3dCommon_->GetLightResource()->GetGPUVirtualAddress());

    // [4] カメラ
    if (camera_) {
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
    commandList->SetGraphicsRootDescriptorTable(9, skinCluster_.srvHandle);

    // モデル描画
    if (model_) {
        Vector4 originalColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool isColorOverridden = false;

        if (useOverrideColor_ && model_->GetMaterial()) {
            originalColor = model_->GetMaterial()->color;  // 今の（白い）色を退避
            model_->GetMaterial()->color = overrideColor_; // 青くて透明な色をブチ込む！
            isColorOverridden = true;
        }

        // モデル本来の描画関数を呼ぶ
        // （この中で model_->GetMaterial()->color がGPUに送られます）
        model_->Draw();

        if (isColorOverridden && model_->GetMaterial()) {
            model_->GetMaterial()->color = originalColor; // これで本体は白いまま守られる！
        }
    }
}

void SkinnedObj3d::SetDissolveThreshold(float threshold) {
    if (dissolveData_) {
        dissolveData_->threshold = threshold;
    }
}

bool SkinnedObj3d::HasJoint(const std::string& jointName) const {
    return skeleton_.jointMap.find(jointName) != skeleton_.jointMap.end();
}

// 全ジョイントの編集オフセットをクリアする
void SkinnedObj3d::ClearJointOffsets() {
    jointOffsets_.clear();
}

// 指定ジョイントだけ編集オフセットを消す
void SkinnedObj3d::ClearJointOffset(const std::string& jointName) {
    for (auto it = jointOffsets_.begin(); it != jointOffsets_.end(); ++it) {
        if (it->jointName == jointName) {
            jointOffsets_.erase(it);
            return;
        }
    }
}

// ジョイント回転オフセットを設定する
void SkinnedObj3d::SetJointRotationOffset(const std::string& jointName, const Vector3& rotation) {
    if (!HasJoint(jointName)) {
        return;
    }

    for (JointTransformOffset& offset : jointOffsets_) {
        if (offset.jointName == jointName) {
            offset.rotation = rotation;
            return;
        }
    }

    jointOffsets_.push_back({ jointName, rotation, { 0.0f, 0.0f, 0.0f } });
}

// ジョイント移動オフセットを設定する
void SkinnedObj3d::SetJointTranslationOffset(const std::string& jointName, const Vector3& translation) {
    if (!HasJoint(jointName)) {
        return;
    }

    for (JointTransformOffset& offset : jointOffsets_) {
        if (offset.jointName == jointName) {
            offset.translation = translation;
            return;
        }
    }

    jointOffsets_.push_back({ jointName, { 0.0f, 0.0f, 0.0f }, translation });
}

// ジョイント回転オフセットを取得する
Vector3 SkinnedObj3d::GetJointRotationOffset(const std::string& jointName) const {
    for (const JointTransformOffset& offset : jointOffsets_) {
        if (offset.jointName == jointName) {
            return offset.rotation;
        }
    }

    return { 0.0f, 0.0f, 0.0f };
}

// ジョイント移動オフセットを取得する
Vector3 SkinnedObj3d::GetJointTranslationOffset(const std::string& jointName) const {
    for (const JointTransformOffset& offset : jointOffsets_) {
        if (offset.jointName == jointName) {
            return offset.translation;
        }
    }

    return { 0.0f, 0.0f, 0.0f };
}

void SkinnedObj3d::CopyPoseFrom(const SkinnedObj3d& source) {
    // 1. 相手の骨（Skeleton）のデータをそのままコピー
    this->skeleton_ = source.skeleton_;

    // 2. GPU側の骨データ（SkinCluster）も更新して、ポーズを確定させる
    // （エンジンの仕様に合わせて、UpdateSkinCluster 関数を使用します）
    if (model_) {
        UpdateSkinCluster(this->skinCluster_, this->skeleton_, model_->GetBoneOrder());
    }

    // 3. アニメーションを止めるフラグを立てる（isPause_ は不要なので削除）
    this->isPoseFrozen_ = true;
}

void SkinnedObj3d::SetColor(const Vector4& color) {
    // 複雑なバッファ作成はやめて、値を保存するだけにします
    overrideColor_ = color;
    useOverrideColor_ = true;
}