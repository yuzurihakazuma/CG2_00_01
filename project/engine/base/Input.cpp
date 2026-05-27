#include "Input.h"
#include <cassert>
#include <cmath>

Input* Input::GetInstance() {
	static Input instance;
	return &instance;
}


void Input::Finalize() {
	// 取得状態を解放
	if (keyboard) {
		keyboard->Unacquire();
		keyboard.Reset();
	}
	if (mouse) {
		mouse->Unacquire();
		mouse.Reset();
	}
}

void Input::Initialize(HWND hwnd) {
	// DirectInputの初期化
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;
	HRESULT result = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(directInput.GetAddressOf()), nullptr);
	assert(SUCCEEDED(result));

	//  キーボードの初期化
	result = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
	assert(SUCCEEDED(result));
	// データフォーマットの設定
	result = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(result));
	// 協調レベルの設定（フォアグラウンド、非排他、Winキー無効）
	result = keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(result));
	keyboard->Acquire();

	//  マウスの初期化
	result = directInput->CreateDevice(GUID_SysMouse, mouse.GetAddressOf(), NULL);
	assert(SUCCEEDED(result));
	// 拡張マウスデータフォーマット
	result = mouse->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(result));
	// 協調レベルの設定（フォアグラウンド、非排他）
	result = mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(result));
	mouse->Acquire();
}

void Input::Update() {
	//  キーボードの更新
	memcpy(preKeys, keys, sizeof(keys));
	// デバイスの状態を取得
	HRESULT result = keyboard->GetDeviceState(sizeof(keys), keys);
	if (FAILED(result)) {
		// キーボードの状態が取得できない場合は、再度Acquireしてみる
		keyboard->Acquire();
		// 再度状態を取得
		keyboard->GetDeviceState(sizeof(keys), keys);
	}

	//  マウスの更新
	preMouseState = mouseState;
	// デバイスの状態を取得
	result = mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState);
	if (FAILED(result)) {
		// マウスの状態が取得できない場合は、再度Acquireしてみる
		mouse->Acquire();
		// 再度状態を取得
		mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState);
	}

	//  コントローラーの更新
	preJoyState = joyState;
	// プレイヤー0 (1P) の状態を取得
	DWORD dwResult = XInputGetState(0, &joyState);
	// コントローラーが接続されているかを判定
	isJoyConnected = (dwResult == ERROR_SUCCESS);
}

// =====================================
//  キーボード処理
// =====================================
bool Input::Pushkey(BYTE keyNumber) {
	// キーが押されているかを判定 (0x80は最上位ビットで、キーが押されているときにセットされる)
	return (keys[keyNumber] & 0x80);
}
bool Input::Triggerkey(BYTE keyNumber) {
	// キーが押された瞬間を判定 (前フレームでは押されていなくて、今フレームで押されている場合)
	return !(preKeys[keyNumber] & 0x80) && (keys[keyNumber] & 0x80);
}

// =====================================
//  マウス処理
// =====================================
bool Input::PushMouseButton(int buttonNumber) {
	// ボタンが押されているかを判定 (0x80は最上位ビットで、ボタンが押されているときにセットされる)
	return (mouseState.rgbButtons[buttonNumber] & 0x80) != 0;
}
bool Input::TriggerMouseButton(int buttonNumber) {
	// ボタンが押された瞬間を判定 (前フレームでは押されていなくて、今フレームで押されている場合)
	return !(preMouseState.rgbButtons[buttonNumber] & 0x80) && (mouseState.rgbButtons[buttonNumber] & 0x80);
}
float Input::GetMouseMoveX() {
	// マウスの移動量を取得 (前フレームからの相対値)
	return static_cast<float>(mouseState.lX);
}
float Input::GetMouseMoveY() {
	// マウスの移動量を取得 (前フレームからの相対値)
	return static_cast<float>(mouseState.lY);
}
float Input::GetMouseWheel() {
	// マウスホイールの回転量を取得 (前フレームからの相対値)
	return static_cast<float>(mouseState.lZ);
}

// =====================================
// コントローラー処理
// =====================================
bool Input::GetJoystickState() {
	// コントローラーが接続されているかを返す
	return isJoyConnected;
}
bool Input::PushJoystickButton(WORD button) {
	// ボタンが押されているかを判定 (XINPUT_GAMEPAD_Aなどのビットフラグを指定)
	if (!isJoyConnected) {
		return false;
	}
	// joyState.Gamepad.wButtonsは、どのボタンが押されているかをビットフラグで表しているため、指定したボタンのビットがセットされているかを判定します。
	return (joyState.Gamepad.wButtons & button) != 0;
}
bool Input::TriggerJoystickButton(WORD button) {
	// ボタンが押された瞬間を判定 (前フレームでは押されていなくて、今フレームで押されている場合)
	if (!isJoyConnected) {
		return false;
	}
	// preJoyState.Gamepad.wButtonsは、前フレームのボタンの状態をビットフラグで表しているため、指定したボタンのビットが前フレームではセットされていなくて、今フレームでセットされているかを判定します。
	return !(preJoyState.Gamepad.wButtons & button) && (joyState.Gamepad.wButtons & button);
}

// ※スティックは触っていなくても微妙に数値が入るため「デッドゾーン(遊び)」を設けています
bool Input::PushLeftTrigger(float threshold) {
	if (!isJoyConnected) {
		return false;
	}
	return (static_cast<float>(joyState.Gamepad.bLeftTrigger) / 255.0f) >= threshold;
}

float Input::GetLeftStickX() {
	if (!isJoyConnected) {
		return 0.0f;
	}
	float x = static_cast<float>(joyState.Gamepad.sThumbLX) / 32768.0f;
	return (std::abs(x) < 0.2f) ? 0.0f : x;
}
float Input::GetLeftStickY() {
	if (!isJoyConnected) {
		return 0.0f;
	}
	float y = static_cast<float>(joyState.Gamepad.sThumbLY) / 32768.0f;
	return (std::abs(y) < 0.2f) ? 0.0f : y;
}
float Input::GetRightStickX() {
	if (!isJoyConnected) {
		return 0.0f;
	}
	float x = static_cast<float>(joyState.Gamepad.sThumbRX) / 32768.0f;
	return (std::abs(x) < 0.2f) ? 0.0f : x;
}
float Input::GetRightStickY() {
	if (!isJoyConnected) {
		return 0.0f;
	}
	float y = static_cast<float>(joyState.Gamepad.sThumbRY) / 32768.0f;
	return (std::abs(y) < 0.2f) ? 0.0f : y;
}
