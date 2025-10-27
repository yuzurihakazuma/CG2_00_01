#include "Input.h"
#include <cassert>

void Input::Initialize(){

	//DirectInput�̏�����
	Microsoft::WRL::ComPtr<IDirectInput8> directInput;
	HRESULT inputKey = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast< void** >( directInput.GetAddressOf() ), nullptr);
	assert(SUCCEEDED(inputKey));
	// �L�[�{�[�h�f�o�C�X�̐���
	Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard = nullptr;
	inputKey = directInput->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
	assert(SUCCEEDED(inputKey));

	// ���̓f�[�^�`���̃Z�b�g
	inputKey = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(inputKey));
	// �r�����䃌�x���̃Z�b�g
	inputKey = keyboard->SetCooperativeLevel(GetActiveWindow(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(inputKey));

}

void Input::Update(){

	HRESULT result;

	// �L�[�{�[�g���̎擾�J�n
	keyboard->Acquire();

	// �S�L�[�̓��͏����擾����
	BYTE key[256] = {};
	keyboard->GetDeviceState(sizeof(key), key);
	// ���݂̃L�[����ۑ�
	BYTE keyPre[256] = {};
	keyboard->GetDeviceState(sizeof(keyPre), keyPre);

	// �O��̃L�[����ۑ�
	memcpy(preKeys, keys, sizeof(keys));

	// �L�[�{�[�h���̎擾�J�n
	result = keyboard->Acquire();
	// �S�L�[�̓��͏����擾����
	result = keyboard->GetDeviceState(sizeof(keys), keys);


}

bool Input::Pushkey(BYTE keyNumber){
	return false;
}

bool Input::Triggerkey(BYTE keyNumber){
	return false;
}
