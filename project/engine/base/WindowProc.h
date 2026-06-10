#pragma once
// --- 標準ライブラリ ---
#include <wtypes.h>
#include <cstdint>
#include <string>
#include <vector>
class WindowProc{
public:

	static WindowProc* GetInstance();

	/// <summary>
   /// ウィンドウの初期化
   /// </summary>
	void Initialize();

	/// <summary>
	/// ウィンドウの更新処理
	/// </summary>
	void Update();

	/// <summary>
	/// WindowsAPI の WndProc（メッセージ処理）
	/// </summary>
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);


	// -------------------- ウィンドウ状態管理 --------------------
	bool IsResized() const{ return isResized_; }

	/// <summary> ウィンドウサイズ変更フラグのクリア </summary>
	void ClearResizeFlag(){ isResized_ = false; }

	/// <summary>
	/// エクスプローラーからD&Dされたファイルのパス一覧を取り出す（取り出すと空になる）
	/// </summary>
	std::vector<std::string> PopDroppedFiles(){
		std::vector<std::string> result = std::move(droppedFiles_);
		droppedFiles_.clear();
		return result;
	}


	// -------------------- Getter 系 --------------------

   /// <summary> ウィンドウハンドルの取得 </summary>
	HWND GetHwnd() const{ return hwnd_; }

	/// <summary> ウィンドウが閉じられたかどうか </summary>
	bool GetIsClosed() const{ return isClosed_; }

	/// <summary> クライアント領域の横幅 </summary>
	int32_t GetClientWidth() const{ return kClientWidth_; }

	/// <summary> クライアント領域の縦幅 </summary>
	int32_t GetClientHeight() const{ return kClientHeight_; }

private:

	// コンストラクタを private にして外部からの生成を禁止
	WindowProc() = default;
	~WindowProc() = default;
	WindowProc(const WindowProc&) = delete;
	WindowProc& operator=(const WindowProc&) = delete;

private:
	// -------------------- ウィンドウ生成処理 --------------------

	/// <summary> ウィンドウクラスの設定 </summary>
	void SetupWindowClass(WNDCLASS& wc);

	/// <summary> ウィンドウクラスの登録 </summary>
	void RegisterWindowClass();

	/// <summary> クライアント領域のサイズ調整 </summary>
	void AdjustClientRect();

	/// <summary> メインウィンドウの作成 </summary>
	void CreateMainWindow();

	/// <summary> メインウィンドウの表示 </summary>
	void ShowMainWindow();


	// -------------------- ウィンドウ情報 --------------------

	WNDCLASS wc_ = {};     // ウィンドウクラス
	RECT wrc_ = {};         // ウィンドウサイズ調整用 RECT

	static constexpr int kDefaultClientWidth = 1920;   // デフォルト横幅
	static constexpr int kDefaultClientHeight = 1080;   // デフォルト縦幅

	static inline int32_t kClientWidth_ = kDefaultClientWidth;       // 現在のクライアント横幅
	static inline int32_t kClientHeight_ = kDefaultClientHeight;     // 現在のクライアント縦幅

	HWND hwnd_ = nullptr;          // ウィンドウハンドル
	static inline bool isClosed_ = false;  // ウィンドウが閉じられたかどうか
	static inline bool isResized_ = false; // ウィンドウサイズが変更されたかどうか

	// エクスプローラーからD&Dされたファイルのパス（WndProcで積む→PopDroppedFilesで消費）
	static inline std::vector<std::string> droppedFiles_;
};

