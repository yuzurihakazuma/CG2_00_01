#include "Input.h"
#include <cassert>

void Input::Initialize(){

	//DirectInputの初期化
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;
	HRESULT inputKey = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast< void** >( directInput.GetAddressOf() ), nullptr);
	assert(SUCCEEDED(inputKey));
	// キーボードデバイスの生成
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard = nullptr;
	inputKey = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
	assert(SUCCEEDED(inputKey));

	// 入力データ形式のセット
	inputKey = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(inputKey));
	// 排他制御レベルのセット
	inputKey = keyboard->SetCooperativeLevel(GetActiveWindow(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(inputKey));

}

void Input::Update(){

	HRESULT result;

	// キーボート情報の取得開始
	keyboard->Acquire();

	// 全キーの入力情報を取得する
	BYTE key[256] = {};
	keyboard->GetDeviceState(sizeof(key), key);
	// 現在のキー情報を保存
	BYTE keyPre[256] = {};
	keyboard->GetDeviceState(sizeof(keyPre), keyPre);

	// 前回のキー情報を保存
	memcpy(preKeys, keys, sizeof(keys));

	// キーボード情報の取得開始
	result = keyboard->Acquire();
	// 全キーの入力情報を取得する
	result = keyboard->GetDeviceState(sizeof(keys), keys);


}

bool Input::Pushkey(BYTE keyNumber){
	return false;
}

bool Input::Triggerkey(BYTE keyNumber){
	return false;
}
