#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}





void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}


// ウィンドウプロシーシャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが破壊された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);

		return 0;
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);

}







// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	WNDCLASS wc{};
	// ウィンドウプロシーシャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名（なんでもいい）
	wc.lpszClassName = L"CG2WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	RegisterClass(&wc);

	// クライアント領域サイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,      // 利用するクラス名
		L"CG2",                // タイトルバーの文字(なんでもいい)
		WS_OVERLAPPEDWINDOW,   // よく見るウィンドウスタイル
		CW_USEDEFAULT,		   // 表示X座標(Windowsに任せる)
		CW_USEDEFAULT,		   // 表示Y座標(WindowsOSに任せる)
		wrc.right - wrc.left,  // ウィンドウ横幅
		wrc.bottom - wrc.top,  // ウィンドウ立幅
		nullptr,			   // 親ウィンドウハンドル
		nullptr,			   // メニューハンドル
		wc.hInstance,		   // インスタンスハンドル
		nullptr				   // オプション
	);
	// ウィンドウを表示
	ShowWindow(hwnd, SW_SHOW);

	MSG msg{};

	while (msg.message != WM_QUIT) {

		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

		} else {
			// ゲームの処理
		}


	}


	//出力ウィンドウへの文字出力
	Log("HelloWored\n");
	Log(ConvertString(std::format(L"WSTRING{}\n", kClientWidth)));
	

	return 0;

}