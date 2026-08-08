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
#include "engine/utils/PerformanceMonitor.h"
#include "engine/utils/FileEditor.h"
#include "engine/utils/NodeEditor.h"
#include "engine/postEffect/PostEffect.h"
#include "engine/graphics/SrvManager.h"
#include "engine/scene/SceneManager.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/utils/Level/RailEditor.h"
#include "engine/utils/Level/BlenderImporter.h"
#include "engine/particle/GPUParticleEditor.h"
#include "engine/sdf/SDFManager.h"
#include "engine/sdf/SDFVolumeObject.h"
#include "engine/utils/GlobalVariables.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include <cstdio>   // UI設定(editor_ui.ini)の保存/読込
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "engine/3d/obj/Obj3d.h"
#include "engine/camera/Camera.h"
#include "engine/camera/DebugCamera.h"
#include "engine/math/Matrix4x4.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#ifdef USE_IMGUI
#include "externals/ImGuizmo/ImGuizmo.h"
#endif

namespace {

// SDF素材（画像/フォント/フォルダ。Shift押下時は .obj も）を resources/sdf_inbox へコピーする。
// SDFWatcher が sdf_inbox を監視していて、自動変換→エンジンへ反映まで面倒を見てくれる。
// SDF行きとして受け取ったら true（.obj/.gltf/.json は従来どおりモデル/シーン取込に回す）
bool RouteDropToSdf(const std::string& path, bool shiftHeld){
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c){ return ( char ) std::tolower(c); });

    const bool isDir = fs::is_directory(p, ec);
    bool target = isDir || ext == ".png" || ext == ".ttf" || ext == ".otf";
    if ( shiftHeld && ext == ".obj" ) { target = true; } // Shift+ドロップで .obj は3D SDF行き
    if ( !target ) { return false; }

    const fs::path inbox = "resources/sdf_inbox";
    fs::create_directories(inbox, ec);
    if ( isDir ) {
        fs::copy(p, inbox / p.filename(),
            fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else {
        fs::copy_file(p, inbox / p.filename(), fs::copy_options::overwrite_existing, ec);
    }
    return !ec;
}

} // namespace

// シングルトンインスタンスの取得
#ifdef USE_IMGUI
namespace {
    // パネル定義（アイコン＋短いラベルの2行ボタン・ツールチップ・ウィンドウ名）。
    //   アイコンは Segoe MDL2 のグリフ（UTF-8直書き）。EditorPanel と同じ並び順。
    //   アイコンツールバーとメニューバー「ウィンドウ」の両方から参照する
    struct PanelIconDef { const char* icon; const char* name; const char* windowTitle; };
    const PanelIconDef kPanelIcons[EditorManager::Panel_Count] = {
        { "\xEE\x9D\xA8\n操作", "コントロール（Play / Stop・タイムスケール）", "コントロール (Play / Stop)" },        // E768 ▶
        { "\xEE\xA3\xB1\n階層", "ヒエラルキー（配置リスト・マップ保存/読込）", "ヒエラルキー (配置リスト)" },          // E8F1 リスト
        { "\xEE\xA2\xB7\n資産", "アセットブラウザ（モデルのドラッグ配置）", "アセットブラウザ (Assets)" },            // E8B7 フォルダ
        { "\xEE\xA4\x8F\n詳細", "インスペクター（詳細設定：ポストエフェクト等）", "インスペクター (詳細設定)" },       // E90F レンチ
        { "\xEE\x9C\x8F\n変形", "インスペクター（Transform：ギズモ対象の編集）", "インスペクター (Transform)" },      // E70F 鉛筆
        { "\xEE\x9D\xB4\n線路", "レールエディタ（管理/作成/動き/配置物/接続）", "レールエディタ" },                  // E774 地球（コース）
        { "\xEE\x9C\xA2\n映像", "カメラエディタ（カメラゾーン）", "カメラエディタ" },                              // E722 カメラ
        { "\xEE\x9C\x99\n配置", "配置エディタ（コイン / ブロック）", "配置エディタ (Coin/Block)" },                // E719 バッグ
        { "\xEE\x9D\xBB\n敵",   "敵配置エディタ", "敵配置エディタ (Enemy Editor)" },                              // E77B 人物
        { "\xEE\x9C\xB4\n粒子", "GPUパーティクルエディタ", "GPU Particle Editor" },                              // E734 星
        { "\xEE\xA3\x92\n文字", "SDF（フォント / 画像）", "SDF (フォント/画像)" },                                // E8D2 フォント
        { "\xEE\xA0\xA3\n計測", "パフォーマンスモニター", "パフォーマンスモニター" },                              // E823 時計
        { "\xEE\xA2\xA5\n書類", "ファイルエディタ（Project）", "ファイルエディタ (Project)" },                     // E8A5 ドキュメント
        { "\xEE\x9C\x87\nマップ", "ミニマップ（俯瞰ビュー。クリックでカメラ移動）", "ミニマップ (俯瞰)" },            // E707 地図
    };
    // 単発アクションのアイコングリフ
    const char* kIconGlyphBack = "\xEE\x9C\x80"; // U+E700 メニュー（三本線）
    const char* kIconGlyphSave = "\xEE\x9D\x8E"; // U+E74E フロッピー（保存）
    const char* kIconGlyphGear = "\xEE\x9C\x93"; // U+E713 歯車（設定）
}
#endif

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

    // 性能モニター（master_engine から移植）
    perfMonitor_ = std::make_unique<PerformanceMonitor>();
    perfMonitor_->Initialize();

    // ファイルエディタ（Project風。master_engine から移植）
    fileEditor_ = std::make_unique<FileEditor>();
    fileEditor_->Initialize();

    // ノードエディタ（ブループリント風。master_engine から移植）
    nodeEditor_ = std::make_unique<NodeEditor>();
    nodeEditor_->Initialize();

    // SDF（フォント/画像）システム：パイプライン構築＋前回の配置(sdf_scene.json)を復元
    SDFManager::GetInstance()->Initialize();
}



