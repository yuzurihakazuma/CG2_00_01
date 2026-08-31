#include "ImGuiManager.h"
#ifdef USE_IMGUI
// --- 外部ライブラリ ---
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
// --- エンジン側のファイル ---
#include "engine/base/DirectXCommon.h"
#include "engine/base/WindowProc.h"
#include "engine/graphics/SrvManager.h"


ImGuiManager* ImGuiManager::GetInstance() {
    static ImGuiManager instance;
    return &instance;
}


void ImGuiManager::Initialize(WindowProc* windowProc, DirectXCommon* dxCommon){

#ifdef USE_IMGUI


    // 1. コンテキストの生成とスタイル設定
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // ドラッグ式の数値欄（DragFloat等）を「クリックだけ」でキーボード入力モードにする。
    // ノード座標や穴の位置などを直接数字で打ち込める（従来どおりドラッグでの増減も可能）
    io.ConfigDragClickToInputText = true;
    // ImGui 1.92系の「ID重複」開発者向け警告オーバーレイを無効化。
    //   同名ラベルのボタンが同時に見えると赤い警告が画面に被って操作の邪魔になるため
    //   （重複自体はコード側で ##suffix を付けて解消していく方針）
    io.ConfigDebugHighlightIdConflicts = false;
    // マルチビューポート：ImGuiウィンドウをゲームウィンドウの外へドラッグで出せる
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


    // OversampleH/V を 1 に下げることでフォントアトラスのテクスチャサイズを大幅に削減できる
    // デフォルト(3,1)だと日本語17000文字分で 4096x4096 になるが、1,1 なら約 1/3 以下に収まる
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    // 既定フォント（メイリオ）＋切替候補を読み込む（UI設定ウィンドウで選べる）。
    //   動的フォントアトラス方式（InitInfo初期化）なのでグリフは使った分だけ実行時に
    //   ラスタライズされる＝候補を複数読み込んでもメモリ・速度への影響はほぼ無い。
    //   全フォントに Windows 標準のアイコンフォント（Segoe MDL2, U+E700〜）を合成し、
    //   アイコンツールバーで本物のアイコングリフ（保存・歯車など）を使えるようにする
    static const ImWchar kIconGlyphRanges[] = { 0xE700, 0xE8FF, 0 };
    auto mergeIcons = [&io, &fontConfig, this](ImFont* baseFont, const char* name){
        if ( !baseFont ) return;
        ImFontConfig mergeConfig = fontConfig;
        mergeConfig.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segmdl2.ttf", 16.0f, &mergeConfig, kIconGlyphRanges);
        fonts_.push_back({ baseFont, name });
    };
    // 日本語フォント（そのまま使える）
    struct FontCandidate { const char* path; const char* name; };
    const FontCandidate kJapaneseFonts[] = {
        { "c:\\Windows\\Fonts\\meiryo.ttc",        "メイリオ" },
        { "c:\\Windows\\Fonts\\BIZ-UDGothicR.ttc", "BIZ UDゴシック" },
        { "c:\\Windows\\Fonts\\YuGothM.ttc",       "游ゴシック" },
        { "c:\\Windows\\Fonts\\UDDigiKyokashoN-R.ttc", "UDデジタル教科書体" },
    };
    for ( const auto& candidate : kJapaneseFonts ) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(candidate.path, 18.0f, &fontConfig, io.Fonts->GetGlyphRangesJapanese());
        mergeIcons(font, candidate.name); // 無いフォントはスキップ（環境差対応）
    }
    // 欧文フォント（ImGui同梱品を取込み。日本語はメイリオを合成して文字化けしない）
    const FontCandidate kLatinFonts[] = {
        { "resources/fonts/Roboto-Medium.ttf",   "Roboto（英＋和）" },
        { "resources/fonts/DroidSans.ttf",       "DroidSans（英＋和）" },
        { "resources/fonts/Karla-Regular.ttf",   "Karla（英＋和）" },
        { "resources/fonts/Cousine-Regular.ttf", "Cousine 等幅（英＋和）" },
    };
    for ( const auto& candidate : kLatinFonts ) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(candidate.path, 18.0f, &fontConfig, nullptr);
        if ( !font ) continue;
        ImFontConfig mergeConfig = fontConfig;
        mergeConfig.MergeMode = true;
        io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\meiryo.ttc", 18.0f, &mergeConfig, io.Fonts->GetGlyphRangesJapanese());
        mergeIcons(font, candidate.name);
    }

    ImGui::StyleColorsDark();

    // 外に出したウィンドウの見た目調整（角丸なし・背景不透明でOSウィンドウらしく）
    if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 2. Win32用初期化
    // WindowProcからハンドルを取得して渡す
    ImGui_ImplWin32_Init(windowProc->GetHwnd());

    // 3. DirectX12用初期化
    SrvManager* srvManager = dxCommon->GetSrvManager();

    // SRVヒープそのものも必要
    ID3D12DescriptorHeap* srvHeap = srvManager->GetDescriptorHeap();

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = dxCommon->GetDevice();
    init_info.CommandQueue = dxCommon->GetCommandQueue(); // ★これが必要！フォント転送の道
    init_info.NumFramesInFlight = static_cast<int>(dxCommon->GetBackBufferCount());
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    init_info.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; // 深度フォーマット(プロジェクトに合わせて変更可)
    init_info.SrvDescriptorHeap = srvHeap;

    // 最新ImGuiのルール：ハンドルはコールバック関数で渡す
    // ※要求されるたびに SrvManager から新しく確保する
    //   （マルチビューポート等で複数回呼ばれても安全）
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
        SrvManager* srv = SrvManager::GetInstance();
        uint32_t idx = srv->Allocate();
        *out_cpu = srv->GetCPUDescriptorHandle(idx);
        *out_gpu = srv->GetGPUDescriptorHandle(idx);
        };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};

    // 新しい構造体を渡して初期化！
    ImGui_ImplDX12_Init(&init_info);
#endif 
}

void ImGuiManager::Begin(){
#ifdef USE_IMGUI
    ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}
void ImGuiManager::End(ID3D12GraphicsCommandList* commandList){
#ifdef USE_IMGUI
    ImGui::Render(); // ImGui描画データ生成
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

    // マルチビューポート：外に出したウィンドウ（OSウィンドウ）を更新・描画する
    // （DX12バックエンドはビューポートごとに専用のコマンドリスト/スワップチェーンを持つ）
    ImGuiIO& io = ImGui::GetIO();
    if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault(nullptr, ( void* ) commandList);
    }
#endif
}
void ImGuiManager::Render(ID3D12GraphicsCommandList* commandList){
#ifdef USE_IMGUI
    // Endの中でRenderDrawDataを呼んでいるなら、ここはEndを呼ぶか、あるいは空でもよい
    End(commandList);
#endif
}
void ImGuiManager::Shutdown(){
#ifdef USE_IMGUI
    ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif
}

bool ImGuiManager::WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
#ifdef USE_IMGUI
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
#else
	return false; // ImGuiが処理しない場合はfalseを返す
#endif
}
