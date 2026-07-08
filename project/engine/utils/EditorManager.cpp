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
#include "engine/utils/GlobalVariables.h"
#include "engine/3d/obj/SkinnedObj3d.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/camera/Camera.h"
#include "engine/camera/DebugCamera.h"
#include "engine/math/Matrix4x4.h"
#include <cmath>
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

    // 性能モニター（master_engine から移植）
    perfMonitor_ = std::make_unique<PerformanceMonitor>();
    perfMonitor_->Initialize();

    // ファイルエディタ（Project風。master_engine から移植）
    fileEditor_ = std::make_unique<FileEditor>();
    fileEditor_->Initialize();

    // ノードエディタ（ブループリント風。master_engine から移植）
    nodeEditor_ = std::make_unique<NodeEditor>();
    nodeEditor_->Initialize();
}



void EditorManager::Begin(){
    // フレーム先頭（コマンドリストが閉じている安全なタイミング）で
    // ファイルエディタのサムネイル画像をまとめて読み込む。
    if ( fileEditor_ ) {
        fileEditor_->ProcessPendingThumbnails();
    }
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

    // ノードグラフの実行も常に行う（ウィンドウが閉じていても適用ノードが作用する）
    if ( nodeEditor_ ) {
        nodeEditor_->Update();
    }

    if ( !isEditorActive_ ) {
        // エディタ非表示（フルスクリーン）ではホイールズームを常に許可
        if ( debugCamera_ ) { debugCamera_->SetGameViewHovered(true); }
        return;
    }

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
            ImGui::Separator();
            ImGui::Checkbox("ノードエディタ", &showNodeEditor_);
            ImGui::Checkbox("Blenderインポータ", &showBlenderImporter_);
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
    ImGui::Begin("Game View");
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
    const bool   imageHovered = ImGui::IsItemHovered();

    // デバッグカメラのホイールズームは Game View にマウスがある時だけ許可する
    // （レールエディタ等のパネル上でスクロールしてもカメラがズームしないように）。
    if ( debugCamera_ ) { debugCamera_->SetGameViewHovered(imageHovered); }

    if ( editorCamera_ ) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(imgMin.x, imgMin.y, imgSize.x, imgSize.y);

        Matrix4x4 view = editorCamera_->GetViewMatrix();
        Matrix4x4 proj = editorCamera_->GetProjectionMatrix();

        // レール編集（ノードのギズモ／クリック追加）は Edit モード時のみ
        const bool railEditMode = ( currentMode_ == EngineMode::Edit );

        // レール編集は RailEditor クラスへ分離。Game View 上の操作はこの re を介して行う。
        RailEditor* re = levelEditor_ ? levelEditor_->GetRailEditor() : nullptr;

        // --- (A) レール選択ギズモ：選択ノード群（1個〜路線全体〜複数レール）をまとめて移動 ---
        bool railNodeGizmo = false;
        if ( railEditMode && re ) {
            const auto& sel = re->GetMultiSelection();
            if ( !sel.empty() ) {
                railNodeGizmo = true;

                // ドラッグしていない間は毎フレーム選択中心をピボットに取り直す。
                // ドラッグ中はピボットを保持し、移動量（差分）だけを全ノードへ配る。
                if ( !railSelDragging_ ) { railSelPivot_ = re->GetSelectionCenter(); }

                Matrix4x4 world = MatrixMath::MakeAffine({ 1.0f,1.0f,1.0f }, { 0.0f,0.0f,0.0f }, railSelPivot_);

                // グリッド刻み：Ctrl を押しながらドラッグした時だけ一定刻みで動く。
                //   Ctrl+Shift はさらに細かい 1/10 刻み。そのままドラッグは滑らかな自由移動、
                //   Shift 単独は移動量を1/10にした微調整。刻み幅はレールエディタの「グリッドサイズ」。
                float snap[3] = { 0.0f, 0.0f, 0.0f };
                float* snapPtr = nullptr;
                if ( ImGui::GetIO().KeyCtrl ) {
                    float g = re->GetRailGridSize();
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
                            re->SetRailNodePos(sel[0].node, np);
                        } else {
                            // 複数ノード or 微調整：形を保ったまま平行移動
                            re->TranslateSelection(delta);
                        }
                    }
                    railSelPivot_ = np;
                } else {
                    railSelDragging_ = false;
                }
            } else {
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
        if ( railEditMode && re ) {
            Matrix4x4 vp    = editorCamera_->GetViewProjectionMatrix();
            Matrix4x4 invVP = MatrixMath::Inverse(vp);
            ImVec2 mouse = ImGui::GetMousePos();
            const bool gizmoActive = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

            // world→screen 投影（カメラ後方は false）
            auto project = [&]( const Vector3& p, ImVec2& out ) -> bool {
                float cw = p.x * vp.m[0][3] + p.y * vp.m[1][3] + p.z * vp.m[2][3] + vp.m[3][3];
                if ( cw <= 0.0001f ) return false;
                float cx = p.x * vp.m[0][0] + p.y * vp.m[1][0] + p.z * vp.m[2][0] + vp.m[3][0];
                float cy = p.x * vp.m[0][1] + p.y * vp.m[1][1] + p.z * vp.m[2][1] + vp.m[3][1];
                out.x = imgMin.x + ( cx / cw * 0.5f + 0.5f ) * imgSize.x;
                out.y = imgMin.y + ( 1.0f - ( cy / cw * 0.5f + 0.5f ) ) * imgSize.y;
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
                float h = re->GetRailDrawHeight();

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
            const int railCount = re->GetRailCount();
            if ( imageHovered ) {
                for ( int rr = 0; rr < railCount; ++rr ) {
                    const int nodeCount = re->GetNodeCountOf(rr);
                    for ( int i = 0; i < nodeCount; ++i ) {
                        Vector3 p; if ( !re->GetNodePosOf(rr, i, p) ) continue;
                        ImVec2 s; if ( !project(p, s) ) continue;
                        float d = std::sqrt(( s.x - mouse.x ) * ( s.x - mouse.x ) + ( s.y - mouse.y ) * ( s.y - mouse.y ));
                        if ( d < hoverBest ) { hoverBest = d; hoverRail = rr; hoverIdx = i; }
                    }
                    for ( int i = 0; i + 1 < nodeCount; ++i ) {
                        Vector3 a, b;
                        if ( !re->GetNodePosOf(rr, i, a) || !re->GetNodePosOf(rr, i + 1, b) ) continue;
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
            const bool stampPending = re->HasPendingStamp();
            if ( stampPending ) {
                Vector3 g;
                bool onGround = groundAt(mouse, g);

                // ゴースト（設置される形）をマウス位置に重ねて表示
                if ( onGround ) {
                    ImDrawList* gdl = ImGui::GetWindowDrawList();
                    const auto& shape = re->GetPendingStamp();
                    ImVec2 prevS; bool prevOk = false;
                    for ( const auto& rel : shape ) {
                        Vector3 wp = { g.x + rel.x, g.y + rel.y, g.z + rel.z };
                        ImVec2 s; bool ok = project(wp, s);
                        if ( ok ) {
                            gdl->AddCircleFilled(s, 3.0f, IM_COL32(90, 220, 255, 230));
                            if ( prevOk ) gdl->AddLine(prevS, s, IM_COL32(90, 220, 255, 200), 2.0f);
                        }
                        prevS = s; prevOk = ok;
                    }
                }
                // クリックで設置 / 右クリック・Esc で中止
                if ( imageHovered && ImGui::IsMouseClicked(0) && !gizmoActive && onGround ) {
                    re->PlaceStamp(g);
                }
                if ( ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ) {
                    re->CancelStamp();
                }
            }

            // ===== (C) キーボードでノード操作（Game View にマウスがある時のみ）=====
            //   矢印=グリッド1マスXZ移動 / Q,E=上下Y / Shift併用=1/10マスの微調整
            //   Delete=削除 / Ctrl+D=路線複製
            if ( imageHovered && !ImGui::IsMouseDown(1) && !ImGui::GetIO().WantTextInput ) {
                float gs = re->GetRailGridSize();
                if ( shiftHeld ) gs *= 0.1f; // Shift＋移動キー = 微調整
                Vector3 kd { 0.0f, 0.0f, 0.0f };
                if ( ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true) ) kd.x -= gs;
                if ( ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ) kd.x += gs;
                if ( ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true) ) kd.z += gs; // 奥
                if ( ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true) ) kd.z -= gs; // 手前
                if ( ImGui::IsKeyPressed(ImGuiKey_E,          true) ) kd.y += gs; // 上
                if ( ImGui::IsKeyPressed(ImGuiKey_Q,          true) ) kd.y -= gs; // 下
                if ( kd.x != 0.0f || kd.y != 0.0f || kd.z != 0.0f ) {
                    re->TranslateSelection(kd);
                }
                if ( ImGui::IsKeyPressed(ImGuiKey_Delete, false) ) {
                    re->DeleteSelectedNodes();
                }
                if ( ctrlHeld && ImGui::IsKeyPressed(ImGuiKey_D, false) ) {
                    re->DuplicateRail(re->GetCurrentRailIndex());
                }
            }

            // --- 左クリック（押した瞬間）。スタンプ配置待ちの時は通常クリックを無効化 ---
            if ( !stampPending && imageHovered && ImGui::IsMouseClicked(0) && !gizmoActive && !railRubberActive_ ) {
                if ( hoverIdx >= 0 ) {
                    if ( shiftHeld ) {
                        re->AddToSelection(hoverRail, hoverIdx);     // Shift+クリック＝追加選択
                    } else {
                        re->SelectSingleNode(hoverRail, hoverIdx);   // ノード単体を選択
                    }
                } else if ( re->IsFreehand() && re->IsRailDrawMode() ) {
                    railFreehandStroking_ = true;                              // 一筆書き開始
                } else if ( segIdx >= 0 ) {
                    if ( ctrlHeld ) {
                        // Ctrl+線クリック → その位置にノードを挿入（誤挿入防止のため修飾キー必須に）
                        re->SetCurrentRail(segRail);
                        re->InsertRailNode(segIdx, segPoint);
                    } else {
                        // 線クリック → 路線まるごと選択（ギズモでレール全体を移動できる）
                        re->SelectWholeRail(segRail);
                    }
                } else if ( re->IsRailDrawMode() ) {
                    Vector3 g; if ( groundAt(mouse, g) ) re->AppendRailNodeAt(g); // 末尾に追加
                } else {
                    // 何もない所 → 矩形選択を開始（動かさず離せばクリック扱い＝選択解除）
                    railRubberActive_ = true;
                    railRubberStartX_ = mouse.x;
                    railRubberStartY_ = mouse.y;
                }
            }

            // --- 矩形選択：ドラッグ中は枠を描き、離した時に囲んだノードをまとめて選択 ---
            if ( railRubberActive_ ) {
                float x0 = std::min(railRubberStartX_, mouse.x), x1 = std::max(railRubberStartX_, mouse.x);
                float y0 = std::min(railRubberStartY_, mouse.y), y1 = std::max(railRubberStartY_, mouse.y);
                ImDrawList* rdl = ImGui::GetWindowDrawList();
                rdl->AddRectFilled({ x0, y0 }, { x1, y1 }, IM_COL32(90, 180, 255, 40));
                rdl->AddRect({ x0, y0 }, { x1, y1 }, IM_COL32(90, 180, 255, 200), 0.0f, 0, 1.5f);

                if ( !ImGui::IsMouseDown(0) ) {
                    railRubberActive_ = false;
                    if ( ( x1 - x0 ) + ( y1 - y0 ) > 8.0f ) {
                        // 囲んだノードを全レールから選択（Shift中は既存の選択に追加）
                        if ( !shiftHeld ) { re->ClearMultiSelection(); }
                        int firstRail = -1;
                        for ( int rr = 0; rr < railCount; ++rr ) {
                            const int nodeCount = re->GetNodeCountOf(rr);
                            for ( int i = 0; i < nodeCount; ++i ) {
                                Vector3 p; if ( !re->GetNodePosOf(rr, i, p) ) continue;
                                ImVec2 s; if ( !project(p, s) ) continue;
                                if ( s.x >= x0 && s.x <= x1 && s.y >= y0 && s.y <= y1 ) {
                                    re->AddToSelection(rr, i);
                                    if ( firstRail < 0 ) firstRail = rr;
                                }
                            }
                        }
                        if ( firstRail >= 0 ) { re->SetCurrentRail(firstRail); }
                    } else {
                        // ほぼ動かさずに離した＝ただのクリック → 選択解除
                        re->ClearMultiSelection();
                        re->SetSelectedRailNode(-1);
                    }
                }
            }

            // --- 右クリック：ノード削除 ---
            if ( imageHovered && ImGui::IsMouseClicked(1) && !gizmoActive && hoverIdx >= 0 ) {
                re->SetCurrentRail(hoverRail);
                re->DeleteRailNode(hoverIdx);
            }

            // --- フリーハンド：ドラッグ中はグリッド間隔ごとに点を置く ---
            if ( !ImGui::IsMouseDown(0) ) railFreehandStroking_ = false;
            if ( railFreehandStroking_ && re->IsRailDrawMode() && !gizmoActive ) {
                Vector3 g;
                if ( groundAt(mouse, g) ) {
                    int c2 = re->GetCurrentRailNodeCount();
                    Vector3 last; bool hasLast = ( c2 > 0 ) && re->GetRailNodePos(c2 - 1, last);
                    float step = re->GetRailGridSize() * 0.9f;
                    float dxz = hasLast ? std::sqrt(( g.x - last.x ) * ( g.x - last.x ) + ( g.z - last.z ) * ( g.z - last.z )) : 1e9f;
                    if ( !hasLast || dxz >= step ) re->AppendRailNodeAt(g);
                }
            }

            // ===== (D) 視覚フィードバック（ImGui描画で重畳）=====
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int curRail = re->GetCurrentRailIndex();

            // 全レールの線を「タイプ色」で描画（編集データから直接引くので、クリック対象＝
            // ノードと完全に一致する。横=青(A/D移動) / 縦=橙(W/S移動) で役割が一目で分かる）。
            for ( int rr = 0; rr < railCount; ++rr ) {
                const bool isCur = ( rr == curRail );
                const bool railVisible = re->IsRailVisible(rr);
                const int  dtype = re->GetRailDisplayType(rr);
                // 非表示レール（連結用）はエディタでは淡いグレーで描く（編集できるように）
                ImU32 col = !railVisible
                    ? IM_COL32(170, 170, 175, isCur ? 200 : 110)
                    : ( ( dtype == 1 )
                        ? IM_COL32(255, 165, 70, isCur ? 235 : 150)   // 縦 = 橙
                        : IM_COL32(90, 180, 255, isCur ? 235 : 150) ); // 横 = 青
                // プレイヤーがスタートから到達できないレールは紫で警告（穴=赤と区別できる色）
                const bool reachable = re->IsRailReachable(rr);
                if ( railVisible && !reachable ) {
                    col = IM_COL32(200, 80, 255, isCur ? 235 : 160);
                }
                const ImU32 holeCol = IM_COL32(255, 60, 50, isCur ? 245 : 180); // 穴 = 赤
                const float baseW = isCur ? 2.5f : 1.5f;
                const int nodeCount = re->GetNodeCountOf(rr);
                // 各区間を中点で2分割し、「近い側のノードが穴か」で半分ずつ色を変える。
                // → 穴ノード1個なら、その周り（前後の半区間）だけが赤くなり分かりやすい。
                for ( int i = 1; i < nodeCount; ++i ) {
                    Vector3 pa, pb;
                    if ( !re->GetNodePosOf(rr, i - 1, pa) || !re->GetNodePosOf(rr, i, pb) ) continue;
                    ImVec2 sa, sb;
                    if ( !project(pa, sa) || !project(pb, sb) ) continue;
                    ImVec2 mid = { ( sa.x + sb.x ) * 0.5f, ( sa.y + sb.y ) * 0.5f };
                    bool holeA = re->IsNodeHole(rr, i - 1);
                    bool holeB = re->IsNodeHole(rr, i);
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

                // 到達できないレールの中央に補足を出す（なぜ紫なのかが一目で分かる）
                if ( railVisible && !reachable && nodeCount >= 2 ) {
                    Vector3 mid3;
                    if ( re->GetNodePosOf(rr, nodeCount / 2, mid3) ) {
                        ImVec2 sm;
                        if ( project(mid3, sm) ) {
                            dl->AddText({ sm.x + 8.0f, sm.y - 8.0f }, IM_COL32(220, 120, 255, 255), "未接続 (通れない)");
                        }
                    }
                }
            }

            // --- ジャンプ予測：Gap レールの未接続の端から弾道（放物線）を点線で描く ---
            //   シアン=どこかのレールへ着地できる（着地点に◎）/ 赤=届かない（先端に✕）。
            //   Safe レールの端はゲーム仕様で飛び出せない（クランプ）ので、
            //   飛べば届く相手がいる時だけ「端をGapにすれば渡れる」とヒントを出す。
            {
                const auto& groundTypes = re->GetRailGroundTypes();
                for ( int rr = 0; rr < railCount; ++rr ) {
                    if ( !re->IsRailVisible(rr) ) continue;
                    const int nodeCnt = re->GetNodeCountOf(rr);
                    if ( nodeCnt < 2 ) continue;
                    const int gt = ( rr < ( int ) groundTypes.size() ) ? groundTypes[rr] : 0;
                    for ( int side = 0; side < 2; ++side ) {
                        const bool front = ( side == 0 );
                        // 接続済みの端はゲームでは合流が優先されて飛び出せないので描かない
                        if ( re->IsRailEndConnected(rr, front) ) continue;

                        if ( gt == 1 ) { // 1 = GroundType::Gap（飛び出せる端）
                            std::vector<Vector3> arc;
                            int land = re->PredictJumpLanding(rr, front, true, &arc);
                            ImU32 jcol = ( land >= 0 ) ? IM_COL32(80, 230, 230, 210)  // シアン = 渡れる
                                                       : IM_COL32(255, 90, 90, 180);  // 赤 = 届かない
                            ImVec2 prevS {}; bool prevOk = false;
                            for ( size_t k = 0; k < arc.size(); ++k ) {
                                ImVec2 s; bool ok = project(arc[k], s);
                                if ( ok && prevOk && ( k % 2 == 0 ) ) dl->AddLine(prevS, s, jcol, 2.0f); // 1個おき＝点線
                                prevS = s; prevOk = ok;
                            }
                            if ( !arc.empty() ) {
                                ImVec2 e;
                                if ( project(arc.back(), e) ) {
                                    if ( land >= 0 ) {
                                        dl->AddCircle(e, 7.0f, jcol, 0, 2.5f); // 予測着地点
                                    } else {
                                        dl->AddLine({ e.x - 5, e.y - 5 }, { e.x + 5, e.y + 5 }, jcol, 2.0f);
                                        dl->AddLine({ e.x - 5, e.y + 5 }, { e.x + 5, e.y - 5 }, jcol, 2.0f);
                                    }
                                }
                            }
                        } else if ( gt == 0 ) { // Safe：端では止まる → 飛べば届くならヒント
                            if ( re->PredictJumpLanding(rr, front, true) >= 0 ) {
                                Vector3 p;
                                if ( re->GetNodePosOf(rr, front ? 0 : nodeCnt - 1, p) ) {
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
                const int nodeCount = re->GetNodeCountOf(highlightRail);
                ImVec2 prevS; bool prevOk = false;
                for ( int i = 0; i < nodeCount; ++i ) {
                    Vector3 p; if ( !re->GetNodePosOf(highlightRail, i, p) ) { prevOk = false; continue; }
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
                const int nodeCount = re->GetNodeCountOf(rr);
                for ( int i = 0; i < nodeCount; ++i ) {
                    Vector3 p; if ( !re->GetNodePosOf(rr, i, p) ) continue;
                    ImVec2 s; if ( !project(p, s) ) continue;
                    bool hole = re->IsNodeHole(rr, i);
                    dl->AddCircleFilled(s, hole ? 5.0f : ( isCur ? 3.5f : 3.0f ),
                        hole ? IM_COL32(255, 60, 50, 255) : col);
                }
            }
            // 選択中のノード群（オレンジの輪）
            for ( const auto& rsel : re->GetMultiSelection() ) {
                Vector3 p; ImVec2 s;
                if ( re->GetNodePosOf(rsel.rail, rsel.node, p) && project(p, s) ) {
                    dl->AddCircle(s, 8.0f, IM_COL32(255, 140, 40, 255), 0, 2.5f);
                }
            }
            // ホバー中のノード（黄色の輪）
            { Vector3 p; ImVec2 s;
              if ( hoverIdx >= 0 && re->GetNodePosOf(hoverRail, hoverIdx, p) && project(p, s) )
                  dl->AddCircle(s, 9.5f, IM_COL32(255, 235, 80, 255), 0, 2.0f); }
            // 配置ゴースト（マウス追加モードで、ノード/線分に当たっていない時）
            //   ・マウス直下の地面点（生の位置）に細い十字
            //   ・実際に置かれる位置（吸着後）に青い丸
            //   ・両者を線で結ぶ → 「吸着でここへ動く」が一目で分かる（ズレ感の解消）
            if ( imageHovered && re->IsRailDrawMode() && !gizmoActive && hoverIdx < 0 && segIdx < 0 && !stampPending ) {
                Vector3 g;
                if ( groundAt(mouse, g) ) {
                    Vector3 place = re->ComputePlacement(g);
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

    // 4. 全シーン共通のUI
    PostEffect::GetInstance()->DrawDebugUI();

    // スキニングUI（「詳細設定」へ合流するウィンドウは必ずドックスペースより後に Begin する。
    //  先に Begin するとそのウィンドウだけドッキングできなくなる）
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
    //   普段は邪魔なので非表示（表示メニューでON）。D&D取り込みは Update 側で処理されるため生きている。
    //   ※Blenderの自動リロード監視はパネル表示中のみ動く。
    if ( blenderImporter_ && showBlenderImporter_ ) {
        blenderImporter_->DrawDebugUI();
    }

    if ( gpuParticleEditor_ ) {
        gpuParticleEditor_->DrawDebugUI();
    }

    // ファイルエディタ（Project風のファイル閲覧・編集。master_engine から移植）
    if ( fileEditor_ ) {
        fileEditor_->DrawDebugUI();
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

    // 7. パフォーマンスモニター（master_engine から移植：履歴グラフ・メモリ/VRAM・ドローコール付き）
    if ( perfMonitor_ ) {
        perfMonitor_->DrawDebugUI(cpuUpdateTimeMs_, cpuDrawTimeMs_);
    }

    // 8. ノードエディタ（表示メニューでON/OFF。master_engine から移植）
    if ( nodeEditor_ && showNodeEditor_ ) {
        nodeEditor_->DrawDebugUI(&showNodeEditor_);
    }
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
const std::vector<int>& EditorManager::GetEditorRailMotionTypes() const{
    static const std::vector<int> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotionTypes() : kEmpty;
}
const std::vector<float>& EditorManager::GetEditorRailMotionPhases() const{
    static const std::vector<float> kEmpty;
    return levelEditor_ ? levelEditor_->GetRailEditor()->GetRailMotionPhases() : kEmpty;
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