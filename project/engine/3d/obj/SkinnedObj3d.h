#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>
#include <vector>

#include "engine/math/struct.h"
#include "engine/math/Matrix4x4.h"
#include "engine/3d/animation/Skeleton.h"
#include "engine/3d/animation/SkinCluster.h"
#include "engine/3d/animation/Animation.h"
#include "IAnimatable.h"

class Obj3dCommon;
class Model;
class Camera;

class SkinnedObj3d : public IAnimatable {
public:
    // WVP 構造体
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 World;
        Matrix4x4 WorldInverseTranspose;
    };

    // ディゾルブ用（Obj3d と合わせる）
    struct DissolveData {
        float threshold;
        float padding[3];
    };

    // デバッグポーズで扱うジョイント差分
    struct JointTransformOffset {
        std::string jointName;
        Vector3 rotation;
        Vector3 translation;
    };

    /// <summary>
    /// 静的生成関数
    /// modelName    : ModelManager に登録済みのモデル名
    /// directoryPath: アニメーションファイルのフォルダ
    /// animFilename : アニメーションファイル名
    /// </summary>
    static std::unique_ptr<SkinnedObj3d> Create(
        const std::string& modelName,
        const std::string& directoryPath,
        const std::string& animFilename
    );

    void Initialize(
        Model* model,
        const std::string& directoryPath,
        const std::string& animFilename
    );

    void Update() override;
    void Draw() override;

public:
    void SetCamera(const Camera* camera) { camera_ = camera; }
    void SetTranslation(const Vector3& translate) override { translate_ = translate; }
    void SetRotation(const Vector3& rotation) override { rotation_ = rotation; }
    void SetScale(const Vector3& scale) override { scale_ = scale; }
    void SetLoopAnimation(bool loop) { isLoop_ = loop; }
    void SetNoiseTexture(uint32_t index) { noiseTextureIndex_ = index; }
    void SetColor(const Vector4& color);
    void SetDissolveThreshold(float threshold);

    const Vector3& GetTranslation() const override { return translate_; }
    const Vector3& GetRotation() const override { return rotation_; }
    const Vector3& GetScale() const override { return scale_; }
    Model* GetModel() const { return model_; }

    const std::string& GetName() const override { return name_; }
    void SetName(const std::string& name) override { name_ = name; }

    void SetIsWalking(bool isWalking) { isWalking_ = isWalking; } // 歩き状態切り替え

    // プレイヤーのポーズ編集GUIから参照するAPI
    const std::vector<std::string>& GetJointNames() const { return jointNames_; }
    bool HasJoint(const std::string& jointName) const;
    void ClearJointOffsets();
    void ClearJointOffset(const std::string& jointName);
    void SetJointRotationOffset(const std::string& jointName, const Vector3& rotation);
    void SetJointTranslationOffset(const std::string& jointName, const Vector3& translation);
    Vector3 GetJointRotationOffset(const std::string& jointName) const;
    Vector3 GetJointTranslationOffset(const std::string& jointName) const;

private:
    // トランスフォーム
    Vector3 scale_ = { 1.0f, 1.0f, 1.0f };
    Vector3 rotation_ = { 0.0f, 0.0f, 0.0f };
    Vector3 translate_ = { 0.0f, 0.0f, 0.0f };

    // 外部参照（所有しない）
    Model* model_ = nullptr;
    const Camera* camera_ = nullptr;
    Obj3dCommon* obj3dCommon_ = nullptr;

    // WVP 定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource_;
    TransformationMatrix* transformationMatrixData_ = nullptr;

    // ディゾルブ定数バッファ
    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource_;
    DissolveData* dissolveData_ = nullptr;
    uint32_t noiseTextureIndex_ = 0;

    // スケルトン・スキニング
    Skeleton skeleton_;
    SkinCluster skinCluster_;

    // アニメーション
    Animation animation_;
    float animationTime_ = 0.0f;
    bool isLoop_ = true;

    // プログラム歩きアニメ用
    float walkTimer_ = 0.0f;
    bool isWalking_ = true;
    float walkSpeed_ = 6.0f;
    float walkAmplitude_ = 0.6f;

    // ここを基準姿勢として保持して、毎フレームここからポーズを再構築する
    std::vector<std::string> jointNames_;
    std::vector<QuaternionTransform> baseJointTransforms_;
    std::vector<JointTransformOffset> jointOffsets_;

    std::string name_ = "SkinnedObj3d";
};
