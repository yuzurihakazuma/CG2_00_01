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
    // マルチビューポート：ImGuiウィンドウをゲームウィンドウの外へドラッグで出せる
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


    // OversampleH/V を 1 に下げることでフォントアトラスのテクスチャサイズを大幅に削減できる
    // デフォルト(3,1)だと日本語17000文字分で 4096x4096 になるが、1,1 なら約 1/3 以下に収まる
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\meiryo.ttc", 18.0f, &fontConfig, io.Fonts->GetGlyphRangesJapanese());

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
