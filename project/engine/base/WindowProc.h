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


	/// <summary>
	/// Get
	/// </summary>
	/// <returns></returns>
	HWND GetHwnd() const{ return hwnd_; } // ウィンドウハンドルの取得
	bool GetIsClosed() const{ return isClosed_; } // ウィンドウが閉じられたかどうかの取得
	int32_t GetClientWidth() const{ return kClientWidth_; } // クライアント領域の横幅の取得
	int32_t GetClientHeight() const{ return kClientHeight_; } // クライアント領域の縦幅の取得


private:
	
	void SetupWindowClass(WNDCLASS& wc); // ウィンドウクラスの設定
	void RegisterWindowClass(); // ウィンドウクラスの登録
	void AdjustClientRect(); // クライアント領域の調整
	void CreateMainWindow(); // メインウィンドウの作成
	void ShowMainWindow(); // メインウィンドウの表示



	WNDCLASS wc_ = {}; // ウィンドウクラス
	
	
	static constexpr int kDefaultClientWidth = 1280; // デフォルトのクライアント領域の横幅
	static constexpr int kDefaultClientHeight = 720; // デフォルトのクライアント領域の縦幅

	
	int32_t kClientWidth_ = kDefaultClientWidth; // クライアント領域の横幅
	int32_t kClientHeight_ = kDefaultClientHeight; // クライアント領域の縦幅


	HWND hwnd_ = nullptr; // ウィンドウハンドル
	static inline bool isClosed_ = false; // ← 追加（staticならWndProcからアクセス可能）


	RECT wrc_ = {}; // ウィンドウサイズ調整用のRECT



};

