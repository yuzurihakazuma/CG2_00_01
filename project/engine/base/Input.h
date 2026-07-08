#pragma once
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800

// --- 標準ライブラリ・外部ライブラリ ---
#include <dinput.h>
#include <Xinput.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include "engine/math/struct.h" // Vector2

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib") 

class Input {
public:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	static Input* GetInstance();

public:
	// 初期化と更新
	void Initialize(HWND hwnd);
	void Update();

	void Finalize();

	// ==========================================
	//  キーボード
	// ==========================================
	bool Pushkey(BYTE keyNumber);
	bool Triggerkey(BYTE keyNumber);

	// ==========================================
	// 🖱 マウス
	// ==========================================
	// ボタン押下 (0:左, 1:右, 2:ホイール(中)ボタン)
	bool PushMouseButton(int buttonNumber);
	bool TriggerMouseButton(int buttonNumber);

	// マウスの移動量を取得（デバッグカメラで視点を回すのに使います！）
	float GetMouseMoveX();
	float GetMouseMoveY();
	float GetMouseWheel(); // ホイールの回転量

	// ==========================================
	// コントローラー
	// ==========================================
	// コントローラーが繋がっているか
	bool GetJoystickState();
	// ボタン押下 (XINPUT_GAMEPAD_A などを指定)
	bool PushJoystickButton(WORD button);
	bool TriggerJoystickButton(WORD button);
	// スティックの入力
	float GetLeftStickX();
	float GetLeftStickY();
	float GetRightStickX();
	float GetRightStickY();

	// スティックを Vector2 で取得（ラジアル・デッドゾーン処理済み、長さ0〜1）
	Vector2 GetLeftStick(float deadzone = 0.2f);
	Vector2 GetRightStick(float deadzone = 0.2f);

	// ==========================================
	// アクションマッピング（「ジャンプ」等にキー/パッドボタンを束ねる）
	//   BindAction("Jump", DIK_SPACE, XINPUT_GAMEPAD_A); のように複数回束ねられる
	//   padButton に 0 を渡すとキーボードのみ。
	// ==========================================
	void BindAction(const std::string& action, BYTE keyboardKey, WORD padButton = 0);
	void ClearAction(const std::string& action);     // そのアクションの束ねを全消去
	bool IsAction(const std::string& action);        // 押されている間 true
	bool IsActionTriggered(const std::string& action); // 押した瞬間だけ true

private:
	Input() = default;
	~Input() = default;
	Input(const Input&) = delete;
	Input& operator=(const Input&) = delete;

private:
	// キーボード用
	ComPtr<IDirectInputDevice8> keyboard;
	BYTE keys[256] = {};
	BYTE preKeys[256] = {};

	// マウス用
	ComPtr<IDirectInputDevice8> mouse;
	DIMOUSESTATE2 mouseState = {};
	DIMOUSESTATE2 preMouseState = {};

	// コントローラー用
	XINPUT_STATE joyState = {};
	XINPUT_STATE preJoyState = {};
	bool isJoyConnected = false;

	// アクション → (キーボードDIK, パッドボタン) の束ね。padButton=0 はキーボードのみ
	struct ActionBind{ BYTE key; WORD pad; };
	std::map<std::string, std::vector<ActionBind>> actions_;
};