void EditorManager::Begin(){
    // フレーム先頭（コマンドリストが閉じている安全なタイミング）で
    // ファイルエディタのサムネイル画像をまとめて読み込む。
    if ( fileEditor_ ) {
        fileEditor_->ProcessPendingThumbnails();
    }

    // SDF アトラスのフォルダ監視・自動ロード・ホットリロードも同じ安全な瞬間に行う
    SDFManager::GetInstance()->Update();
#ifdef USE_IMGUI
    // アイコンモード中はウィンドウ同士のドッキングを無効化する（NewFrame前に切替）。
    //   パネルは右側ドロワーへ自動整列されるのでドッキング不要。有効なままだと
    //   浮遊ウィンドウ同士を重ねてドッキングでき、アイコンモードのドック解体処理と
    //   衝突してエラーになる（敵＋レールの窓を重ねると落ちる報告の原因）
    if ( ImGui::GetCurrentContext() ) {
        ImGuiIO& io = ImGui::GetIO();
        if ( uiShell_ == UiShell::Icon ) { io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable; }
        else                             { io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; }
    }
    uiShellFrame_ = uiShell_; // このフレームのUI分岐はここで確定（F2切替は次フレームから効く）
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
    // F2: UIシェル切替（ドック⇔アイコン）。選択は editor_ui.ini に保存され次回起動も維持
    if ( !uiConfigLoaded_ ) { LoadUiConfig(); uiConfigLoaded_ = true; }
    if ( input->Triggerkey(DIK_F2) ) {
        uiShell_ = ( uiShell_ == UiShell::Dock ) ? UiShell::Icon : UiShell::Dock;
        if ( uiShell_ == UiShell::Dock ) { dockRelayoutPending_ = true; } // 元のドック配置へ組み直す
        SaveUiConfig();
    }

    // エクスプローラーからD&Dされたファイルを取り込む（エディタ非表示でも受け付ける）
    //   画像/フォント/フォルダ → SDFWatcher へ中継（Shift押下なら .obj も 3D SDF行き）
    //   .obj/.gltf/.json      → 従来どおりモデル/シーンとして取込
    {
        std::vector<std::string> dropped = WindowProc::GetInstance()->PopDroppedFiles();
        if ( !dropped.empty() ) {
            const bool shiftHeld = ImGui::GetIO().KeyShift;
            for ( const auto& file : dropped ) {
                if ( RouteDropToSdf(file, shiftHeld) ) { continue; }
                if ( blenderImporter_ ) { blenderImporter_->HandleDroppedFile(file); }
            }
        }
    }

    // LevelEditor の更新は常に行う（エディタ非表示でも3Dオブジェクトは動く）
    if ( levelEditor_ ) {
        levelEditor_->Update();
    }

    // ノードグラフの実行も常に行う（ウィンドウが閉じていても適用ノードが作用する）
    if ( nodeEditor_ ) {
        nodeEditor_->Update();
    }

    if ( !isEditorActive_ ) {
        // エディタ非表示（フルスクリーン）ではホイールズームを常に許可
        if ( debugCamera_ ) { debugCamera_->SetGameViewHovered(true); }
        return;
    }

    // 1. 全画面の透明なドッキング土台（アイコンモードではドックを使わず、ゲームビューを全画面背景にする）
    //   ※分岐は必ず uiShellFrame_（フレーム先頭で確定した値）を使う
    if ( uiShellFrame_ == UiShell::Dock ) {
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

    // アイコンモードから戻った直後は必ず初期レイアウトへ組み直す（浮遊化した窓を全部戻す）
    if ( dockRelayoutPending_ ) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        dockRelayoutPending_ = false;
    }

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
        // 4ゾーン構成（同じゾーンに入れたウィンドウはタブとしてまとまる）
        //   中央: ゲーム画面 / 左: シーン操作系 / 右: 設定・情報系 / 下: ツール系
        ImGui::DockBuilderDockWindow("Game View",                  dock_center);
        ImGui::DockBuilderDockWindow("コントロール (Play / Stop)",   dock_left);
        ImGui::DockBuilderDockWindow("ヒエラルキー (配置リスト)",     dock_left);
        ImGui::DockBuilderDockWindow("アセットブラウザ (Assets)",    dock_left);
        ImGui::DockBuilderDockWindow("インスペクター (詳細設定)",     dock_right);
        ImGui::DockBuilderDockWindow("インスペクター (Transform)",   dock_right);
        ImGui::DockBuilderDockWindow("パフォーマンスモニター",        dock_right);
        ImGui::DockBuilderDockWindow("レールエディタ",               dock_bottom);
        ImGui::DockBuilderDockWindow("Blenderインポート (Blender)",  dock_bottom);
        ImGui::DockBuilderDockWindow("GPU Particle Editor",         dock_bottom);
        // 以前は浮遊だったウィンドウも全てドックへ入れる（アイコンモードから戻った時に
        // バラバラの浮遊窓が散らばらないように、全ウィンドウの定位置を決めておく）
        ImGui::DockBuilderDockWindow("配置エディタ (Coin/Block)",     dock_bottom);
        ImGui::DockBuilderDockWindow("カメラエディタ",               dock_bottom);
        ImGui::DockBuilderDockWindow("ミニマップ (俯瞰)",            dock_left);
        ImGui::DockBuilderDockWindow("SDF (フォント/画像)",          dock_bottom);
        ImGui::DockBuilderDockWindow("ファイルエディタ (Project)",    dock_bottom);
        ImGui::DockBuilderDockWindow("敵配置エディタ (Enemy Editor)", dock_right);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
    } // uiShell_ == Dock
    // アイコンモードではドッキング自体を無効化している（Begin参照）ため、ここでは何もしない。
    // ドックモードへ戻る時は dockRelayoutPending_ で標準レイアウトが組み直される

    // コントロールウィンドウ（実行制御をここに集約：モード切替＋時間制御）
    if ( IsPanelVisible(Panel_Control) ) {
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
    } // Panel_Control

    // Ctrl+S の保存はヒエラルキーパネル内のハンドラが担当だが、パネルを非表示に
    // していると効かなくなるので、その間はここで代行する（ドック/アイコン両モード共通）
    {
        ImGuiIO& saveIo = ImGui::GetIO();
        if ( levelEditor_ && !IsPanelVisible(Panel_Hierarchy) && !saveIo.WantTextInput
            && saveIo.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) ) {
            levelEditor_->QuickSave();
        }
    }

    // 2. メインメニューバー
    if ( ImGui::BeginMainMenuBar() ) {
        if ( ImGui::BeginMenu("ファイル (File)") ) {
            // マップ保存：ヒエラルキーの「上書き保存」ボタンと同じ処理をメニューからも
            if ( ImGui::MenuItem("マップを上書き保存 (Ctrl+S)") ) {
                if ( levelEditor_ ) { levelEditor_->QuickSave(); }
            }
            ImGui::Separator();
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
            if ( ImGui::MenuItem(uiShell_ == UiShell::Dock ? "アイコンUIへ切替 (F2)" : "ドックUIへ切替 (F2)") ) {
                uiShell_ = ( uiShell_ == UiShell::Dock ) ? UiShell::Icon : UiShell::Dock;
                if ( uiShell_ == UiShell::Dock ) { dockRelayoutPending_ = true; }
                SaveUiConfig();
            }
            if ( ImGui::MenuItem("UI設定（フォント / スケール）") ) { showUiSettings_ = true; }
            ImGui::Separator();
            // パネルの表示/非表示（ドック・アイコン両モード共通の機能。今のモード側の設定を切り替える）
            if ( ImGui::BeginMenu(uiShell_ == UiShell::Dock ? "パネル表示（ドック）" : "パネル表示（アイコン）") ) {
                for ( int i = 0; i < Panel_Count; ++i ) {
                    bool visible = ( uiShell_ == UiShell::Dock ) ? !dockPanelHidden_[i] : panelVisible_[i];
                    if ( ImGui::MenuItem(kPanelIcons[i].windowTitle, nullptr, visible) ) {
                        if ( uiShell_ == UiShell::Dock ) { dockPanelHidden_[i] = visible; }
                        else                             { panelVisible_[i] = !visible; }
                        SaveUiConfig();
                    }
                }
                ImGui::EndMenu();
            }
            // ワークスペース一括切替（アイコンツールバーと同じ機能をメニューからも）
            if ( ImGui::BeginMenu("ワークスペース（一式切替）") ) {
                if ( ImGui::MenuItem("設置（配置エディタ＋操作）") )       { ApplyWorkspace(0); }
                if ( ImGui::MenuItem("調整（インスペクタ＋計測＋操作）") ) { ApplyWorkspace(1); }
                if ( ImGui::MenuItem("線路（レール＋カメラ＋階層）") )     { ApplyWorkspace(2); }
                if ( ImGui::MenuItem("敵配置（敵エディタ＋操作）") )       { ApplyWorkspace(3); }
                if ( ImGui::MenuItem("全部閉じる（ゲーム画面のみ）") )     { ApplyWorkspace(4); }
                ImGui::EndMenu();
            }
            ImGui::Separator();
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
            ImGui::Separator();
            ImGui::Checkbox("ノードエディタ", &showNodeEditor_);
            ImGui::Checkbox("Blenderインポータ", &showBlenderImporter_);
            ImGui::Separator();
            // デモ・デバッグ系の表示（マップ制作には不要なので既定OFF。必要な時だけON）
            ImGui::Checkbox("デモ展示（回転ブロック・オーラ・SDF卵・見本の人形）", &showDemo_);
            if ( ImGui::IsItemHovered() ) {
                ImGui::SetTooltip("エンジン機能の見本オブジェクト一式（ヒットエフェクト・サークル・ブロック群など）。\nゲーム本編とは無関係の展示なので、マップ制作中はOFFのままでよい");
            }
            ImGui::Checkbox("調整項目 (GlobalVariables)", &showGlobalVars_);
            ImGui::EndMenu();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if ( currentMode_ == EngineMode::Edit ) {
            // エディットモード時の表示（緑色）
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), " [ Edit Mode ] ");

            // 背景色を少し緑っぽくして目立たせる
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if ( ImGui::Button(" ▶ Play ") ) {
                currentMode_ = EngineMode::Play;
            }
            ImGui::PopStyleColor();

        } else {
            // プレイモード時の表示（赤色）
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), " [ Play Mode ] ");

            // 背景色を赤っぽくして目立たせる
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if ( ImGui::Button(" ■ Stop ") ) {
                currentMode_ = EngineMode::Edit;
            }
            ImGui::PopStyleColor();
        }

        ImGui::EndMainMenuBar();
    }

    

    // 3. Game View
    //   アイコンモードでは全画面の「背景ウィンドウ」として描く。
    //   マウス橋渡し（GameViewMouse）は Image の矩形から取るので、ブロック配置・
    //   敵ドラッグ・レール編集はどちらのモードでも無改修でそのまま動く
    if ( uiShellFrame_ == UiShell::Icon ) {
        ImGuiViewport* gameViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(gameViewport->WorkPos);
        ImGui::SetNextWindowSize(gameViewport->WorkSize);
        ImGui::SetNextWindowViewport(gameViewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("Game View", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(3);
    } else {
        ImGui::Begin("Game View");
    }
    ImVec2 sceneSize = ImGui::GetContentRegionAvail();
    if ( sceneSize.x < 10.0f ) sceneSize.x = 640.0f;
    if ( sceneSize.y < 10.0f ) sceneSize.y = 360.0f;
    uint32_t srvIndex = gameViewSrvIndex_;
    D3D12_GPU_DESCRIPTOR_HANDLE textureHandle =
        SrvManager::GetInstance()->GetGPUDescriptorHandle(srvIndex);
    ImGui::Image(( ImTextureID ) ( uintptr_t ) textureHandle.ptr, sceneSize);

    // 直前の Image の画面矩形とホバー状態（マウス↔ワールド変換に使う）
    const ImVec2 imgMin       = ImGui::GetItemRectMin();
    const ImVec2 imgSize      = ImGui::GetItemRectSize();
    bool         imageHovered = ImGui::IsItemHovered();

    // デバッグカメラのホイールズームは Game View にマウスがある時だけ許可する
    // （レールエディタ等のパネル上でスクロールしてもカメラがズームしないように）。
    if ( debugCamera_ ) { debugCamera_->SetGameViewHovered(imageHovered); }

    // ゲーム側の配置エディタ（敵ドラッグ等）が独自ピッキングに使う Game View 情報を控える
    {
        ImVec2 gvMouse = ImGui::GetMousePos();
        gameViewMouse_.hovered     = imageHovered;
        gameViewMouse_.gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        gameViewMouse_.mousePos    = { gvMouse.x, gvMouse.y };
        gameViewMouse_.imgMin      = { imgMin.x, imgMin.y };
        gameViewMouse_.imgSize     = { imgSize.x, imgSize.y };
        if ( editorCamera_ ) { gameViewMouse_.viewProj = editorCamera_->GetViewProjectionMatrix(); }
    }
    // 敵ドラッグ（ゲーム側）優先中はレール編集のマウス操作を止める（両方掴む事故防止）
    if ( externalDragActive_ ) { imageHovered = false; }

    if ( editorCamera_ ) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

        Matrix4x4 view = editorCamera_->GetViewMatrix();
        Matrix4x4 proj = editorCamera_->GetProjectionMatrix();

        // レール編集（ノードのギズモ／クリック追加）は Edit モード時のみ
        const bool railEditMode = ( currentMode_ == EngineMode::Edit );

        // レール編集は RailEditor クラスへ分離。Game View 上の操作はこの railEditor を介して行う。
        RailEditor* railEditor = levelEditor_ ? levelEditor_->GetRailEditor() : nullptr;

        // --- (A) レール選択ギズモ：選択ノード群（1個〜路線全体〜複数レール）をまとめて移動 ---
        bool railNodeGizmo = false;
        if ( railEditMode && railEditor ) {
            const auto& sel = railEditor->GetMultiSelection();
            if ( !sel.empty() ) {
                railNodeGizmo = true;

                // ドラッグしていない間は毎フレーム選択中心をピボットに取り直す。
                // ドラッグ中はピボットを保持し、移動量（差分）だけを全ノードへ配る。
                if ( !railSelDragging_ ) { railSelPivot_ = railEditor->GetSelectionCenter(); }

                Matrix4x4 world = MatrixMath::MakeAffine({ 1.0f,1.0f,1.0f }, { 0.0f,0.0f,0.0f }, railSelPivot_);

                // グリッド刻み：Ctrl を押しながらドラッグした時だけ一定刻みで動く。
                //   Ctrl+Shift はさらに細かい 1/10 刻み。そのままドラッグは滑らかな自由移動、
                //   Shift 単独は移動量を1/10にした微調整。刻み幅はレールエディタの「グリッドサイズ」。
                float snap[3] = { 0.0f, 0.0f, 0.0f };
                float* snapPtr = nullptr;
                if ( ImGui::GetIO().KeyCtrl ) {
                    float g = railEditor->GetRailGridSize();
                    if ( ImGui::GetIO().KeyShift ) g *= 0.1f; // Ctrl+Shift = 1/10 の細かい刻み
                    if ( g > 0.0f ) {
                        snap[0] = snap[1] = snap[2] = g;
                        snapPtr = snap;
                    }
                }
                ImGuizmo::Manipulate(&view.m[0][0], &proj.m[0][0],
                    ImGuizmo::TRANSLATE, ImGuizmo::WORLD, &world.m[0][0], nullptr, snapPtr);

                if ( ImGuizmo::IsUsing() ) {
                    railSelDragging_ = true;
                    float t[3], r[3], s[3];
                    ImGuizmo::DecomposeMatrixToComponents(&world.m[0][0], t, r, s);
                    Vector3 np = { t[0], t[1], t[2] };
                    Vector3 delta = { np.x - railSelPivot_.x, np.y - railSelPivot_.y, np.z - railSelPivot_.z };
                    // Shift 単独ドラッグ＝微調整（移動量を1/10に）。端点吸着に引っ張られると
                    // 細かい調整と喧嘩するので、1ノードでも吸着なしの平行移動を使う。
                    const bool fineDrag = ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl;
                    if ( fineDrag ) { delta.x *= 0.1f; delta.y *= 0.1f; delta.z *= 0.1f; }
                    if ( delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f ) {
                        if ( sel.size() == 1 && !fineDrag ) {
                            // 1ノードだけなら従来どおり（他レール端点へのノード吸着も効く）
                            railEditor->SetRailNodePos(sel[0].node, np);
                        } else {
                            // 複数ノード or 微調整：形を保ったまま平行移動
                            railEditor->TranslateSelection(delta);
                        }
                    }
                    railSelPivot_ = np;

                    // --- 自動スナップ候補の検出（§5：端点1個をドラッグしている時だけ）---
                    railSnapCandidate_.valid = false;
                    railSnapHeightGap_ = false;
                    railSnapDragRail_ = -1;
                    if ( railEditor->IsAutoSnap() && sel.size() == 1 && !fineDrag ) {
                        int nodeCount = railEditor->GetNodeCountOf(sel[0].rail);
                        bool isFront = ( sel[0].node == 0 );
                        bool isBack  = ( sel[0].node == nodeCount - 1 );
                        if ( nodeCount >= 2 && ( isFront || isBack ) ) {
                            RailEditor::SnapTarget candidate =
                                railEditor->FindSnapTarget(sel[0].rail, isFront, railEditor->GetSnapDistance());
                            if ( !candidate.valid ) {
                                // 3D距離では届かないが、真上から見れば近い相手（高さだけ合っていない）。
                                // 「近づけたのに繋がらない」の理由表示と高さ合わせスナップの両方に使う
                                candidate = railEditor->FindSnapTargetXZ(
                                    sel[0].rail, isFront, railEditor->GetSnapDistance());
                            }
                            if ( candidate.valid ) {
                                // 「高さ合わせ接続」かどうかは見つけ方ではなく実際のYの移動量で判定する。
                                //   スナップ距離を広げると3D検索でも高さ差の大きい相手が混ざるため、
                                //   ここで判定しないと立体交差が「高さ合わせOFF」でも黙って潰される
                                const float kHeightGapTol = 0.5f;
                                Vector3 dragTip;
                                float heightDiff = 0.0f;
                                if ( railEditor->GetNodePosOf(sel[0].rail, isFront ? 0 : nodeCount - 1, dragTip) ) {
                                    heightDiff = candidate.pos.y - dragTip.y;
                                }
                                railSnapCandidate_ = candidate;
                                railSnapHeightGap_ = std::abs(heightDiff) > kHeightGapTol;
                                railSnapDragRail_  = sel[0].rail;
                                railSnapDragFront_ = isFront;
                            }
                        }
                    }
                } else {
                    // ドラッグ終了エッジ：スナップ候補が生きていれば接続を確定する（§5）
                    //   高さ違いの候補は「高さも自動で合わせる」ONの時だけ（OFF=立体交差を作れる）
                    if ( railSelDragging_ && railSnapCandidate_.valid && railSnapDragRail_ >= 0
                      && ( !railSnapHeightGap_ || railEditor->IsSnapMatchHeight() ) ) {
                        railEditor->ConnectToTarget(railSnapDragRail_, railSnapDragFront_, railSnapCandidate_);
                        // 直後の DrawWindow 冒頭 CommitIfStable がドラッグ＋接続を1履歴にまとめる
                    }
                    railSnapCandidate_.valid = false;
                    railSnapHeightGap_ = false;
                    railSnapDragRail_ = -1;
                    railSelDragging_ = false;
                }
            } else {
                railSnapCandidate_.valid = false;
                railSnapHeightGap_ = false;
                railSnapDragRail_ = -1;
                railSelDragging_ = false;
            }
        }

        // --- (B) 対象オブジェクトのギズモ（レールノードを編集していない時だけ）---
        if ( !railNodeGizmo && gizmoTarget_ ) {
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

        // ===== (C) レール編集インタラクション（Editモード時のみ）=====
        if ( railEditMode && railEditor ) {
            Matrix4x4 vp    = editorCamera_->GetViewProjectionMatrix();
            Matrix4x4 invVP = MatrixMath::Inverse(vp);
            ImVec2 mouse = ImGui::GetMousePos();
            const bool gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

            // world→screen 投影（カメラ後方は false）。NDC計算は MatrixMath::WorldToNdc に一本化
            auto project = [&]( const Vector3& worldPos, ImVec2& out ) -> bool {
                Vector2 ndc;
                if ( !MatrixMath::WorldToNdc(worldPos, vp, ndc) ) return false;
                out.x = imgMin.x + ( ndc.x * 0.5f + 0.5f ) * imgSize.x;
                out.y = imgMin.y + ( 1.0f - ( ndc.y * 0.5f + 0.5f ) ) * imgSize.y;
                return true;
                };
            // mouse→地面(Y=配置高さ) のワールド点
            auto groundAt = [&]( const ImVec2& m, Vector3& out ) -> bool {
                float mx = ( m.x - imgMin.x ) / imgSize.x;
                float my = ( m.y - imgMin.y ) / imgSize.y;
                float ndcx = mx * 2.0f - 1.0f;
                float ndcy = 1.0f - my * 2.0f;
                Vector3 nearW = MatrixMath::Transforms({ ndcx, ndcy, 0.0f }, invVP);
                Vector3 farW  = MatrixMath::Transforms({ ndcx, ndcy, 1.0f }, invVP);
                Vector3 dir = { farW.x - nearW.x, farW.y - nearW.y, farW.z - nearW.z };
                float h = railEditor->GetRailDrawHeight();

                // レイがほぼ水平（地平線方向）だと交点が無限遠へ飛んで Z が暴れる → 弾く。
                // dir.y / |dir| はレイの「下向き具合」(0=水平, 1=真下)。約7°より浅ければ描かない。
                float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                if ( dirLen < 1e-6f ) return false;
                if ( std::abs(dir.y) / dirLen < 0.12f ) return false;

                float tt = ( h - nearW.y ) / dir.y;
                if ( tt <= 0.0f ) return false;

                Vector3 p = { nearW.x + dir.x * tt, h, nearW.z + dir.z * tt };

                // カメラから水平に遠すぎる点は描かない（地平線側の巨大な伸び＝暴れを防ぐ）
                const float kMaxReach = 60.0f;
                float ddx = p.x - nearW.x, ddz = p.z - nearW.z;
                if ( std::sqrt(ddx * ddx + ddz * ddz) > kMaxReach ) return false;

                out = p;
                return true;
                };

            // --- マウス下のノード / 線分を「全レール」から探す ---
            //   ノードクリック＝そのノードを選択／線クリック＝路線まるごと選択
            int hoverRail = -1, hoverIdx = -1; float hoverBest = 12.0f;
            int segRail = -1,   segIdx = -1;   float segBest = 10.0f; Vector3 segPoint {};
            const int railCount = railEditor->GetRailCount();
            if ( imageHovered ) {
                for ( int rr = 0; rr < railCount; ++rr ) {
                    const int nodeCount = railEditor->GetNodeCountOf(rr);
                    for ( int i = 0; i < nodeCount; ++i ) {
                        Vector3 p; if ( !railEditor->GetNodePosOf(rr, i, p) ) continue;
                        ImVec2 s; if ( !project(p, s) ) continue;
                        float d = std::sqrt(( s.x - mouse.x ) * ( s.x - mouse.x ) + ( s.y - mouse.y ) * ( s.y - mouse.y ));
                        if ( d < hoverBest ) { hoverBest = d; hoverRail = rr; hoverIdx = i; }
                    }
                    for ( int i = 0; i + 1 < nodeCount; ++i ) {
                        Vector3 a, b;
                        if ( !railEditor->GetNodePosOf(rr, i, a) || !railEditor->GetNodePosOf(rr, i + 1, b) ) continue;
                        ImVec2 sa, sb; if ( !project(a, sa) || !project(b, sb) ) continue;
                        float vx = sb.x - sa.x, vy = sb.y - sa.y;
                        float len2 = vx * vx + vy * vy;
                        float t = ( len2 > 1e-6f ) ? ( ( mouse.x - sa.x ) * vx + ( mouse.y - sa.y ) * vy ) / len2 : 0.0f;
                        if ( t < 0.0f ) t = 0.0f; if ( t > 1.0f ) t = 1.0f;
                        float cxp = sa.x + vx * t, cyp = sa.y + vy * t;
                        float d = std::sqrt(( cxp - mouse.x ) * ( cxp - mouse.x ) + ( cyp - mouse.y ) * ( cyp - mouse.y ));
                        if ( d < segBest ) {
                            segBest = d; segRail = rr; segIdx = i;
                            segPoint = { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t };
                        }
                    }
                }
            }

            const bool ctrlHeld  = ImGui::GetIO().KeyCtrl;
            const bool shiftHeld = ImGui::GetIO().KeyShift;

            // ===== (B) スタンプ配置：形を生成すると pending になり、マウスに追従→クリックで設置 =====
            const bool stampPending = railEditor->HasPendingStamp();
            if ( stampPending ) {
                Vector3 g;
                bool onGround = groundAt(mouse, g);

                // ゴースト（設置される形）をマウス位置に重ねて表示
                if ( onGround ) {
                    ImDrawList* ghostDrawList = ImGui::GetWindowDrawList();
                    // 複数レールテンプレートは全ポリラインをゴースト表示（見えない骨組みガイドは薄く）。
                    // パラメータを変えるとテンプレート側が毎フレーム作り直すので、ゴーストが即追従する
                    const auto& multiShapes = railEditor->GetPendingMultiStamp();
                    const auto& multiMeta   = railEditor->GetPendingMultiMeta();
                    std::vector<const std::vector<Vector3>*> shapesToDraw;
                    if ( !multiShapes.empty() ) { for ( const auto& s : multiShapes ) shapesToDraw.push_back(&s); }
                    else { shapesToDraw.push_back(&railEditor->GetPendingStamp()); }
                    for ( int si = 0; si < ( int ) shapesToDraw.size(); ++si ) {
                        bool dim = ( si < ( int ) multiMeta.size() && multiMeta[si].visible == 0 );
                        ImU32 colLine = dim ? IM_COL32(90, 220, 255, 80)  : IM_COL32(90, 220, 255, 200);
                        ImU32 colDot  = dim ? IM_COL32(90, 220, 255, 100) : IM_COL32(90, 220, 255, 230);
                        ImVec2 prevS; bool prevOk = false;
                        for ( const auto& rel : *shapesToDraw[si] ) {
                            Vector3 wp = { g.x + rel.x, g.y + rel.y, g.z + rel.z };
                            ImVec2 s; bool ok = project(wp, s);
                            if ( ok ) {
                                ghostDrawList->AddCircleFilled(s, dim ? 2.0f : 3.0f, colDot);
                                if ( prevOk ) ghostDrawList->AddLine(prevS, s, colLine, dim ? 1.5f : 2.0f);
                            }
                            prevS = s; prevOk = ok;
                        }
                    }
                }
                // クリックで設置 / 右クリック・Esc で中止
                if ( imageHovered && ImGui::IsMouseClicked(0) && !gizmoActive && onGround ) {
                    railEditor->PlaceStamp(g);
                }
                if ( ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ) {
                    railEditor->CancelStamp();
                }
            }

            // ===== (C) キーボードでノード操作（Game View にマウスがある時のみ）=====
            //   矢印=グリッド1マスXZ移動 / Q,E=上下Y / Shift併用=1/10マスの微調整
            //   Delete=削除 / Ctrl+D=路線複製
            if ( imageHovered && !ImGui::IsMouseDown(1) && !ImGui::GetIO().WantTextInput ) {
                float gs = railEditor->GetRailGridSize();
                if ( shiftHeld ) gs *= 0.1f; // Shift＋移動キー = 微調整
                Vector3 kd { 0.0f, 0.0f, 0.0f };
                if ( ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true) ) kd.x -= gs;
                if ( ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ) kd.x += gs;
                if ( ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true) ) kd.z += gs; // 奥
                if ( ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true) ) kd.z -= gs; // 手前
                if ( ImGui::IsKeyPressed(ImGuiKey_E,          true) ) kd.y += gs; // 上
                if ( ImGui::IsKeyPressed(ImGuiKey_Q,          true) ) kd.y -= gs; // 下
                if ( kd.x != 0.0f || kd.y != 0.0f || kd.z != 0.0f ) {
                    railEditor->TranslateSelection(kd);
                }
                if ( ImGui::IsKeyPressed(ImGuiKey_Delete, false) ) {
                    railEditor->DeleteSelectedNodes();
                }
                if ( ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D, false) ) {
                    railEditor->DuplicateRail(railEditor->GetCurrentRailIndex());
                }
            }

            const bool targetLocked = railEditor->IsEditTargetLocked();
            const int  lockedRail   = railEditor->GetCurrentRailIndex();

            // --- 線ホバーの目印：緑の「＋」＝ここを押してそのまま引っ張れば点が増える ---
            if ( !stampPending && imageHovered && !gizmoActive && !railRubberActive_
                && !railPullActive_ && !railPullPending_ && hoverIdx < 0 && segIdx >= 0
                && !( targetLocked && segRail != lockedRail ) && !ImGui::IsMouseDown(0) ) {
                ImVec2 hintPos;
                if ( project(segPoint, hintPos) ) {
                    ImDrawList* hintDrawList = ImGui::GetWindowDrawList();
                    hintDrawList->AddCircleFilled(hintPos, 6.0f, IM_COL32(120, 255, 160, 230));
                    hintDrawList->AddLine({ hintPos.x - 3.5f, hintPos.y }, { hintPos.x + 3.5f, hintPos.y }, IM_COL32(15, 60, 30, 255), 1.6f);
                    hintDrawList->AddLine({ hintPos.x, hintPos.y - 3.5f }, { hintPos.x, hintPos.y + 3.5f }, IM_COL32(15, 60, 30, 255), 1.6f);
                }
            }

            // --- 左クリック（押した瞬間）。スタンプ配置待ちの時は通常クリックを無効化 ---
            // 「編集対象を固定」中は、別レールへの切り替えを伴うクリック操作を無効化する
            //   （リフト調整中に他のレールを触ってタブの内容が消えてしまう事故防止）
            if ( !stampPending && imageHovered && ImGui::IsMouseClicked(0) && !gizmoActive && !railRubberActive_ ) {
                if ( hoverIdx >= 0 ) {
                    if ( targetLocked && hoverRail != lockedRail ) {
                        // 固定中：他レールのノードは選択しない（何もしない）
                    } else if ( shiftHeld ) {
                        railEditor->AddToSelection(hoverRail, hoverIdx);     // Shift+クリック＝追加選択
                    } else {
                        railEditor->SelectSingleNode(hoverRail, hoverIdx);   // ノード単体を選択
                    }
                } else if ( railEditor->IsFreehand() && railEditor->IsRailDrawMode() ) {
                    railFreehandStroking_ = true;                              // 一筆書き開始
                } else if ( segIdx >= 0 && !( targetLocked && segRail != lockedRail ) ) {
                    if ( ctrlHeld ) {
                        // Ctrl+線クリック → その位置にノードを挿入（従来どおりの最短操作）
                        railEditor->SetCurrentRail(segRail);
                        railEditor->InsertRailNode(segIdx, segPoint);
                    } else {
                        // 線を押した：この時点では確定しない。
                        //   そのまま動かせば「点を追加して引っ張る」/ 動かさず離せば「路線まるごと選択」
                        railPullPending_  = true;
                        railPullRail_     = segRail;
                        railPullSegIdx_   = segIdx;
                        railPullSegPoint_ = segPoint;
                        railPullStartX_   = mouse.x;
                        railPullStartY_   = mouse.y;
                    }
                } else if ( railEditor->IsRailDrawMode() ) {
                    Vector3 g; if ( groundAt(mouse, g) ) railEditor->AppendRailNodeAt(g); // 末尾に追加
                } else {
                    // 何もない所 → 矩形選択を開始（動かさず離せばクリック扱い＝選択解除）
                    railRubberActive_ = true;
                    railRubberStartX_ = mouse.x;
                    railRubberStartY_ = mouse.y;
                }
            }

            // --- 線を引っ張って点を追加：押したまま5px動かすと挿入＋そのままドラッグ ---
            //   動かさずに離した時だけ従来どおり「路線まるごと選択」になる
            if ( railPullPending_ ) {
                float pdx = mouse.x - railPullStartX_, pdy = mouse.y - railPullStartY_;
                float movedPx = std::sqrt(pdx * pdx + pdy * pdy);
                if ( !ImGui::IsMouseDown(0) ) {
                    railPullPending_ = false;
                    if ( movedPx < 5.0f ) { railEditor->SelectWholeRail(railPullRail_); }
                } else if ( movedPx >= 5.0f ) {
                    railPullPending_ = false;
                    railEditor->SetCurrentRail(railPullRail_);
                    railEditor->InsertRailNode(railPullSegIdx_, railPullSegPoint_);
                    railPullNode_ = railEditor->GetSelectedRailNode(); // 挿入ノードが選択される
                    railPullActive_ = ( railPullNode_ >= 0 );
                }
            }
            if ( railPullActive_ ) {
                if ( !ImGui::IsMouseDown(0) || !editorCamera_ ) {
                    railPullActive_ = false;
                    railPullNode_ = -1; // 離した：確定（Undoはマウスアップ後に1回分でまとまる）
                } else {
                    // カメラに平行な面でノードをマウスに追従させる（ガイドハンドルと同じ換算）
                    Vector3 nodePos;
                    if ( railEditor->GetNodePosOf(railPullRail_, railPullNode_, nodePos) ) {
                        ImVec2 nodeDelta = ImGui::GetIO().MouseDelta;
                        if ( nodeDelta.x != 0.0f || nodeDelta.y != 0.0f ) {
                            Vector3 camRotation = editorCamera_->GetRotation();
                            float cosPitch = std::cos(camRotation.x), sinPitch = std::sin(camRotation.x);
                            float sinYaw = std::sin(camRotation.y), cosYaw = std::cos(camRotation.y);
                            Vector3 camForward { cosPitch * sinYaw, -sinPitch, cosPitch * cosYaw };
                            Vector3 camRight { cosYaw, 0.0f, -sinYaw };
                            Vector3 camUp {
                                camForward.y * camRight.z - camForward.z * camRight.y,
                                camForward.z * camRight.x - camForward.x * camRight.z,
                                camForward.x * camRight.y - camForward.y * camRight.x };
                            ImVec2 sp0, spR, spU;
                            if ( project(nodePos, sp0)
                                && project({ nodePos.x + camRight.x, nodePos.y + camRight.y, nodePos.z + camRight.z }, spR)
                                && project({ nodePos.x + camUp.x, nodePos.y + camUp.y, nodePos.z + camUp.z }, spU) ) {
                                float rx = spR.x - sp0.x, ry = spR.y - sp0.y;
                                float ux = spU.x - sp0.x, uy = spU.y - sp0.y;
                                // 1mが2px未満にしか映らない極端な状況では換算を頭打ちにする（1pxで数十m飛ぶ暴走防止）
                                float lenR2 = ( std::max )( rx * rx + ry * ry, 4.0f );
                                float lenU2 = ( std::max )( ux * ux + uy * uy, 4.0f );
                                float meterR = ( nodeDelta.x * rx + nodeDelta.y * ry ) / lenR2;
                                float meterU = ( nodeDelta.x * ux + nodeDelta.y * uy ) / lenU2;
                                if ( ImGui::GetIO().KeyShift ) { meterR = 0.0f; } // Shift=縦（高さ）だけ
                                if ( ImGui::GetIO().KeyCtrl )  { meterU = 0.0f; } // Ctrl=横だけ
                                Vector3 newPos { nodePos.x + camRight.x * meterR + camUp.x * meterU,
                                                 nodePos.y + camRight.y * meterR + camUp.y * meterU,
                                                 nodePos.z + camRight.z * meterR + camUp.z * meterU };
                                // SetRailNodePos は現在レールの選択ノードに書く（端点吸着・版更新つき）
                                railEditor->SetRailNodePos(railPullNode_, newPos);
                                ImGui::SetTooltip("X=%.1f Y=%.1f Z=%.1f", newPos.x, newPos.y, newPos.z);
                            }
                        }
                        railSelDragging_ = true; // ドラッグ中は道の再生成を10Hzに間引く
                    } else {
                        railPullActive_ = false;
                        railPullNode_ = -1;
                    }
                }
            }

            // --- 矩形選択：ドラッグ中は枠を描き、離した時に囲んだノードをまとめて選択 ---
            if ( railRubberActive_ ) {
                float x0 = std::min(railRubberStartX_, mouse.x), x1 = std::max(railRubberStartX_, mouse.x);
                float y0 = std::min(railRubberStartY_, mouse.y), y1 = std::max(railRubberStartY_, mouse.y);
                ImDrawList* rubberDrawList = ImGui::GetWindowDrawList();
                rubberDrawList->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(90, 180, 255, 40));
                rubberDrawList->AddRect({ x0, y0 }, { x1, y1 }, IM_COL32(90, 180, 255, 200), 0.0f, 0, 1.5f);

                if ( !ImGui::IsMouseDown(0) ) {
                    railRubberActive_ = false;
                    if ( ( x1 - x0 ) + ( y1 - y0 ) > 8.0f ) {
                        // 囲んだノードを全レールから選択（Shift中は既存の選択に追加）
                        if ( !shiftHeld ) { railEditor->ClearMultiSelection(); }
                        int firstRail = -1;
                        for ( int rr = 0; rr < railCount; ++rr ) {
                            const int nodeCount = railEditor->GetNodeCountOf(rr);
                            for ( int i = 0; i < nodeCount; ++i ) {
                                Vector3 p; if ( !railEditor->GetNodePosOf(rr, i, p) ) continue;
                                ImVec2 s; if ( !project(p, s) ) continue;
                                if ( s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1 ) {
                                    railEditor->AddToSelection(rr, i);
                                    if ( firstRail < 0 ) firstRail = rr;
                                }
                            }
                        }
                        if ( firstRail >= 0 && !( targetLocked && firstRail != lockedRail ) ) {
                            railEditor->SetCurrentRail(firstRail);
                        }
                    } else {
                        // ほぼ動かさずに離した＝ただのクリック → 選択解除
                        railEditor->ClearMultiSelection();
                        railEditor->SetSelectedRailNode(-1);
                    }
                }
            }

            // --- 右クリック：ノード削除（固定中は他レールのノードを消さない＝切替も起きない）---
            if ( imageHovered && ImGui::IsMouseClicked(1) && !gizmoActive && hoverIdx >= 0
                && !( targetLocked && hoverRail != lockedRail ) ) {
                railEditor->SetCurrentRail(hoverRail);
                railEditor->DeleteRailNode(hoverIdx);
            }

            // --- フリーハンド：ドラッグ中はグリッド間隔ごとに点を置く ---
            if ( !ImGui::IsMouseDown(0) ) railFreehandStroking_ = false;
            if ( railFreehandStroking_ && railEditor->IsRailDrawMode() && !gizmoActive ) {
                Vector3 g;
                if ( groundAt(mouse, g) ) {
                    int c2 = railEditor->GetCurrentRailNodeCount();
                    Vector3 last; bool hasLast = ( c2 > 0 ) && railEditor->GetRailNodePos(c2 - 1, last);
                    float step = railEditor->GetRailGridSize() * 0.9f;
                    float dxz = hasLast ? std::sqrt(( g.x - last.x ) * ( g.x - last.x ) + ( g.z - last.z ) * ( g.z - last.z )) : 1e9f;
                    if ( !hasLast || dxz >= step ) railEditor->AppendRailNodeAt(g);
                }
            }

            // ===== (D) 視覚フィードバック（ImGui描画で重畳）=====
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int curRail = railEditor->GetCurrentRailIndex();

            // 全レールの線を「タイプ色」で描画（編集データから直接引くので、クリック対象＝
            // ノードと完全に一致する。横=青(A/D移動) / 縦=橙(W/S移動) で役割が一目で分かる）。
            for ( int rr = 0; rr < railCount; ++rr ) {
                const bool isCur = ( rr == curRail );
                const bool railVisible = railEditor->IsRailVisible(rr);
                const int  displayType = railEditor->GetRailDisplayType(rr);
                // 非表示レール（連結用）はエディタでは淡いグレーで描く（編集できるように）
                ImU32 col = !railVisible
                    ? IM_COL32(170, 170, 175, isCur ? 200 : 110)
                    : ( ( displayType == 1 )
                        ? IM_COL32(255, 165, 70, isCur ? 235 : 150)   // 縦 = 橙
                        : IM_COL32(90, 180, 255, isCur ? 235 : 150) ); // 横 = 青
                // プレイヤーがスタートから到達できないレールは紫で警告（穴=赤と区別できる色）
                const bool reachable = railEditor->IsRailReachable(rr);
                if ( railVisible && !reachable ) {
                    col = IM_COL32(200, 80, 255, isCur ? 235 : 160);
                }
                const ImU32 holeCol = IM_COL32(255, 60, 50, isCur ? 245 : 180); // 穴 = 赤
                const float baseW = isCur ? 2.5f : 1.5f;
                const int nodeCount = railEditor->GetNodeCountOf(rr);
                // 各区間を中点で2分割し、「近い側のノードが穴か」で半分ずつ色を変える。
                // → 穴ノード1個なら、その周り（前後の半区間）だけが赤くなり分かりやすい。
                for ( int i = 1; i < nodeCount; ++i ) {
                    Vector3 pa, pb;
                    if ( !railEditor->GetNodePosOf(rr, i - 1, pa) || !railEditor->GetNodePosOf(rr, i, pb) ) continue;
                    ImVec2 sa, sb;
                    if ( !project(pa, sa) || !project(pb, sb) ) continue;
                    ImVec2 mid = { ( sa.x + sb.x ) * 0.5f, ( sa.y + sb.y ) * 0.5f };
                    bool holeA = railEditor->IsNodeHole(rr, i - 1);
                    bool holeB = railEditor->IsNodeHole(rr, i);
                    // 非表示レールは点線風に（中点までだけ描いて隙間を作る）
                    if ( !railVisible ) {
                        ImVec2 q1 = { sa.x + ( mid.x - sa.x ) * 0.6f, sa.y + ( mid.y - sa.y ) * 0.6f };
                        ImVec2 q2 = { mid.x + ( sb.x - mid.x ) * 0.6f, mid.y + ( sb.y - mid.y ) * 0.6f };
                        dl->AddLine(sa, q1, col, baseW);
                        dl->AddLine(mid, q2, col, baseW);
                    } else {
                        dl->AddLine(sa, mid, holeA ? holeCol : col, baseW + ( holeA ? 1.5f : 0.0f ));
                        dl->AddLine(mid, sb, holeB ? holeCol : col, baseW + ( holeB ? 1.5f : 0.0f ));
                    }
                }

                // レール中央に接続状況の補足を出す：
                //   通れない → 紫の「未接続」／ 繋がっている → 水色で相手と種類（例: R2溶接 R4交差）
                if ( railVisible && nodeCount >= 2 ) {
                    Vector3 midNodePos;
                    ImVec2 sm;
                    if ( railEditor->GetNodePosOf(rr, nodeCount / 2, midNodePos) && project(midNodePos, sm) ) {
                        if ( !reachable ) { // 通れない表示は常時（消すのはジャンプ予測線だけ）
                            dl->AddText({ sm.x + 8.0f, sm.y - 8.0f }, IM_COL32(220, 120, 255, 255), "未接続 (通れない)");
                        }
                        // 接続の要約（選択中のレールだけ表示＝画面が文字だらけにならないように）
                        if ( rr == railEditor->GetCurrentRailIndex() ) {
                            const auto& conns = railEditor->GetRailConnections(rr);
                            if ( !conns.empty() ) {
                                const char* typeNames[] = { "溶接", "T字", "交差" };
                                std::string summary;
                                int shown = 0;
                                for ( const auto& conn : conns ) {
                                    if ( shown >= 4 ) { summary += " …"; break; }
                                    if ( shown > 0 ) summary += " ";
                                    summary += "R" + std::to_string(conn.otherRail) + typeNames[std::clamp(conn.type, 0, 2)];
                                    ++shown;
                                }
                                dl->AddText({ sm.x + 8.0f, sm.y + 6.0f }, IM_COL32(120, 220, 255, 255), summary.c_str());
                            }
                        }
                    }
                }
                // --- 接続/交差ポイントのマーク（全レール常時表示）---
                //   溶接=緑の二重丸 / T字=黄 / 交差=水色。「どこで繋がっているか」が見た瞬間に分かる。
                //   座標の数字は選択中のレールに関係する点だけ（画面が文字だらけにならないように）
                if ( railVisible ) {
                    for ( const auto& conn : railEditor->GetRailConnections(rr) ) {
                        if ( conn.otherRail < rr ) continue; // 相手側の走査と二重に描かない
                        ImVec2 sc;
                        if ( !project(conn.pos, sc) ) continue;
                        ImU32 markColor = ( conn.type == 0 ) ? IM_COL32(80, 255, 140, 235)   // 溶接=緑
                                        : ( conn.type == 1 ) ? IM_COL32(255, 200, 60, 235)   // T字=黄
                                                             : IM_COL32(120, 220, 255, 235); // 交差=水色
                        dl->AddCircleFilled(sc, 4.5f, markColor);
                        dl->AddCircle(sc, 8.5f, markColor, 0, 2.0f);
                        bool related = ( rr == railEditor->GetCurrentRailIndex()
                                      || conn.otherRail == railEditor->GetCurrentRailIndex() );
                        if ( related ) {
                            char coordText[64];
                            snprintf(coordText, sizeof(coordText), "(%.1f, %.1f, %.1f)",
                                     conn.pos.x, conn.pos.y, conn.pos.z);
                            dl->AddText({ sc.x + 10.0f, sc.y - 4.0f }, markColor, coordText);
                        }
                    }

                    // --- 繋がっていない端点の警告（全レール常時）---
                    //   赤いリング＋「未」で「この端はどこにも繋がっていない」が一目で分かる。
                    //   ループ（先頭=末尾）はそもそも端が無いので出さない
                    Vector3 frontPos, backPos;
                    bool isLoopRail = railEditor->GetNodePosOf(rr, 0, frontPos)
                                   && railEditor->GetNodePosOf(rr, nodeCount - 1, backPos)
                                   && nodeCount >= 3
                                   && ( std::abs(frontPos.x - backPos.x) + std::abs(frontPos.y - backPos.y)
                                      + std::abs(frontPos.z - backPos.z) ) < 0.7f;
                    if ( !isLoopRail ) {
                        for ( int endSide = 0; endSide < 2; ++endSide ) {
                            bool isFront = ( endSide == 0 );
                            if ( railEditor->IsRailEndConnected(rr, isFront) ) continue; // 繋がっている端はOK
                            Vector3 endPos;
                            if ( !railEditor->GetNodePosOf(rr, isFront ? 0 : nodeCount - 1, endPos) ) continue;
                            ImVec2 se;
                            if ( !project(endPos, se) ) continue;
                            dl->AddCircle(se, 10.0f, IM_COL32(255, 80, 70, 235), 0, 2.5f);
                            dl->AddCircle(se, 14.0f, IM_COL32(255, 80, 70, 120), 0, 1.5f);
                            dl->AddText({ se.x + 12.0f, se.y - 6.0f }, IM_COL32(255, 110, 100, 235), "未");
                        }
                    }

                }

                // --- 端点の足元ガイド（選択中のレールだけ。非表示＝連結用レールでも出す）---
                //   端点から地面(Y=0)へ縦線を落とし、足元に丸＋端点の横にY値を表示。
                //   パース付き3Dビューでは見えない「高さ」を縦線の長さと数字でそのまま見せる
                if ( rr == railEditor->GetCurrentRailIndex() && railEditor->IsEndGuideVisible() ) {
                    Vector3 firstTip {};
                    bool hasFirstTip = false;
                    for ( int endSide = 0; endSide < 2; ++endSide ) {
                        int nodeIdx = ( endSide == 0 ) ? 0 : nodeCount - 1;
                        Vector3 tipPos;
                        if ( !railEditor->GetNodePosOf(rr, nodeIdx, tipPos) ) continue;
                        if ( endSide == 0 ) { firstTip = tipPos; hasFirstTip = true; }
                        // 同じ点を二重に描かない（1ノード、またはループで先頭と末尾が同位置）
                        else if ( nodeIdx == 0 ) break;
                        else if ( hasFirstTip
                               && std::abs(tipPos.x - firstTip.x) + std::abs(tipPos.y - firstTip.y)
                                + std::abs(tipPos.z - firstTip.z) < 0.01f ) break;
                        ImVec2 sTip;
                        if ( !project(tipPos, sTip) ) continue;
                        const ImU32 guideCol = IM_COL32(230, 230, 230, 150);
                        // 縦線と足元マークは地面点が映る時だけ。Y値の数字は端点が映れば必ず出す
                        //   （見上げるカメラだと地面点が後ろに回って project が失敗するため分離）
                        Vector3 footPos { tipPos.x, 0.0f, tipPos.z };
                        ImVec2 sFoot;
                        if ( project(footPos, sFoot) ) {
                            dl->AddLine(sTip, sFoot, guideCol, 1.5f);
                            dl->AddCircle(sFoot, 5.0f, guideCol, 0, 1.5f);
                            dl->AddLine({ sFoot.x - 7.0f, sFoot.y }, { sFoot.x + 7.0f, sFoot.y }, guideCol, 1.5f);
                        }
                        char heightText[32];
                        snprintf(heightText, sizeof(heightText), "Y=%.1f", tipPos.y);
                        dl->AddText({ sTip.x + 10.0f, sTip.y + 8.0f }, IM_COL32(235, 235, 235, 220), heightText);
                    }
                }
            }

            // --- リストでホバー中のレールを黄色でハイライト（リストと画面の対応が一目で分かる）---
            //   接続一覧のホバー時は相手側レールと接続点のリングも一緒に光らせる
            {
                auto highlightRail = [&](int railIdx){
                    if ( railIdx < 0 ) return;
                    int highlightNodeCount = railEditor->GetNodeCountOf(railIdx);
                    ImVec2 prevS {}; bool prevOk = false;
                    for ( int ni = 0; ni < highlightNodeCount; ++ni ) {
                        Vector3 p; ImVec2 s;
                        if ( !railEditor->GetNodePosOf(railIdx, ni, p) || !project(p, s) ) { prevOk = false; continue; }
                        if ( prevOk ) { dl->AddLine(prevS, s, IM_COL32(255, 240, 80, 230), 4.5f); }
                        dl->AddCircleFilled(s, 4.0f, IM_COL32(255, 240, 80, 230));
                        prevS = s; prevOk = true;
                    }
                };
                highlightRail(railEditor->GetHoveredListRail());
                highlightRail(railEditor->GetHoveredListRailB());
                Vector3 connPos;
                if ( railEditor->GetHoveredConnPos(connPos) ) {
                    ImVec2 sc;
                    if ( project(connPos, sc) ) {
                        dl->AddCircle(sc, 13.0f, IM_COL32(255, 240, 80, 255), 0, 3.0f);
                        dl->AddCircle(sc, 19.0f, IM_COL32(255, 240, 80, 120), 0, 2.0f);
                    }
                }
            }

            // --- 到達チェックの経路（オレンジ）とゴースト走行（緑の丸）---
            {
                const auto& route = railEditor->GetRoutePoints();
                const auto& routeJump = railEditor->GetRouteJumpFlags();
                if ( route.size() >= 2 ) {
                    ImVec2 prevS {}; bool prevOk = false;
                    for ( size_t ri = 0; ri < route.size(); ++ri ) {
                        ImVec2 s; bool ok = project(route[ri], s);
                        if ( ok && prevOk ) {
                            // レール区間=オレンジ / 通常ジャンプ=シアン / ふんばり必要=マゼンタ
                            char jumpFlag = ( ri < routeJump.size() ) ? routeJump[ri] : 0;
                            ImU32 routeCol = ( jumpFlag == 2 ) ? IM_COL32(255, 110, 245, 230)
                                           : ( jumpFlag == 1 ) ? IM_COL32(80, 230, 255, 230)
                                                               : IM_COL32(255, 150, 40, 220);
                            dl->AddLine(prevS, s, routeCol, jumpFlag ? 3.5f : 5.0f);
                        }
                        prevS = s; prevOk = ok;
                    }
                }
                Vector3 ghostPos;
                if ( railEditor->GetGhostPos(ghostPos) ) {
                    ImVec2 s;
                    if ( project({ ghostPos.x, ghostPos.y + 0.4f, ghostPos.z }, s) ) {
                        dl->AddCircleFilled(s, 8.0f, IM_COL32(90, 255, 120, 235));
                        dl->AddCircle(s, 11.0f, IM_COL32(30, 160, 70, 235), 0, 2.0f);
                    }
                }
            }

            // --- 凡例：マークの意味を Game View の左下に常時表示 ---
            //   初めて触る人でも「丸や色が何を意味するか」が画面内で完結して分かるように
            {
                float legendY = imgMin.y + imgSize.y - 22.0f;
                float legendX = imgMin.x + 10.0f;
                dl->AddRectFilled({ legendX - 6.0f, legendY - 5.0f },
                                  { legendX + 470.0f, legendY + 17.0f }, IM_COL32(0, 0, 0, 110), 4.0f);
                auto legendItem = [&](ImU32 color, const char* label, bool filled){
                    if ( filled ) { dl->AddCircleFilled({ legendX + 5.0f, legendY + 6.0f }, 4.5f, color); }
                    else          { dl->AddCircle({ legendX + 5.0f, legendY + 6.0f }, 5.5f, color, 0, 2.0f); }
                    dl->AddText({ legendX + 13.0f, legendY }, IM_COL32(235, 235, 235, 255), label);
                    legendX += 13.0f + ImGui::CalcTextSize(label).x + 14.0f;
                };
                legendItem(IM_COL32(80, 255, 140, 235),  "溶接", true);
                legendItem(IM_COL32(255, 200, 60, 235),  "T字", true);
                legendItem(IM_COL32(120, 220, 255, 235), "交差", true);
                legendItem(IM_COL32(255, 80, 70, 235),   "未接続の端", false);
                dl->AddText({ legendX, legendY }, IM_COL32(220, 120, 255, 255), "紫の線=通れない路線");
                legendX += ImGui::CalcTextSize("紫の線=通れない路線").x + 14.0f;
                dl->AddText({ legendX, legendY }, IM_COL32(255, 90, 80, 255), "赤い線=穴");
            }

            // --- ジャンプ予測：Gap レールの未接続の端から弾道（放物線）を点線で描く ---
            //   シアン=どこかのレールへ着地できる（着地点に◎）/ 赤=届かない（先端に✕）。
            //   Safe レールの端はゲーム仕様で飛び出せない（クランプ）ので、
            //   飛べば届く相手がいる時だけ「端をGapにすれば渡れる」とヒントを出す。
            //   ※Gapが標準になり点線が密になりやすいため、エディタ設定でOFFにできる
            if ( railEditor->IsReachLinesVisible() ) {
                const auto& groundTypes = railEditor->GetRailGroundTypes();
                for ( int rr = 0; rr < railCount; ++rr ) {
                    if ( !railEditor->IsRailVisible(rr) ) continue;
                    const int nodeCnt = railEditor->GetNodeCountOf(rr);
                    if ( nodeCnt < 2 ) continue;
                    const int gt = ( rr < ( int ) groundTypes.size() ) ? groundTypes[rr] : 0;
                    for ( int side = 0; side < 2; ++side ) {
                        const bool front = ( side == 0 );
                        // 接続済みの端はゲームでは合流が優先されて飛び出せないので描かない
                        if ( railEditor->IsRailEndConnected(rr, front) ) continue;

                        if ( gt == 1 ) { // 1 = GroundType::Gap（飛び出せる端）
                            std::vector<Vector3> arc;
                            int land = railEditor->PredictJumpLanding(rr, front, true, &arc);
                            ImU32 jumpColor = ( land >= 0 ) ? IM_COL32(80, 230, 230, 210)  // シアン = 渡れる
                                                       : IM_COL32(255, 90, 90, 180);  // 赤 = 届かない
                            ImVec2 prevS {}; bool prevOk = false;
                            for ( size_t k = 0; k < arc.size(); ++k ) {
                                ImVec2 s; bool ok = project(arc[k], s);
                                if ( ok && prevOk && ( k % 2 == 0 ) ) dl->AddLine(prevS, s, jumpColor, 2.0f); // 1個おき＝点線
                                prevS = s; prevOk = ok;
                            }
                            if ( !arc.empty() ) {
                                ImVec2 e;
                                if ( project(arc.back(), e) ) {
                                    if ( land >= 0 ) {
                                        dl->AddCircle(e, 7.0f, jumpColor, 0, 2.5f); // 予測着地点
                                    } else {
                                        dl->AddLine({ e.x - 5, e.y - 5 }, { e.x + 5, e.y + 5 }, jumpColor, 2.0f);
                                        dl->AddLine({ e.x - 5, e.y + 5 }, { e.x + 5, e.y - 5 }, jumpColor, 2.0f);
                                    }
                                }
                            }
                        } else if ( gt == 0 ) { // Safe：端では止まる → 飛べば届くならヒント
                            if ( railEditor->PredictJumpLanding(rr, front, true) >= 0 ) {
                                Vector3 p;
                                if ( railEditor->GetNodePosOf(rr, front ? 0 : nodeCnt - 1, p) ) {
                                    ImVec2 s;
                                    if ( project(p, s) ) {
                                        dl->AddText({ s.x + 10.0f, s.y - 14.0f },
                                            IM_COL32(255, 200, 90, 230), "端をGapにすれば渡れる");
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // マウス下の路線をハイライト（クリックでどれを掴むか分かる）。
            // ノードに乗っていればその路線、そうでなければ線の路線を光らせる。
            int highlightRail = ( hoverIdx >= 0 ) ? hoverRail : segRail;
            if ( highlightRail >= 0 && !stampPending ) {
                const int nodeCount = railEditor->GetNodeCountOf(highlightRail);
                ImVec2 prevS; bool prevOk = false;
                for ( int i = 0; i < nodeCount; ++i ) {
                    Vector3 p; if ( !railEditor->GetNodePosOf(highlightRail, i, p) ) { prevOk = false; continue; }
                    ImVec2 s; bool ok = project(p, s);
                    if ( ok && prevOk ) {
                        dl->AddLine(prevS, s, IM_COL32(255, 240, 130, 220), 4.0f); // 太い黄色で路線全体を強調
                    }
                    prevS = s; prevOk = ok;
                }
            }

            // 全レールのノードを描画（編集中の路線＝明るい緑、他＝控えめな色、穴＝赤）
            for ( int rr = 0; rr < railCount; ++rr ) {
                const bool isCur = ( rr == curRail );
                const ImU32 col = isCur ? IM_COL32(80, 220, 120, 255) : IM_COL32(150, 175, 160, 170);
                const int nodeCount = railEditor->GetNodeCountOf(rr);
                for ( int i = 0; i < nodeCount; ++i ) {
                    Vector3 p; if ( !railEditor->GetNodePosOf(rr, i, p) ) continue;
                    ImVec2 s; if ( !project(p, s) ) continue;
                    bool hole = railEditor->IsNodeHole(rr, i);
                    dl->AddCircleFilled(s, hole ? 5.0f : ( isCur ? 3.5f : 3.0f ),
                        hole ? IM_COL32(255, 60, 50, 255) : col);
                }
            }
            // 選択中のノード群（オレンジの輪）
            for ( const auto& selectedNode : railEditor->GetMultiSelection() ) {
                Vector3 p; ImVec2 s;
                if ( railEditor->GetNodePosOf(selectedNode.rail, selectedNode.node, p) && project(p, s) ) {
                    dl->AddCircle(s, 8.0f, IM_COL32(255, 140, 40, 255), 0, 2.5f);
                }
            }
            // ホバー中のノード（黄色の輪）
            { Vector3 p; ImVec2 s;
              if ( hoverIdx >= 0 && railEditor->GetNodePosOf(hoverRail, hoverIdx, p) && project(p, s) )
                  dl->AddCircle(s, 9.5f, IM_COL32(255, 235, 80, 255), 0, 2.0f); }
            // 自動スナップのプレビュー（§5）：候補があれば接続先をハイライト＋端点ゴースト
            //   緑=そのまま繋がる ／ オレンジ=真上から見れば近いが高さだけズレている
            //   （高さズレはパース付きの3Dビューではほぼ見えないので、点線＋数字で理由を出す）
            if ( railSnapCandidate_.valid && railSnapDragRail_ >= 0 ) {
                const bool heightGap   = railSnapHeightGap_;
                const bool willConnect = !heightGap || railEditor->IsSnapMatchHeight();
                const ImU32 colLine = heightGap ? IM_COL32(255, 170, 40, 210) : IM_COL32(60, 255, 120, 200);
                const ImU32 colMark = heightGap ? IM_COL32(255, 170, 40, 255) : IM_COL32(60, 255, 120, 255);
                const ImU32 colFill = heightGap ? IM_COL32(255, 170, 40, 110) : IM_COL32(60, 255, 120, 120);

                // 画面上の点線（高さのズレ＝縦方向のギャップを見せる用）
                auto dashedLine = [&](ImVec2 a, ImVec2 b, ImU32 col, float thickness){
                    float ddx = b.x - a.x, ddy = b.y - a.y;
                    float len = std::sqrt(ddx * ddx + ddy * ddy);
                    if ( len < 1.0f ) return;
                    float ux = ddx / len, uy = ddy / len;
                    const float dash = 7.0f, gap = 5.0f;
                    for ( float d = 0.0f; d < len; d += dash + gap ) {
                        float e = ( std::min )( d + dash, len );
                        dl->AddLine({ a.x + ux * d, a.y + uy * d },
                                    { a.x + ux * e, a.y + uy * e }, col, thickness);
                    }
                };

                Vector3 dragP {};
                int dragNode = railSnapDragFront_ ? 0 : railEditor->GetNodeCountOf(railSnapDragRail_) - 1;
                ImVec2 sFrom, sTo;
                bool okFrom = railEditor->GetNodePosOf(railSnapDragRail_, dragNode, dragP) && project(dragP, sFrom);
                bool okTo   = project(railSnapCandidate_.pos, sTo);
                if ( okTo ) {
                    // スナップ後の位置にゴースト（二重丸）＋ドラッグ中端点からの線
                    if ( okFrom ) {
                        if ( heightGap ) {
                            // 高さ(縦)と位置(横)に分けたL字点線：縦線の長さ＝合っていない高さの量
                            Vector3 corner { dragP.x, railSnapCandidate_.pos.y, dragP.z };
                            ImVec2 sCorner;
                            if ( project(corner, sCorner) ) {
                                dashedLine(sFrom, sCorner, colLine, 2.5f);
                                dashedLine(sCorner, sTo, colLine, 2.0f);
                            } else {
                                dashedLine(sFrom, sTo, colLine, 2.0f);
                            }
                        } else {
                            dl->AddLine(sFrom, sTo, colLine, 2.0f);
                        }
                    }
                    dl->AddCircleFilled(sTo, 5.0f, colFill);
                    dl->AddCircle(sTo, 9.0f, colMark, 0, 2.5f);
                    // 高さ違いの時は「なぜ繋がらないか／離すとどうなるか」を数字で出す
                    if ( heightGap && okFrom ) {
                        float heightDiff = railSnapCandidate_.pos.y - dragP.y;
                        char gapText[96];
                        snprintf(gapText, sizeof(gapText),
                                 willConnect ? "高さ差 %+.1fm（離すと高さを合わせて接続）"
                                             : "高さ差 %+.1fm（高さ合わせOFF：接続しません）",
                                 heightDiff);
                        ImVec2 mid { ( sFrom.x + sTo.x ) * 0.5f + 12.0f, ( sFrom.y + sTo.y ) * 0.5f - 8.0f };
                        ImVec2 ts = ImGui::CalcTextSize(gapText);
                        dl->AddRectFilled({ mid.x - 4.0f, mid.y - 3.0f },
                                          { mid.x + ts.x + 4.0f, mid.y + ts.y + 3.0f },
                                          IM_COL32(0, 0, 0, 150), 3.0f);
                        dl->AddText(mid, colMark, gapText);
                    }
                }
                // 接続先レールの該当付近をハイライト（端点 or セグメント両端）
                if ( railSnapCandidate_.isEndpoint ) {
                    Vector3 p; ImVec2 s;
                    if ( railEditor->GetNodePosOf(railSnapCandidate_.rail, railSnapCandidate_.node, p) && project(p, s) ) {
                        dl->AddCircle(s, 12.0f, colLine, 0, 2.0f);
                    }
                } else {
                    Vector3 a, b; ImVec2 sa, sb;
                    if ( railEditor->GetNodePosOf(railSnapCandidate_.rail, railSnapCandidate_.seg, a) &&
                         railEditor->GetNodePosOf(railSnapCandidate_.rail, railSnapCandidate_.seg + 1, b) &&
                         project(a, sa) && project(b, sb) ) {
                        dl->AddLine(sa, sb, colLine, 4.0f);
                    }
                }
            }
            // 配置ゴースト（マウス追加モードで、ノード/線分に当たっていない時）
            //   ・マウス直下の地面点（生の位置）に細い十字
            //   ・実際に置かれる位置（吸着後）に青い丸
            //   ・両者を線で結ぶ → 「吸着でここへ動く」が一目で分かる（ズレ感の解消）
            if ( imageHovered && railEditor->IsRailDrawMode() && !gizmoActive && hoverIdx < 0 && segIdx < 0 && !stampPending ) {
                Vector3 g;
                if ( groundAt(mouse, g) ) {
                    Vector3 place = railEditor->ComputePlacement(g);
                    ImVec2 sRaw, sPlace;
                    bool okRaw = project(g, sRaw);
                    bool okPlace = project(place, sPlace);
                    if ( okRaw ) {
                        // マウス直下（生の地面点）＝薄い灰の十字
                        dl->AddLine({ sRaw.x - 6, sRaw.y }, { sRaw.x + 6, sRaw.y }, IM_COL32(220, 220, 220, 150), 1.0f);
                        dl->AddLine({ sRaw.x, sRaw.y - 6 }, { sRaw.x, sRaw.y + 6 }, IM_COL32(220, 220, 220, 150), 1.0f);
                    }
                    if ( okPlace ) {
                        if ( okRaw ) dl->AddLine(sRaw, sPlace, IM_COL32(90, 200, 255, 130), 1.0f); // 吸着の移動量を可視化
                        dl->AddCircleFilled(sPlace, 4.0f, IM_COL32(90, 200, 255, 90));
                        dl->AddCircle(sPlace, 6.0f, IM_COL32(90, 200, 255, 240), 0, 2.0f);
                    }
                }
            }
        }
    }

    ImGui::End();

    // 3.5 アイコンツールバー（アイコンモードのみ。Game View の後に描いて手前に出す）
    if ( uiShellFrame_ == UiShell::Icon ) { DrawIconToolbar(); }
    if ( showUiSettings_ ) { DrawUiSettingsWindow(); }

    // 4. 全シーン共通のUI
    if ( IsPanelVisible(Panel_Inspector) ) {
        PostEffect::GetInstance()->DrawDebugUI();

        // スキニングUI（「詳細設定」へ合流するウィンドウは必ずドックスペースより後に Begin する。
        //  先に Begin するとそのウィンドウだけドッキングできなくなる）
        if ( targetSkinnedObj_ ) {
            targetSkinnedObj_->DrawDebugUI();
        }
    }

    // 5. 現在のシーン固有のUI
    SceneManager::GetInstance()->DrawCurrentSceneDebugUI();

    // 6. LevelEditor のデバッグUI（パネル分類に応じてウィンドウ単位で開閉。Dockモードは全true）
    if ( levelEditor_ ) {
        levelEditor_->DrawDebugUI(
            IsPanelVisible(Panel_Hierarchy), IsPanelVisible(Panel_Inspector), IsPanelVisible(Panel_Assets),
            IsPanelVisible(Panel_Rail), IsPanelVisible(Panel_Camera), IsPanelVisible(Panel_Items));
    }

    // 6.5 Blenderインポータ（パネル描画＋ホットリロード監視）
    //   普段は邪魔なので非表示（表示メニューでON）。D&D取り込みは Update 側で処理されるため生きている。
    //   ※Blenderの自動リロード監視はパネル表示中のみ動く。
    if ( blenderImporter_ && showBlenderImporter_ ) {
        blenderImporter_->DrawDebugUI();
    }

    if ( gpuParticleEditor_ && IsPanelVisible(Panel_GpuParticle) ) {
        gpuParticleEditor_->DrawDebugUI();
    }

    // ファイルエディタ（Project風のファイル閲覧・編集。master_engine から移植）
    if ( fileEditor_ && IsPanelVisible(Panel_File) ) {
        fileEditor_->DrawDebugUI();
    }

    // 調整項目（GlobalVariables）の編集ウィンドウ
    // 6.7 SDF（フォント/画像）パネル：アトラス一覧＋テキスト/スプライト配置
    if ( IsPanelVisible(Panel_Sdf) ) {
        SDFManager::GetInstance()->DrawDebugUI();
    }

    if ( showGlobalVars_ ) { GlobalVariables::GetInstance()->Update(); } // 表示メニューでON/OFF

    // 7.5 インスペクター（ギズモ対象オブジェクトの Transform 編集）
    if ( IsPanelVisible(Panel_Transform) ) {
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
    } // Panel_Transform

    // 7. パフォーマンスモニター（master_engine から移植：履歴グラフ・メモリ/VRAM・ドローコール付き）
    if ( perfMonitor_ && IsPanelVisible(Panel_Perf) ) {
        perfMonitor_->DrawDebugUI(cpuUpdateTimeMs_, cpuDrawTimeMs_);
    }

    // 8. ノードエディタ（表示メニューでON/OFF。master_engine から移植）
    if ( nodeEditor_ && showNodeEditor_ ) {
        nodeEditor_->DrawDebugUI(&showNodeEditor_);
    }

    // 9. アイコンモードのツールバーは全ウィンドウ提出後に必ず最前面へ戻す
    //   （Game View をクリックした時などに背面へ回って見えなくなる不具合の対策）
    if ( uiShellFrame_ == UiShell::Icon ) {
        if ( ImGuiWindow* toolbarWindow = ImGui::FindWindowByName("##IconToolbar") ) {
            ImGui::BringWindowToDisplayFront(toolbarWindow);
        }
    }
#endif
}
// レベルエディタの描画
void EditorManager::Draw(){
    if ( levelEditor_ ) {
        levelEditor_->Draw();
    }
}

// ゲーム側から Play モードを開始する（右クリック「ここからテストプレイ」用。Playボタンと同じ処理）
void EditorManager::RequestPlay(){
    currentMode_ = EngineMode::Play;
    Time::GetInstance()->SetTimeScale(1.0f);
}

// レール編集のドラッグ中か（ゲーム側が道の再生成を10Hzへ間引く判定に使う）。
//   ギズモ/フリーハンドに加えて、「リフト経路エディタ」とゲームビューの
//   ガイドハンドル（オレンジの点）のノードドラッグも含める
bool EditorManager::IsRailDragging() const{
    if ( railSelDragging_ || railFreehandStroking_ || gameViewGuideDragging_ ) return true;
    return levelEditor_ && levelEditor_->GetRailEditor()->IsGuidePanelDragging();
}

// =====================================================================
//  UIシェル（アイコンモード）
// =====================================================================
// ※パネル定義テーブル(kPanelIcons)はメニューバーからも参照するためファイル冒頭に移動した

void EditorManager::DrawIconToolbar(){
#ifdef USE_IMGUI
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    // ドロワーが左配置の時、ツールバーは右端へ寄せる（重ならないように反対側）
    float toolbarWidth = 110.0f;
    if ( ImGuiWindow* toolbarWindow = ImGui::FindWindowByName("##IconToolbar") ) { toolbarWidth = toolbarWindow->Size.x; }
    const bool drawerOnLeft = ( uiDrawerSide_ == 1 );
    const float toolbarX = drawerOnLeft
        ? viewport->WorkPos.x + viewport->WorkSize.x - toolbarWidth - 6.0f
        : viewport->WorkPos.x + 6.0f;
    ImGui::SetNextWindowPos(ImVec2(toolbarX, viewport->WorkPos.y + 6.0f), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID); // 別OSウィンドウ化させない（ゲーム画面の中に固定）
    // ボタンが増えて画面の高さを超えても外へはみ出さない（超えた分は中でスクロールできる）
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, viewport->WorkSize.y - 12.0f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGuiWindowFlags toolbarFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if ( !ImGui::Begin("##IconToolbar", nullptr, toolbarFlags) ) { ImGui::End(); return; }
    // 他の浮遊ウィンドウに隠れないよう毎フレーム最前面へ
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    const ImVec2 kIconSize { 48.0f, 50.0f }; // 上段アイコングリフ＋下段ラベルの2行ボタン
    int columnIndex = 0; // 2列に並べる（縦に長くなりすぎて画面から溢れないように）
    auto nextCell = [&](){
        if ( ( columnIndex & 1 ) == 1 ) { ImGui::SameLine(); }
        ++columnIndex;
    };
    auto newRow = [&](){ columnIndex = 0; };
    // ONのアイコンは青く光らせるトグルボタン
    auto iconButton = [&](const char* icon, const char* tooltip, bool active) -> bool {
        nextCell();
        if ( active ) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.65f, 0.95f, 1.0f));
        }
        bool pressed = ImGui::Button(icon, kIconSize);
        if ( active ) { ImGui::PopStyleColor(2); }
        if ( ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) ) { ImGui::SetTooltip("%s", tooltip); }
        return pressed;
    };
    if ( iconButton("\xEE\x9C\x80\nドック", "ドックUIへ戻る (F2)", false) ) {
        uiShell_ = UiShell::Dock;
        dockRelayoutPending_ = true; // 元のドック配置へ組み直す
        SaveUiConfig();
    }
    newRow();

    // --- ワークスペース（作業内容に合わせて一式切替）---
    ImGui::Separator();
    ImGui::TextDisabled("作業");
    bool workspaceApplied = false;
    // ※ラベルの「##ws〜」はID重複防止（下の「窓」セクションに同名ボタンがあるため。表示はされない）
    if ( iconButton("\xEE\x9C\x99\n設置##ws0", "設置：配置エディタだけ表示（画面が広く、クリック配置がやりやすい）", false) ) { ApplyWorkspace(0); workspaceApplied = true; }
    if ( iconButton("\xEE\x9E\x90\n調整##ws1", "調整：インスペクタ＋パフォーマンス（スライダーを動かしながら画を見る）", false) ) { ApplyWorkspace(1); workspaceApplied = true; }
    if ( iconButton("\xEE\x9D\xB4\n線路##ws2", "レール：レールエディタ＋カメラ＋ヒエラルキー", false) ) { ApplyWorkspace(2); workspaceApplied = true; }
    if ( iconButton("\xEE\x9D\xBB\n敵##ws3",   "敵配置：敵エディタだけ表示", false) ) { ApplyWorkspace(3); workspaceApplied = true; }
    if ( iconButton("\xEE\x9C\x91\n全閉##ws4", "全部閉じる（ゲーム画面のみ）", false) ) { ApplyWorkspace(4); workspaceApplied = true; }
    ( void ) workspaceApplied; // ドロワーが毎フレーム自動整列するので個別配置は不要
    newRow();

    // --- パネル個別開閉 ---
    ImGui::Separator();
    ImGui::TextDisabled("窓");
    for ( int i = 0; i < Panel_Count; ++i ) {
        if ( iconButton(kPanelIcons[i].icon, kPanelIcons[i].name, panelVisible_[i]) ) {
            panelVisible_[i] = !panelVisible_[i];
            SaveUiConfig();
        }
    }
    newRow();

    // --- その他の既存トグル ---
    ImGui::Separator();
    ImGui::TextDisabled("他");
    if ( iconButton("\xEE\xA5\x90\nノード", "ノードエディタ", showNodeEditor_) ) { showNodeEditor_ = !showNodeEditor_; }
    if ( iconButton("\xEE\xA2\x98\nBlend", "Blenderインポータ", showBlenderImporter_) ) { showBlenderImporter_ = !showBlenderImporter_; }
    if ( iconButton("\xEE\x9E\x8A\nデモ", "デモ展示（回転ブロック・オーラ・SDF卵・人形）", showDemo_) ) { showDemo_ = !showDemo_; }
    if ( iconButton("\xEE\x9C\x9C\n調整値", "調整項目 (GlobalVariables)", showGlobalVars_) ) { showGlobalVars_ = !showGlobalVars_; }
    newRow();

    // --- シーン切替・ファイル・設定 ---
    ImGui::Separator();
    ImGui::TextDisabled("シーン");
    if ( iconButton("\xEE\xA1\x8A\n題名", "タイトルシーンへ", false) ) { SceneManager::GetInstance()->ChangeSceneWithFade("TITLE"); }
    if ( iconButton("\xEE\x9D\xA8\n遊ぶ", "ゲームプレイシーンへ", false) ) { SceneManager::GetInstance()->ChangeSceneWithFade("GAMEPLAY"); }
    if ( iconButton("\xEE\x9D\x95\n動作", "アニメーションエディタへ", false) ) { SceneManager::GetInstance()->ChangeSceneWithFade("ANIMATION_EDITOR"); }
    newRow();
    ImGui::Separator();
    if ( iconButton("\xEE\x9D\x8E\n保存", "マップを上書き保存 (Ctrl+S)", false) ) { if ( levelEditor_ ) { levelEditor_->QuickSave(); } }
    if ( iconButton("\xEE\x9C\x93\n設定", "UI設定（フォント / スケール）", showUiSettings_) ) { showUiSettings_ = !showUiSettings_; }

    ImGui::End();

    // --- 右側ドロワー：表示中のパネルを右端の固定カラムへ自動で縦積みする ---
    //   浮遊ウィンドウが画面のあちこちに散らばる／中央のゲーム画面を覆う／前回位置の
    //   まま変な場所に開く、といった使いづらさをまとめて解消する。
    //   位置とサイズは毎フレーム名前指定で上書きするので、どのパネルも必ずここに現れる
    {
        ImGuiViewport* drawerViewport = ImGui::GetMainViewport();
        // 手動リサイズの取り込み：パネルの縁をドラッグして変えた幅をドロワー幅として採用する
        //（毎フレーム幅を強制する方式のままでも、採用→強制の順なのでドラッグが効く）
        for ( int i = 0; i < Panel_Count; ++i ) {
            if ( !panelVisible_[i] ) continue;
            ImGuiWindow* panelWindow = ImGui::FindWindowByName(kPanelIcons[i].windowTitle);
            if ( panelWindow && !panelWindow->Collapsed && fabsf(panelWindow->Size.x - uiDrawerWidth_) > 0.5f ) {
                uiDrawerWidth_ = ( std::max )( 300.0f, ( std::min )( 800.0f, panelWindow->Size.x ) );
                uiDrawerWidthDirty_ = true;
                break;
            }
        }
        if ( uiDrawerWidthDirty_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) ) {
            uiDrawerWidthDirty_ = false;
            SaveUiConfig(); // ドラッグを離したタイミングで保存
        }
        const float drawerWidth = uiDrawerWidth_;
        // タイトルの▼で畳んだパネルはタイトルバーぶんだけ場所を取り、
        // 残りの高さを開いているパネルで分け合う（使わないパネルを畳むと他が広くなる）
        const float collapsedHeight = ImGui::GetFrameHeight() + 4.0f;
        int   expandedCount  = 0;
        float collapsedTotal = 0.0f;
        bool  isCollapsed[Panel_Count] = {};
        int   visibleCount = 0;
        for ( int i = 0; i < Panel_Count; ++i ) {
            if ( !panelVisible_[i] ) continue;
            ++visibleCount;
            ImGuiWindow* panelWindow = ImGui::FindWindowByName(kPanelIcons[i].windowTitle);
            isCollapsed[i] = ( panelWindow && panelWindow->Collapsed );
            if ( isCollapsed[i] ) { collapsedTotal += collapsedHeight; } else { ++expandedCount; }
        }
        if ( visibleCount > 0 ) {
            // 配置（右/左）はUI設定で切替可能。左配置ならツールバーが右へ移る
            const float drawerX = drawerOnLeft
                ? drawerViewport->WorkPos.x
                : drawerViewport->WorkPos.x + drawerViewport->WorkSize.x - drawerWidth;
            const float expandedHeight = ( std::max )( 200.0f,
                ( drawerViewport->WorkSize.y - collapsedTotal ) / ( float ) ( std::max )( 1, expandedCount ) );
            float y = drawerViewport->WorkPos.y;
            for ( int i = 0; i < Panel_Count; ++i ) {
                if ( !panelVisible_[i] ) continue;
                const char* windowName = kPanelIcons[i].windowTitle;
                const float height = isCollapsed[i] ? collapsedHeight : expandedHeight;
                ImGui::SetWindowPos(windowName, ImVec2(drawerX, y), ImGuiCond_Always);
                ImGui::SetWindowSize(windowName, ImVec2(drawerWidth, expandedHeight), ImGuiCond_Always);
                y += height;
            }
        }
    }
