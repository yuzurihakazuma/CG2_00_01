#include "EditorManager.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_internal.h"
#endif
#include "engine/base/Input.h"
#include "engine/base/DirectXCommon.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/graphics/SrvManager.h"
#include "engine/scene/SceneManager.h"
#include "engine/utils/Level/LevelEditor.h" 
#include "engine/particle/GPUParticleEditor.h" 
#include "engine/3d/obj/SkinnedObj3d.h"

// シングルトンインスタンスの取得
EditorManager* EditorManager::GetInstance(){
    static EditorManager instance;
    return &instance;
}

// cpp 側で定義（unique_ptr の前方宣言対応）
EditorManager::~EditorManager() = default;


void EditorManager::Initialize(){
    // LevelEditor の生成と初期化
    levelEditor_ = std::make_unique<LevelEditor>();
    levelEditor_->Initialize();

    gpuParticleEditor_ = std::make_unique<GPUParticleEditor>();
}



void EditorManager::Begin(){
#ifdef USE_IMGUI
    ImGuiManager::GetInstance()->Begin();
#endif
}

void EditorManager::Update(){
#ifdef USE_IMGUI
    Input* input = Input::GetInstance();
    if ( input->Triggerkey(DIK_F1) ) {
        isEditorActive_ = !isEditorActive_;
    }

    // LevelEditor の更新は常に行う（エディタ非表示でも3Dオブジェクトは動く）
    if ( levelEditor_ ) {
        levelEditor_->Update();
    }

    if ( targetSkinnedObj_ ) {
        targetSkinnedObj_->DrawDebugUI();
    }

    if ( !isEditorActive_ ) { return; }

    // 1. 全画面の透明なドッキング土台
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("MasterDockSpace", nullptr, window_flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // imgui.ini にレイアウトが保存されていない時のみ初期レイアウトを構築する
    // DockBuilderGetNode が nullptr を返す = 保存データなし = 初期化が必要
    if ( ImGui::DockBuilderGetNode(dockspace_id) == nullptr ) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        // 左ペイン(25%) → 中央 → 右ペイン(30%) → 中央下(25%) に分割
        ImGuiID dock_center = dockspace_id;
        ImGuiID dock_left   = ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Left,  0.25f, nullptr, &dock_center);
        ImGuiID dock_right  = ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Right, 0.30f, nullptr, &dock_center);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Down,  0.25f, nullptr, &dock_center);

        // 各ウィンドウを対応するペインに割り当て
        ImGui::DockBuilderDockWindow("Game View",                dock_center);
        ImGui::DockBuilderDockWindow("コントロール (Play / Stop)", dock_left);
        ImGui::DockBuilderDockWindow("パフォーマンスモニター",       dock_right);
        ImGui::DockBuilderDockWindow("レベルエディタ",              dock_left);
        ImGui::DockBuilderDockWindow("パーティクルエディタ",         dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();

    // コントロールウィンドウ
    ImGui::Begin("コントロール (Play / Stop)");

    if ( currentMode_ == EngineMode::Edit ) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "状態：エディットモード (停止中)");
        if ( ImGui::Button("▶ プレイ開始 (Play)", ImVec2(150, 40)) ) {
            currentMode_ = EngineMode::Play;
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "状態：プレイモード (実行中)");
        if ( ImGui::Button("■ 停止 (Stop)", ImVec2(150, 40)) ) {
            currentMode_ = EngineMode::Edit;
        }
    }

    ImGui::End();

    // 2. メインメニューバー
    if ( ImGui::BeginMainMenuBar() ) {
        if ( ImGui::BeginMenu("ファイル (File)") ) {
            if ( ImGui::MenuItem("パーティクルを保存 (Save Particles)") ) {}
            if ( ImGui::MenuItem("パーティクルを読み込む (Load Particles)") ) {}
            ImGui::Separator();
            if ( ImGui::MenuItem("エディタを終了する") ) {}
            ImGui::EndMenu();
        }
        if ( ImGui::BeginMenu("ウィンドウ (Window)") ) {
            if ( ImGui::MenuItem("レイアウトをリセット") ) {
                // ノードを削除することで次フレームの GetNode チェックが nullptr になり再構築される
                ImGuiID id = ImGui::GetID("MyDockSpace");
                ImGui::DockBuilderRemoveNode(id);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("シーン (Scene)")) {
            if (ImGui::MenuItem("タイトル (Title Scene)")) {
                SceneManager::GetInstance()->ChangeScene("TITLE");
            }
            if (ImGui::MenuItem("ゲームプレイ (GamePlay Scene)")) {
                SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
            }
            if (ImGui::MenuItem("アニメーションエディタ (Animation Editor)")) {
                SceneManager::GetInstance()->ChangeScene("ANIMATION_EDITOR");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    

    // 3. Game View
    ImGui::Begin("Game View");
    ImVec2 sceneSize = ImGui::GetContentRegionAvail();
    if ( sceneSize.x < 10.0f ) sceneSize.x = 640.0f;
    if ( sceneSize.y < 10.0f ) sceneSize.y = 360.0f;
    uint32_t srvIndex = gameViewSrvIndex_;
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
        SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex);
    ImGui::Image(( ImTextureID ) ( uintptr_t ) textureHandle.ptr, sceneSize);
    ImGui::End();

    // 4. 全シーン共通のUI
    PostEffect::GetInstance()->DrawDebugUI();

    // 5. 現在のシーン固有のUI
    SceneManager::GetInstance()->DrawCurrentSceneDebugUI();

    // 6. LevelEditor のデバッグUI
    if ( levelEditor_ ) {
        levelEditor_->DrawDebugUI();
    }

    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->DrawDebugUI();
    }

    // 7. パフォーマンスモニター
    ImGui::Begin("パフォーマンスモニター");
    float fps = ImGui::GetIO().Framerate;
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("フレーム時間: %.3f ms", 1000.0f / fps);
    ImGui::Separator();
    ImGui::Text("[CPU] 更新処理(Update) : %.3f ms", cpuUpdateTimeMs_);
    ImGui::Text("[CPU] 描画準備(Draw)   : %.3f ms", cpuDrawTimeMs_);
    ImGui::Separator();
    float totalCpuTime = cpuUpdateTimeMs_ + cpuDrawTimeMs_;
    if ( fps < 55.0f ) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " 警告: 処理落ちが発生しています！");
        if ( totalCpuTime > 16.0f ) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                " 原因: CPUの処理が重いです\n（計算やループ処理が多すぎます）");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                " 原因: GPUの処理が重いです\n（描画する量が多すぎるか、シェーダーが重いです）");
        }
    } else {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), " 快適に動作しています！ (60 FPS維持)");
    }
    ImGui::End();
#endif
}
// レベルエディタの描画
void EditorManager::Draw(){
    if ( levelEditor_ ) {
        levelEditor_->Draw();
    }
}

void EditorManager::SetCamera(const Camera* camera){
    if ( levelEditor_ ) {
        levelEditor_->SetCamera(camera);
    }
}


void EditorManager::End(){
#ifdef USE_IMGUI
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ImGuiManager::GetInstance()->End(dxCommon->GetCommandList());
#endif
}

void EditorManager::Finalize(){
    levelEditor_.reset();
    gpuParticleEditor_.reset();
}

void EditorManager::SetParticleEmitter(GPUParticleEmitter* emitter){
    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->SetEmitter(emitter);
    }
}