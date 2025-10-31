#include "WindowProc.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// WindowProcの初期化
void WindowProc::Initialize(WNDCLASS wc, const int32_t kClientWidth, const int32_t kClientHeight){


	wc_ = wc; // ウィンドウクラス
	kClientWidth_ = kClientWidth; // クライアント領域の横幅
	kClientHeight_ = kClientHeight; // クライアント領域の縦幅

	// ウィンドウクラスの設定
	SetupWindowClass(wc_);
	// ウィンドウクラスの登録
	RegisterWindowClass();
	// クライアント領域の調整
	AdjustClientRect();
	// メインウィンドウの作成
	CreateMainWindow();
	// メインウィンドウの表示
	ShowMainWindow();

}
// ウィンドウクラスの設定
void WindowProc::SetupWindowClass(WNDCLASS& wc){

	// ウィンドウプロシージャを設定
	wc.lpfnWndProc = WndProc;

	// ウィンドウクラス名
	wc.lpszClassName = L"CG2WindowClass";

	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

}

// ウィンドウクラスの登録
void WindowProc::RegisterWindowClass(){

	// ウィンドウクラスを登録
	RegisterClass(&wc_);

}
// クライアント領域の調整
void WindowProc::AdjustClientRect(){

	// クライアント領域サイズ
	wrc_ = { 0, 0, kClientWidth_, kClientHeight_ };

	// クライアント領域を元に実際のサイズにwrc_を変更してもらう
	AdjustWindowRect(&wrc_, WS_OVERLAPPEDWINDOW, false);

}
// メインウィンドウの作成
void WindowProc::CreateMainWindow(){

	// ウィンドウの生成
	hwnd_ = CreateWindow(
		wc_.lpszClassName,      // 利用するクラス名
		L"CG2",                // タイトルバーの文字(なんでもいい)
		WS_OVERLAPPEDWINDOW,   // よく見るウィンドウスタイル
		CW_USEDEFAULT,		   // 表示X座標(Windowsに任せる)
		CW_USEDEFAULT,		   // 表示Y座標(WindowsOSに任せる)
		wrc_.right - wrc_.left,  // ウィンドウ横幅
		wrc_.bottom - wrc_.top,  // ウィンドウ立幅
		nullptr,			   // 親ウィンドウハンドル
		nullptr,			   // メニューハンドル
		wc_.hInstance,		   // インスタンスハンドル
		nullptr				   // オプション
	);



}
// メインウィンドウの表示
void WindowProc::ShowMainWindow(){

	// ウィンドウの表示
	ShowWindow(hwnd_, SW_SHOW);

}

// ウィンドウの更新
void WindowProc::Update(){

	MSG msg = {};
	// メッセージキューからメッセージを取得
	while ( PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) ) {
		// メッセージがWM_QUITならばアプリケーションを終了
		if ( msg.message == WM_QUIT ) {
			isClosed_ = true; // ウィンドウが閉じられたことを記録
			return;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

}


// ウィンドウプロシージャ
LRESULT WindowProc::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
	if ( ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) ) {
		return true;
	}

	switch ( msg ) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// デフォルトのウィンドウプロシージャを呼び出す
	return DefWindowProc(hwnd, msg, wparam, lparam);
}