#endif
}

void EditorManager::DrawUiSettingsWindow(){
#ifdef USE_IMGUI
    if ( !ImGui::Begin("UI設定", &showUiSettings_, ImGuiWindowFlags_AlwaysAutoResize) ) { ImGui::End(); return; }
    // フォント切替（起動時に読み込めたフォントの一覧から選ぶ）
    const auto& fonts = ImGuiManager::GetInstance()->GetFonts();
    if ( !fonts.empty() ) {
        if ( uiFontIndex_ < 0 || uiFontIndex_ >= ( int ) fonts.size() ) { uiFontIndex_ = 0; }
        if ( ImGui::BeginCombo("フォント", fonts[uiFontIndex_].name.c_str()) ) {
            for ( int i = 0; i < ( int ) fonts.size(); ++i ) {
                if ( ImGui::Selectable(fonts[i].name.c_str(), i == uiFontIndex_) ) {
                    uiFontIndex_ = i;
                    ImGui::GetIO().FontDefault = fonts[i].font; // 次フレームから反映
                    SaveUiConfig();
                }
            }
            ImGui::EndCombo();
        }
    }
    if ( ImGui::SliderFloat("UIスケール", &uiFontScale_, 0.7f, 1.6f, "%.2fx") ) {
        ImGui::GetStyle().FontScaleMain = uiFontScale_;
    }
    if ( ImGui::IsItemDeactivatedAfterEdit() ) { SaveUiConfig(); }
    ImGui::SliderFloat("ドロワー幅", &uiDrawerWidth_, 300.0f, 800.0f, "%.0fpx");
    if ( ImGui::IsItemDeactivatedAfterEdit() ) { SaveUiConfig(); }
    const char* kDrawerSideNames[] = { "右側（ツールバー左）", "左側（ツールバー右）" };
    if ( ImGui::Combo("ドロワー位置", &uiDrawerSide_, kDrawerSideNames, 2) ) { SaveUiConfig(); }
    if ( ImGui::Checkbox("起動時にエディタを表示（F1不要）", &uiAutoShowEditor_) ) { SaveUiConfig(); }
    ImGui::TextDisabled("F2: ドックUI ⇔ アイコンUI（設定は自動保存）\n"
                        "パネルはタイトルの▼で畳める（畳むと他のパネルが広がる）\n"
                        "パネルの縁ドラッグでもドロワー幅を変えられる\n"
                        "ゲームビュー右クリック＝その場に敵/コイン/ブロックを配置");
    ImGui::End();
#endif
}

