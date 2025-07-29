#pragma once
#include <wtypes.h>
#include<cstdint>

class WindowProc{
public:
	

	void Initialize(WNDCLASS wc ,const int32_t kClientWidth,const int32_t kClientHeight);

	void Update();

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
	
	WNDCLASS wc_ = {}; // ウィンドウクラス
	int32_t kClientWidth_ = 1280; // クライアント領域の横幅
	int32_t kClientHeight_ = 720; // クライアント領域の縦幅
	HWND hwnd_ = nullptr; // ウィンドウハンドル
};

