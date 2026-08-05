#define NOMINMAX
#include "AnimationEditorScene.h"
#include "Engine/Base/Input.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Base/DirectXCommon.h"
#include "Engine/Base/WindowProc.h"
#include "Engine/3D/Model/ModelManager.h"
#include "Engine/3D/Obj/Obj3dCommon.h"
#include "Engine/Graphics/PipelineManager.h"
#include "Engine/Graphics/TextureManager.h"
#include "engine/graphics/SrvManager.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/utils/EditorManager.h"
#include "TitleScene.h"

void AnimationEditorScene::Initialize() {
    auto windowProc = WindowProc::GetInstance();
    auto dxCommon = DirectXCommon::GetInstance();

    // モデル読み込み
    ModelManager::GetInstance()->LoadModel("human", "resources/human", "walk.gltf");

    // 環境マップ（キューブマップ）を読み込んで既定に設定。
    //   Object3D系シェーダーはキューブマップSRVを参照するため、束縛しないと
    //   スロット0の2Dテクスチャが刺さり GPU-BASED VALIDATION の次元不一致で落ちる
    envMapSrv_ = TextureManager::GetInstance()->LoadCube("resources/StandardCubeMap.dds").srvIndex;
    Obj3dCommon::GetInstance()->SetEnvironmentTexture(envMapSrv_);

    // カメラ生成・初期位置設定
    camera_ = std::make_unique<Camera>(
        windowProc->GetClientWidth(), windowProc->GetClientHeight(), dxCommon);
    camera_->SetTranslation({ 0.0f, 2.0f, -15.0f });

    // デバッグカメラ生成
    debugCamera_ = std::make_unique<DebugCamera>();
    debugCamera_->Initialize();

    // 編集対象モデル生成
    skinnedObj_ = SkinnedObj3d::Create("human", "resources/human", "walk.gltf");
    skinnedObj_->SetCamera(camera_.get());
    skinnedObj_->SetName("human");
    // 環境マップを明示設定（未設定だとスロット0の2DテクスチャがキューブマップSRVとして
    // 束縛され、GPU-BASED VALIDATION の次元不一致で落ちる。GamePlayScene と同じ対応）
    skinnedObj_->SetEnvironmentMap(envMapSrv_);

    // アニメーションエディター生成・設定
    editor_ = std::make_unique<AnimationEditor>();
    editor_->Initialize();
    editor_->SetCamera(camera_.get());
    editor_->AddTarget(skinnedObj_.get()); // 編集対象として登録
}

void AnimationEditorScene::Finalize() {}

void AnimationEditorScene::Update() {
    Input* input = Input::GetInstance();

    // カメラ更新
    if (debugCamera_) debugCamera_->Update(camera_.get());
    camera_->Update();

    // Tキーでタイトルに戻る
    if (input->Triggerkey(DIK_T)) {
        SceneManager::GetInstance()->ChangeScene(std::make_unique<TitleScene>());
        return;
    }

    // モデル更新
    skinnedObj_->Update();

    // エディター更新（アニメーション再生・キーフレーム補間）
    editor_->Update();
}

void AnimationEditorScene::Draw() {
    auto commandList = DirectXCommon::GetInstance()->GetCommandList();

    // PostEffectのRenderTextureに描画
    PostEffect::GetInstance()->PreDrawScene(commandList);
    Obj3dCommon::GetInstance()->PreDraw(commandList);
    // 環境マップを束縛（GamePlayScene と同じ。PreDraw でルートシグネチャが張り直されるため毎フレーム必要）
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(9, envMapSrv_);
    skinnedObj_->Draw();
    PostEffect::GetInstance()->PostDrawScene(commandList);
    PostEffect::GetInstance()->Draw(commandList);

    // Game Viewに表示するSRVをEditorManagerに通知
    EditorManager::GetInstance()->SetGameViewSrvIndex(
        PostEffect::GetInstance()->GetSrvIndex());
}

void AnimationEditorScene::DrawDebugUI() {
    // カメラのデバッグUI
    if (camera_)      camera_->DrawDebugUI();
    if (debugCamera_) debugCamera_->DrawDebugUI();

    // アニメーションエディターのUI（全て委譲）
    editor_->DrawDebugUI();
}

void AnimationEditorScene::Reload(){}
