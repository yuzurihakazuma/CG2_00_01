#define NOMINMAX
#include "ShaderCompiler.h"
#include <cassert>
#include <strsafe.h>
#include <format>
#include "LogManager.h"

extern logs::LogManager logManager;


bool ShaderCompiler::Initialize(){

	return true;

}

Microsoft::WRL::ComPtr<IDxcBlob> ShaderCompiler::CompileShader(const std::wstring& filePath, const wchar_t* profile, const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils, const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler, const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler){
	
	// 1.hlslファイルを読み込む

	// これからシェーダーをコンパイルする旨をログに出す
	logManager.Log(logManager.ConvertString(std::format(L"Begin CompileShader,path:{},profile:{}\n", filePath, profile)));
	// hislファイルを読む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));
	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

	// 2.Compileする
	LPCWSTR arguments[] = {
		filePath.c_str(), // コンバイル対象のhlslファイル名
		L"-E",L"main",    // エントリーポイントの指定。　基本的にmain以外にはしない
		L"-T",profile,    // ShaderProfileの設定
		L"-Zi",L"-Qembed_debug", // デバック用の情報を埋め込む
		L"-Od",           // 最適化を外しとく
		L"-Zpr"           // メモリレイアウトは行優先
	};
	// 実際にshaderをコンバイルする
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer, // 読み込んだファイル
		arguments,           // コンバイルオプション
		_countof(arguments), // コンバイルオプションの数
		includeHandler.Get(),      // includeが含んだ諸々
		IID_PPV_ARGS(&shaderResult) //コンバイル結果
	);
	// コンバイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// 3. 警告・エラーが出てないか確認する

	// 警告・エラーがでていたらログに出して止める
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if ( shaderError != nullptr && shaderError->GetStringLength() != 0 ) {
		logManager.Log(shaderError->GetStringPointer());
		// 警告・エラーダメゼッタイ
		assert(false);
	}

	// 4.Compile結果を受け取って返す

	// コンバイル結果から実行用のパイナリ部分を取得
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	// 成功したログを出す
	logManager.Log(logManager.ConvertString(std::format(L"Compile Succeeded,path:{},profile:{}\n", filePath, profile)));
	// 実行用のパイナリを返却
	return shaderBlob;

}