// ワークスペース：作業内容に合わせてパネル一式を切り替える（0=設置/1=調整/2=レール/3=敵/4=全部閉じる）
//   ドック・アイコン両モード共通の機能。今のモード側のパネル表示設定に適用する
void EditorManager::ApplyWorkspace(int index){
    bool enable[Panel_Count] = {};
    switch ( index ) {
    case 0: enable[Panel_Items] = true;  enable[Panel_Control] = true; break;
    case 1: enable[Panel_Inspector] = true; enable[Panel_Perf] = true; enable[Panel_Control] = true; break;
    case 2: enable[Panel_Rail] = true; enable[Panel_Camera] = true; enable[Panel_Hierarchy] = true; break;
    case 3: enable[Panel_Enemy] = true; enable[Panel_Control] = true; break;
    default: break; // 4: 全部閉じる（ゲーム画面のみ）
    }
    for ( int i = 0; i < Panel_Count; ++i ) {
        if ( uiShell_ == UiShell::Dock ) { dockPanelHidden_[i] = !enable[i]; }
        else                             { panelVisible_[i] = enable[i]; }
    }
    SaveUiConfig();
}

void EditorManager::SaveUiConfig() const{
    FILE* fp = nullptr;
    fopen_s(&fp, "resources/editor_ui.ini", "w");
    if ( !fp ) return;
    fprintf(fp, "shell=%d\nfont=%d\nscale=%.3f\ndrawer=%.0f\nside=%d\nautoshow=%d\npanels=",
            ( int ) uiShell_, uiFontIndex_, uiFontScale_, uiDrawerWidth_, uiDrawerSide_, uiAutoShowEditor_ ? 1 : 0);
    for ( int i = 0; i < Panel_Count; ++i ) { fputc(panelVisible_[i] ? '1' : '0', fp); }
    fputc('\n', fp);
    // ドックモード側の非表示パネル（1=非表示）。キー名は panels= と部分一致しないものにする
    fputs("dockhide=", fp);
    for ( int i = 0; i < Panel_Count; ++i ) { fputc(dockPanelHidden_[i] ? '1' : '0', fp); }
    fputc('\n', fp);
    fclose(fp);
}

