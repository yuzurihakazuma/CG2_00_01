#pragma once
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <Windows.h>
#include <wrl.h>
#include <cstdint>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

class Input{
public:
	// namespace�ȗ�
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;


	void Initialize();

	void Update();
	/// <summary>
	/// �L�[�̉������`�F�b�N
	/// </summary>
	/// <param name="keyNumber">�L�[�ԍ�(DIK_0��)</param>
	/// <returns>������Ă��邩</returns>
	bool Pushkey(BYTE keyNumber);
	/// <summary>
	/// �L�[�̃g���K�[�`�F�b�N
	/// </summary>
	/// <param name="keyNumber">�L�[�ԍ�(DIK_0��)</param>
	/// <returns>�g���K�[��</returns>
	bool Triggerkey(BYTE keyNumber);


private:
	// �L�[�{�[�h�̃f�o�C�X
	ComPtr<IDirectInputDevice8> keyboard;
	// �S�L�[�̏��
	BYTE keys[256] = {};
	// �O��t���[���̑S�L�[�̏��
	BYTE preKeys[256] = {};
};

