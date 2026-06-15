#include "EditorManager.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_internal.h"
#endif
#include "engine/base/Input.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/TimeManager.h"
#include "engine/base/WindowProc.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/utils/RenderStats.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/graphics/SrvManager.h"
#include "engine/scene/SceneManager.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/utils/Level/BlenderImporter.h"
#include "engine/particle/GPUParticleEditor.h"
#include "engine/utils/GlobalVariables.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/camera/Camera.h"
#include "engine/camera/DebugCamera.h"
#include "engine/math/Matrix4x4.h"
#ifdef USE_IMGUI
#include "externals/ImGuizmo/ImGuizmo.h"
#endif

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

    // Blenderシーンインポータ（インポート結果は levelEditor_ に反映される）
    blenderImporter_ = std::make_unique<BlenderImporter>();
    blenderImporter_->Initialize(levelEditor_.get());

    gpuParticleEditor_ = std::make_unique<GPUParticleEditor>();
}



void EditorManager::Begin(){
#ifdef USE_IMGUI
    ImGuiManager::GetInstance()->Begin();
#endif
}

void EditorManager::Update(){
#ifdef USE_IMGUI
    // ImGuizmo のフレーム開始（ImGui::NewFrame の後・操作の前に毎フレーム1回）
    ImGuizmo::BeginFrame();

    Input* input = Input::GetInstance();
    if ( input->Triggerkey(DIK_F1) ) {
        isEditorActive_ = !isEditorActive_;
    }

    // エクスプローラーからD&Dされたファイルを取り込む（エディタ非表示でも受け付ける）
    {
        std::vector<std::string> dropped = WindowProc::GetInstance()->PopDroppedFiles();
        if ( !dropped.empty() && blenderImporter_ ) {
            for ( const auto& file : dropped ) {
                blenderImporter_->HandleDroppedFile(file);
            }
        }
    }

    // LevelEditor の更新は常に行う（エディタ非表示でも3Dオブジェクトは動く）
    if ( levelEditor_ ) {
        levelEditor_->Update();
    }

    // ※ targetSkinnedObj_->DrawDebugUI() は「インスペクター (詳細設定)」を Begin するので、
    //   ドックスペース作成より後（下の共通UIセクション）で呼ぶ。
    //   先に呼ぶとそのウィンドウがドック外で生成され、ドッキングできなくなる。

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
        ImGui::DockBuilderDockWindow("インスペクター (Transform)",   dock_right);
        ImGui::DockBuilderDockWindow("レベルエディタ",              dock_left);
        ImGui::DockBuilderDockWindow("Blenderインポート (Blender)",  dock_left);
        ImGui::DockBuilderDockWindow("パーティクルエディタ",         dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();

    // コントロールウィンドウ（実行制御をここに集約：モード切替＋時間制御）
    ImGui::Begin("コントロール (Play / Stop)");

    Time* time = Time::GetInstance();
    if ( currentMode_ == EngineMode::Edit ) {
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "状態：エディットモード (編集中)");
        if ( ImGui::Button("▶ プレイ開始 (Play)", ImVec2(150, 40)) ) {
            currentMode_ = EngineMode::Play;
            time->SetTimeScale(1.0f); // Playで時間を動かす
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "状態：プレイモード (実行中)");
        if ( ImGui::Button("■ 停止 (Stop)", ImVec2(150, 40)) ) {
            currentMode_ = EngineMode::Edit;
            time->SetTimeScale(0.0f); // Stopで時間を止める
        }
    }

    // 時間制御（ポーズ／スロー／倍速）も同じウィンドウに集約
    ImGui::Separator();
    float timeScale = time->GetTimeScale();
    if ( ImGui::SliderFloat("タイムスケール", &timeScale, 0.0f, 2.0f, "%.2fx") ) {
        time->SetTimeScale(timeScale);
    }
    if ( ImGui::Button(time->IsPaused() ? "再開 (Resume)" : "一時停止 (Pause)") ) {
        time->SetTimeScale(time->IsPaused() ? 1.0f : 0.0f);
    }
    ImGui::SameLine();
    if ( ImGui::Button("等速 (1.0x)") ) { time->SetTimeScale(1.0f); }

    ImGui::End();

    // 2. メインメニューバー
    if ( ImGui::BeginMainMenuBar() ) {
        if ( ImGui::BeginMenu("ファイル (File)") ) {
            if ( ImGui::MenuItem("パーティクルを保存 (Save Particles)") ) {
                if ( gpuParticleEditor_ ) { gpuParticleEditor_->Save(); }
            }
            if ( ImGui::MenuItem("パーティクルを読み込む (Load Particles)") ) {
                if ( gpuParticleEditor_ ) { gpuParticleEditor_->Load(); }
            }
            ImGui::Separator();
            if ( ImGui::MenuItem("エディタを閉じる (F1で再表示)") ) {
                isEditorActive_ = false;
            }
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
                SceneManager::GetInstance()->ChangeSceneWithFade("TITLE");
            }
            if (ImGui::MenuItem("ゲームプレイ (GamePlay Scene)")) {
                SceneManager::GetInstance()->ChangeSceneWithFade("GAMEPLAY");
            }
            if (ImGui::MenuItem("アニメーションエディタ (Animation Editor)")) {
                SceneManager::GetInstance()->ChangeSceneWithFade("ANIMATION_EDITOR");
            }
            ImGui::EndMenu();
        }
        // 表示メニュー：デバッグカメラのON/OFF をメニューバーに集約（チェックボックス）
        if ( ImGui::BeginMenu("表示 (View)") ) {
            if ( debugCamera_ ) {
                bool active = debugCamera_->IsActive();
                if ( ImGui::Checkbox("デバッグカメラを有効化", &active) ) {
                    debugCamera_->SetActive(active);
                }
            } else {
                ImGui::TextDisabled("デバッグカメラ未登録");
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

    // --- ImGuizmo：Game View 上で対象オブジェクトをドラッグ操作 ---
    if ( gizmoTarget_ && editorCamera_ ) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImVec2 imgMin = ImGui::GetItemRectMin();   // 直前の Image の矩形
        ImVec2 imgSize = ImGui::GetItemRectSize();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

        Matrix4x4 view = editorCamera_->GetViewMatrix();
        Matrix4x4 proj = editorCamera_->GetProjectionMatrix();
        Matrix4x4 world = MatrixMath::MakeAffine(
            gizmoTarget_->GetScale(), gizmoTarget_->GetRotation(), gizmoTarget_->GetTranslation());

        ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0],
            ( ImGuizmo::OPERATION ) gizmoOperation_, ( ImGuizmo::MODE ) gizmoMode_, &world.m[0][0]);

        if ( ImGuizmo::IsUsing() ) {
            float t[3], r[3], s[3];
            ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], t, r, s);
            const float deg2rad = 3.14159265358979323846f / 180.0f;
            gizmoTarget_->SetTranslation({ t[0], t[1], t[2] });
            gizmoTarget_->SetScale({ s[0], s[1], s[2] });
            gizmoTarget_->SetRotation({ r[0] * deg2rad, r[1] * deg2rad, r[2] * deg2rad });
        }
    }

    ImGui::End();

    // 4. 全シーン共通のUI
    PostEffect::GetInstance()->DrawDebugUI();

    // スキニングUI（「詳細設定」へ合流するのでドックスペースより後に Begin する）
    if ( targetSkinnedObj_ ) {
        targetSkinnedObj_->DrawDebugUI();
    }

    // 5. 現在のシーン固有のUI
    SceneManager::GetInstance()->DrawCurrentSceneDebugUI();

    // 6. LevelEditor のデバッグUI
    if ( levelEditor_ ) {
        levelEditor_->DrawDebugUI();
    }

    // 6.5 Blenderインポータ（パネル描画＋ホットリロード監視）
    if ( blenderImporter_ ) {
        blenderImporter_->DrawDebugUI();
    }

    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->DrawDebugUI();
    }

    // 調整項目（GlobalVariables）の編集ウィンドウ
    GlobalVariables::GetInstance()->Update();

    // 7.5 インスペクター（ギズモ対象オブジェクトの Transform 編集）
    ImGui::Begin("インスペクター (Transform)");
    if ( gizmoTarget_ ) {
        // ギズモ操作モードの切替（Game View 上のギズモに反映）
        if ( ImGui::RadioButton("移動", gizmoOperation_ == 7) ) { gizmoOperation_ = 7; }    // TRANSLATE
        ImGui::SameLine();
        if ( ImGui::RadioButton("回転", gizmoOperation_ == 120) ) { gizmoOperation_ = 120; } // ROTATE
        ImGui::SameLine();
        if ( ImGui::RadioButton("拡縮", gizmoOperation_ == 896) ) { gizmoOperation_ = 896; } // SCALE
        if ( ImGui::RadioButton("ワールド", gizmoMode_ == 1) ) { gizmoMode_ = 1; }
        ImGui::SameLine();
        if ( ImGui::RadioButton("ローカル", gizmoMode_ == 0) ) { gizmoMode_ = 0; }
        ImGui::Separator();

        const float rad2deg = 180.0f / 3.14159265358979323846f;
        const float deg2rad = 3.14159265358979323846f / 180.0f;
        Vector3 t = gizmoTarget_->GetTranslation();
        Vector3 r = gizmoTarget_->GetRotation();
        Vector3 s = gizmoTarget_->GetScale();
        float tp[3] = { t.x, t.y, t.z };
        float rp[3] = { r.x * rad2deg, r.y * rad2deg, r.z * rad2deg };
        float sp[3] = { s.x, s.y, s.z };
        if ( ImGui::DragFloat3("位置 (Position)", tp, 0.05f) ) { gizmoTarget_->SetTranslation({ tp[0], tp[1], tp[2] }); }
        if ( ImGui::DragFloat3("回転 (Rotation deg)", rp, 0.5f) ) { gizmoTarget_->SetRotation({ rp[0] * deg2rad, rp[1] * deg2rad, rp[2] * deg2rad }); }
        if ( ImGui::DragFloat3("拡縮 (Scale)", sp, 0.01f) ) { gizmoTarget_->SetScale({ sp[0], sp[1], sp[2] }); }
    } else {
        ImGui::TextDisabled("対象オブジェクトが未登録です\n(シーンから SetGizmoTarget で登録)");
    }
    ImGui::End();

    // 7. パフォーマンスモニター
    ImGui::Begin("パフォーマンスモニター");
    float fps = ImGui::GetIO().Framerate;

    // --- FPS 履歴グラフ（直近の推移を可視化）---
    static float fpsHistory[120] = {};
    static int   fpsOffset = 0;
    fpsHistory[fpsOffset] = fps;
    fpsOffset = ( fpsOffset + 1 ) % IM_ARRAYSIZE(fpsHistory);
    char fpsOverlay[32];
    snprintf(fpsOverlay, sizeof(fpsOverlay), "FPS %.1f", fps);
    ImGui::PlotLines("##fps", fpsHistory, IM_ARRAYSIZE(fpsHistory), fpsOffset,
        fpsOverlay, 0.0f, 120.0f, ImVec2(0, 60));

    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("フレーム時間: %.3f ms", 1000.0f / fps);
    ImGui::Separator();
    ImGui::Text("[CPU] 更新処理(Update) : %.3f ms", cpuUpdateTimeMs_);
    ImGui::Text("[CPU] 描画準備(Draw)   : %.3f ms", cpuDrawTimeMs_);

    // --- 描画統計（前フレームの集計値）---
    ImGui::Separator();
    ImGui::Text("ドローコール数 : %u", RenderStats::GetInstance()->GetDrawCalls());
    ImGui::Text("時間 (Time) : 経過 %.1f s / フレーム %llu",
        Time::GetInstance()->GetTotalTime(),
        ( unsigned long long ) Time::GetInstance()->GetFrameCount());
    // ※ 一時停止／スロー等の操作は「コントロール (Play / Stop)」ウィンドウに集約

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
    editorCamera_ = camera; // ギズモ計算でも使う
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
    blenderImporter_.reset();
    levelEditor_.reset();
    gpuParticleEditor_.reset();
}

void EditorManager::SetParticleEmitter(GPUParticleEmitter* emitter){
    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->SetEmitter(emitter);
    }
}