void EditorManager::LoadUiConfig(){
    FILE* fp = nullptr;
    fopen_s(&fp, "resources/editor_ui.ini", "r");
    if ( fp ) {
        // 行構造に依存しないよう、全体を読んでキーごとに拾う
        char buffer[256] = {};
        fread(buffer, 1, sizeof(buffer) - 1, fp);
        fclose(fp);
        const char* found = nullptr;
        if ( ( found = strstr(buffer, "shell=") ) )  { uiShell_ = ( atoi(found + 6) == 1 ) ? UiShell::Icon : UiShell::Dock; }
        if ( ( found = strstr(buffer, "font=") ) )   { uiFontIndex_ = atoi(found + 5); }
        if ( ( found = strstr(buffer, "scale=") ) )  {
            float scale = ( float ) atof(found + 6);
            uiFontScale_ = ( scale > 0.3f && scale < 3.0f ) ? scale : 1.0f;
        }
        if ( ( found = strstr(buffer, "drawer=") ) ) {
            float width = ( float ) atof(found + 7);
            uiDrawerWidth_ = ( width >= 300.0f && width <= 800.0f ) ? width : 460.0f;
        }
        if ( ( found = strstr(buffer, "side=") ) )     { uiDrawerSide_ = ( atoi(found + 5) == 1 ) ? 1 : 0; }
        if ( ( found = strstr(buffer, "autoshow=") ) ) {
            uiAutoShowEditor_ = ( atoi(found + 9) == 1 );
            if ( uiAutoShowEditor_ ) { isEditorActive_ = true; } // 起動時からエディタを開く設定
        }
        if ( ( found = strstr(buffer, "panels=") ) ) {
            found += 7;
            for ( int i = 0; i < Panel_Count && ( found[i] == '0' || found[i] == '1' ); ++i ) {
                panelVisible_[i] = ( found[i] == '1' );
            }
        }
        if ( ( found = strstr(buffer, "dockhide=") ) ) {
            found += 9;
            for ( int i = 0; i < Panel_Count && ( found[i] == '0' || found[i] == '1' ); ++i ) {
                dockPanelHidden_[i] = ( found[i] == '1' );
            }
        }
    } else {
        // 初回は「設置ワークスペース」相当（アイコンモードにした時に画面が広い状態から始まる）
        panelVisible_[Panel_Items]   = true;
        panelVisible_[Panel_Control] = true;
    }
#ifdef USE_IMGUI
    const auto& fonts = ImGuiManager::GetInstance()->GetFonts();
    if ( uiFontIndex_ > 0 && uiFontIndex_ < ( int ) fonts.size() ) {
        ImGui::GetIO().FontDefault = fonts[uiFontIndex_].font;
    }
    ImGui::GetStyle().FontScaleMain = uiFontScale_;
#endif
    // ドックモードは起動時に必ず標準レイアウトへ組み直す。
    //   アイコンモードのセッションを挟むと imgui.ini に「空のドックノード＋浮遊ウィンドウ」という
    //   中途半端な状態が残り、初期化スキップ判定をすり抜けてバラバラのまま起動してしまうため
    dockRelayoutPending_ = true;
}

