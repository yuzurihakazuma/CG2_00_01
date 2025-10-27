#pragma once
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include"LogManager.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")



class Input{
public:
	// namespace省略
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;


	void Initialize();

	void Update();
	/// <summary>
	/// キーの押下をチェック
	/// </summary>
	/// <param name="keyNumber">キー番号(DIK_0等)</param>
	/// <returns>押されているか</returns>
	bool Pushkey(BYTE keyNumber);
	/// <summary>
	/// キーのトリガーチェック
	/// </summary>
	/// <param name="keyNumber">キー番号(DIK_0等)</param>
	/// <returns>トリガーか</returns>
	bool Triggerkey(BYTE keyNumber);


private:
	// キーボードのデバイス
	ComPtr<IDirectInputDevice8> keyboard;
	// 全キーの状態
	BYTE keys[256] = {};
	// 前回フレームの全キーの状態
	BYTE preKeys[256] = {};
};

