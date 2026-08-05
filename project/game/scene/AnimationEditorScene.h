#pragma once
#include "Engine/Scene/IScene.h"
#include "Engine/Camera/Camera.h"
#include "Engine/Camera/DebugCamera.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/utils/AnimationEditor.h" // アニメーションエディター
#include <memory>

/// <summary>
/// アニメーションエディターシーン
/// カメラ・モデルの管理のみを担当し、
/// UI・編集ロジックは AnimationEditor に委譲する
/// </summary>
class AnimationEditorScene : public IScene {
public:
    void Initialize() override;
    void Finalize()   override;
    void Update()     override;
    void Draw()       override;
    void DrawDebugUI() override;
    void Reload() override;

private:
    std::unique_ptr<Camera>          camera_;      // メインカメラ
    std::unique_ptr<DebugCamera>     debugCamera_; // デバッグカメラ（マウス操作）
    std::unique_ptr<SkinnedObj3d>    skinnedObj_;  // 編集対象モデル
    std::unique_ptr<AnimationEditor> editor_;      // アニメーション編集UI
    uint32_t envMapSrv_ = 0; // 環境マップ（キューブマップ）。未束縛だとシェーダーのSRV次元不一致でGPU検証エラーになる
};