void EditorManager::SetCamera(const Camera* camera){
    editorCamera_ = camera; // ギズモ計算でも使う
    if ( levelEditor_ ) {
        levelEditor_->SetCamera(camera);
    }
}

// レール編集データの公開（levelEditor_ → RailEditor へ委譲）
int EditorManager::GetRailEditVersion() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetVersion() : 0;
}
const std::vector<std::vector<Vector3>>& EditorManager::GetEditorRailLines() const{
    static const std::vector<std::vector<Vector3>> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailLines() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailTypes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailTypes() : kEmpty;
}
const std::vector<Vector4>& EditorManager::GetEditorRailMotions() const{
    static const std::vector<Vector4> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotions() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailGroundTypes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGroundTypes() : kEmpty;
}
const std::vector<std::vector<int>>& EditorManager::GetEditorRailNodeHoles() const{
    static const std::vector<std::vector<int>> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailNodeHoles() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailVisible() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailVisible() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailLineModes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailLineModes() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailRoadModes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailRoadModes() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailEndPlazas() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailEndPlazas() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailGuideRails() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideRails() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailGuideStarts() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideStarts() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailGuideEnds() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideEnds() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailGuideModes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideModes() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailGuideAligns() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideAligns() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailGuideDwells() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailGuideDwells() : kEmpty;
}
const std::vector<CoinData>& EditorManager::GetEditorCoins() const{
    static const std::vector<CoinData> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetCoins() : kEmpty;
}
const std::vector<BlockData>& EditorManager::GetEditorBlocks() const{
    static const std::vector<BlockData> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetBlocks() : kEmpty;
}
int EditorManager::GetEditorBlockVersion() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetBlockVersion() : 0;
}
bool EditorManager::IsEditorBlockPaintMode() const{
    // 配置エディタパネルを閉じている間はペイントモードも実質OFF扱いにする。
    //   フラグだけ残ると「見えないペイントモード」が入りっぱなしになり、
    //   敵/コイン/ブロックのつかみや右クリックメニューまで全部効かなくなる事故が起きる
    if ( !IsPanelVisible(Panel_Items) ) return false;
    return levelEditor_ ? levelEditor_->GetRailEditor()->IsBlockPaintMode() : false;
}
bool EditorManager::IsEditorBlockEraseMode() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->IsBlockEraseMode() : false;
}
int EditorManager::GetEditorBlockPaintType() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetBlockPaintType() : 0;
}
int EditorManager::GetEditorBlockPaintShape() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetBlockPaintShape() : 0;
}
bool EditorManager::HasEditorBlock(int rail, float dist, int level, float side) const{
    return levelEditor_ ? ( levelEditor_->GetRailEditor()->FindBlock(rail, dist, level, side) >= 0 ) : false;
}
void EditorManager::AddEditorBlock(int rail, float dist, int level, float side, int type){
    if ( levelEditor_ ) { levelEditor_->GetRailEditor()->AddBlock(rail, dist, level, side, type); }
}
void EditorManager::RemoveEditorBlock(int rail, float dist, int level, float side){
    if ( levelEditor_ ) { levelEditor_->GetRailEditor()->RemoveBlock(rail, dist, level, side); }
}
bool EditorManager::GetEditorSelectedNode(int& outRail, Vector3& outPos) const{
    if ( !levelEditor_ ) return false;
    RailEditor* railEditor = levelEditor_->GetRailEditor();
    int node = railEditor->GetSelectedRailNode();
    if ( node < 0 ) return false;
    int rail = railEditor->GetCurrentRailIndex();
    if ( !railEditor->GetNodePosOf(rail, node, outPos) ) return false;
    outRail = rail;
    return true;
}
bool EditorManager::GetEditorRailMotionPreview() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->IsMotionPreview() : false;
}
void EditorManager::SetEditorRailMotionPreview(bool v){
    if ( levelEditor_ ) { levelEditor_->GetRailEditor()->SetMotionPreview(v); }
}
int EditorManager::GetEditorJointVisible() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetJointVisible() : 1;
}
const std::vector<int>& EditorManager::GetEditorRailMotionTypes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotionTypes() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailMotionPhases() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotionPhases() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailMotionTriggers() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotionTriggers() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailAppearTriggers() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailAppearTriggers() : kEmpty;
}
const std::vector<int>& EditorManager::GetEditorRailOneWay() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailOneWay() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailSpeedMuls() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailSpeedMuls() : kEmpty;
}
int EditorManager::GetEditorStartRail() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetStartRail() : 0;
}
int EditorManager::GetEditorStartNode() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetStartNode() : 0;
}
int EditorManager::GetEditorGoalRail() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetGoalRail() : -1;
}
int EditorManager::GetEditorGoalNode() const{
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetGoalNode() : 0;
}
const std::vector<LevelCameraZone>& EditorManager::GetEditorCameraZones() const{
    static const std::vector<LevelCameraZone> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetCameraZones() : kEmpty;
}

