#pragma once
// --- 標準・外部ライブラリ ---
#include "externals/imgui/imgui.h"
#include <d3d12.h>
#include <wtypes.h>
#include <string>
#include <vector>

// 前方宣言
class DirectXCommon;
class WindowProc;


class ImGuiManager{
public:

    // シングルトンインスタンスの取得
    static ImGuiManager* GetInstance();

    // -------------------- 初期化・終了処理 --------------------

    /// <summary>
    /// ImGui の初期化
    /// </summary>
    void Initialize(WindowProc* windowProc, DirectXCommon* dxCommon);

    /// <summary>
    /// フレーム開始処理
    /// </summary>
    void Begin();

    /// <summary>
    /// フレーム終了処理
    /// </summary>
    void End(ID3D12GraphicsCommandList* commandList); 

    /// <summary>
    /// ImGui の描画コマンドを発行
    /// </summary>
    void Render(ID3D12GraphicsCommandList* commandList);
    /// <summary>
    /// ImGui の破棄処理
    /// </summary>
    void Shutdown();


    // -------------------- メッセージ処理 --------------------

    /// <summary>
    /// Win32 メッセージの ImGui 用処理
    /// </summary>
    bool WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // -------------------- フォント --------------------
    // 起動時に読み込めたUIフォントの一覧（UI設定ウィンドウの切替候補）
    struct UiFont {
        ImFont* font = nullptr;
        std::string name;
    };
    const std::vector<UiFont>& GetFonts() const{ return fonts_; }
private:
    // コンストラクタを隠蔽
    ImGuiManager() = default;
    ~ImGuiManager() = default;
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    std::vector<UiFont> fonts_; // 読み込めたUIフォント（[0]=既定のメイリオ）
};

