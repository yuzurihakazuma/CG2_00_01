#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

// 
#include "engine/math/struct.h"
#include "engine/math/Matrix4x4.h"

#include "engine/3d/animation/Skeleton.h"
#include "engine/3d/animation/SkinCluster.h"
#include "engine/3d/animation/Animation.h"
#include "IAnimatable.h"

// 前方宣言
class Obj3dCommon;
class Model;
class Camera;

class SkinnedObj3d : public IAnimatable {
public:
    //  WVP 構造体
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
    void Draw()   override;

	// / デバッグ用UIの描画
	void DrawDebugUI();


public:  // --- Getter / Setter ---

   
    void SetCamera(const Camera* camera) { camera_ = camera; }
    void SetTranslation(const Vector3& translate) override { translate_ = translate; }
    void SetRotation(const Vector3& rotation )override { rotation_ = rotation; }
    void SetScale(const Vector3& scale)override { scale_ = scale; }
    void SetLoopAnimation(bool loop) { isLoop_ = loop; }
    void SetNoiseTexture(uint32_t index) { noiseTextureIndex_ = index; }
    void SetDissolveThreshold(float threshold);

    void SetEnvironmentMap(uint32_t srvIndex){ skyboxTextureIndex_ = srvIndex; }


    const Vector3& GetTranslation() const override { return translate_; }
    const Vector3& GetRotation()    const  override { return rotation_; }
    const Vector3& GetScale()       const override { return scale_; }

    const std::string& GetName() const override { return name_; }
    void SetName(const std::string& name) override { name_ = name; }

	Skeleton& GetSkeleton(){ return skeleton_; }

	// アニメーションの再生速度（1=等速 / 0=停止）。
	// プレイヤーの実際の移動速度に合わせて呼ぶと「歩いた時だけ歩きモーション」になる
	void SetPlaybackSpeed(float speed){ playbackSpeed_ = speed; }


    void SetPauseAnimation(bool pause) { isPause_ = pause; }


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
    uint32_t      noiseTextureIndex_ = 0;

    // スケルトン・スキニング
    Skeleton    skeleton_;
    SkinCluster skinCluster_;

    // アニメーション
    Animation animation_;
    float     animationTime_ = 0.0f;
    float     playbackSpeed_ = 1.0f; // 再生速度（0=停止。歩行速度に合わせる等、毎フレーム変更可）
    bool      isLoop_ = true;

    std::string name_ = "SkinnedObj3d";


    bool isPause_ = false; // アニメーション一時停止フラグ


    uint32_t skyboxTextureIndex_ = 0;
};