// 敵の配置データ（マップ保存に乗せる）を levelEditor_ へ委譲
void EditorManager::SetEditorEnemyData(const std::vector<LevelEnemyData>& e){
    if ( levelEditor_ ) levelEditor_->SetEnemyData(e);
}
const std::vector<LevelEnemyData>& EditorManager::GetEditorEnemyData() const{
    static const std::vector<LevelEnemyData> kEmpty;
    return levelEditor_ ? levelEditor_->GetEnemyData() : kEmpty;
}
int EditorManager::GetMapLoadVersion() const{
    return levelEditor_ ? levelEditor_->GetMapLoadVersion() : 0;
}


void EditorManager::End(){
#ifdef USE_IMGUI
    DirectXCommon* dxCommon = DirectXCommon::GetInstance();
    ImGuiManager::GetInstance()->End(dxCommon->GetCommandList());
#endif
}

void EditorManager::Finalize(){
    // SDF は D3D12 リソースを持つため、リークチェッカーより先にここで必ず解放する
    SDFManager::GetInstance()->Finalize();
    SDFVolumeObject::FinalizeShared(); // 3Dボリューム描画の共有パイプラインも同様に解放

    blenderImporter_.reset();
    fileEditor_.reset();
    levelEditor_.reset();
    gpuParticleEditor_.reset();
    perfMonitor_.reset();
    nodeEditor_.reset();
}

void EditorManager::SetParticleEmitter(GPUParticleEmitter* emitter){
    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->SetEmitter(emitter);
    }
    // ノードエディタの「パーティクル適用ノード」も同じエミッターを操作する
    if ( nodeEditor_ ) {
        nodeEditor_->SetParticleEmitter(emitter);
    }
}

// ノードエディタの「→ ゲーム値」ノードのターゲットを登録する（シーンから）
void EditorManager::RegisterNodeGameValue(const std::string& label, float* target, float minV, float maxV){
    if ( nodeEditor_ ) {
        nodeEditor_->RegisterGameValue(label, target, minV, maxV);
    }
}

void EditorManager::ClearNodeGameValues(){
    if ( nodeEditor_ ) {
        nodeEditor_->ClearGameValues();
    }
}