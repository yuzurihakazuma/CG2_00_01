#pragma once
#include <wtypes.h>
#include<cstdint>

class WindowProc{
public:
	
	// ウィンドウの初期化
	void Initialize(WNDCLASS wc, const int32_t kClientWidth = 1280 , const int32_t kClientHeight=720);
	// ウィンドウの更新
	void Update();
	// ウィンドウの取得
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	HWND GetHwnd() const{ return hwnd_; }
	bool IsClosed() const{ return isClosed_; } 

private:
	
	WNDCLASS wc_ = {}; // ウィンドウクラス
	
	
	static constexpr int kDefaultClientWidth = 1280; // デフォルトのクライアント領域の横幅
	static constexpr int kDefaultClientHeight = 720; // デフォルトのクライアント領域の縦幅

	
	int32_t kClientWidth_ = kDefaultClientWidth; // クライアント領域の横幅
	int32_t kClientHeight_ = kDefaultClientHeight; // クライアント領域の縦幅


	HWND hwnd_ = nullptr; // ウィンドウハンドル
	static inline bool isClosed_ = false; // ← 追加（staticならWndProcからアクセス可能）
};

