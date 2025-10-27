#include "Input.h"
#include <cassert>

using namespace logs;

void Input::Initialize(){

	//DirectInputの初期化
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;
	HRESULT inputKey = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast< void** >( directInput.GetAddressOf() ), nullptr);
	assert(SUCCEEDED(inputKey));
	// キーボードデバイスの生成
	inputKey = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
	assert(SUCCEEDED(inputKey));

	// 入力データ形式のセット
	inputKey = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(inputKey));
	// 排他制御レベルのセット
	inputKey = keyboard->SetCooperativeLevel(GetActiveWindow(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(inputKey));
	// キーボードの取得開始
	inputKey = keyboard->Acquire();
	assert(SUCCEEDED(inputKey));


}

void Input::Update(){
	// 前回のキー状態を保存
	memcpy(preKeys, keys, sizeof(keys));
	// 全キーの入力状態を取得
	HRESULT result = keyboard->GetDeviceState(sizeof(keys), keys);
	if ( FAILED(result) ) {
		
		// 入力取得に失敗 → 再取得を試みる
		result = keyboard->Acquire();
		result = keyboard->GetDeviceState(sizeof(keys), keys);

		// それでも失敗 → 全キーを未入力に初期化
		if ( FAILED(result) ) {
			ZeroMemory(keys, sizeof(keys));
		}
	}
}

/// <summary>
/// キーの押下をチェック
/// </summary>
/// <param name="keyNumber">キー番号(DIK_0等)</param>
bool Input::Pushkey(BYTE keyNumber){
	return ( keys[keyNumber] & 0x80 );
}

/// <summary>
/// キーのトリガーチェック
/// </summary>
/// <param name="keyNumber">キー番号(DIK_0等)</param>
bool Input::Triggerkey(BYTE keyNumber){
	return !( preKeys[keyNumber] & 0x80 ) && ( keys[keyNumber] & 0x80 );
}
