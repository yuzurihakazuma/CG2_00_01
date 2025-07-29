#pragma once
#include <wtypes.h>
#include<cstdint>

class WindowProc{
public:
	
	// ウィンドウの初期化
	void Initialize(WNDCLASS wc ,const int32_t kClientWidth,const int32_t kClientHeight);
	// ウィンドウの更新
	void Update();
	// ウィンドウの取得
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	HWND GetHwnd() const{ return hwnd_; }
	bool IsClosed() const{ return isClosed_; } // ← 追加

private:
	
	WNDCLASS wc_ = {}; // ウィンドウクラス
	int32_t kClientWidth_ = 1280; // クライアント領域の横幅
	int32_t kClientHeight_ = 720; // クライアント領域の縦幅
	HWND hwnd_ = nullptr; // ウィンドウハンドル
	static inline bool isClosed_ = false; // ← 追加（staticならWndProcからアクセス可能）
};

