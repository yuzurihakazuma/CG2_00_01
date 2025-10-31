#include"Matrix4x4.h"
#include <Windows.h>
#include <cstdint>
#include <string>
#include <format>
#include <filesystem>
#include <fstream>// ファイルを書いたり読み込んだりするライブラリ
#include <chrono> // 時間を扱うライブラリ 
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <dxcapi.h>
#include <numbers>
#include<sstream>
#include <wrl.h>
#include <xaudio2.h>
#define DIRECTINPUT_VERSION 0x0800 // DirectInputのバージョンを指定
#include <dinput.h>
// Debug用のあれやこれを使えるようにする
#include <dbghelp.h>
#include <strsafe.h>
#include "externals/DirectXTex/d3dx12.h"
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"Dbghelp.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")
#pragma comment(lib,"xaudio2.lib")
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

#pragma comment(lib, "ole32.lib")         // COMライブラリ CoInitializeEx などに必要
#pragma comment(lib, "shlwapi.lib")        // Shell API に必要
#pragma comment(lib, "windowscodecs.lib")  // WIC（テクスチャ読み込み）に必要

#include <random>


#include "DebugCamera.h"
#include "struct.h"
#include "WindowProc.h"
#include "LogManager.h"
#include "CrashDumper.h"
#include "ShaderCompiler.h"
#include "ImGuiManager.h"
#include"Input.h"
using namespace logs;
using namespace MatrixMath;





struct VertexData{
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};



struct Material{
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransfrom; // UV変換行列
};

struct TransformationMatrix{
	Matrix4x4 WVP;
	Matrix4x4 World;
};


struct ParticleForGPU{
	Matrix4x4 WVP;
	Matrix4x4 World;
	Vector4 color;
};

struct DirectionalLight{
	Vector4 color; // ライトの色
	Vector3 direction; // ライトの向き
	float intensity; // 輝度

};

struct MaterialData{
	std::string textrueFilePath; // テクスチャファイルのパス

};

struct ModelData{
	std::vector<VertexData> vertices; // 頂点データ
	std::vector<uint32_t> indices;
	MaterialData material;
};
// チャンクヘッダ
struct ChunkHeader{
	char id[4]; // チャンクID
	int32_t size; // チャンクのサイズ
};
// RIFFヘッダチャンク
struct RiffHeader{
	ChunkHeader chunk; // RIFF
	char type[4]; // WAVE
};
// FMTチャンク
struct ForamatChunk{
	ChunkHeader chunk; // FMT
	WAVEFORMATEX fmt;  // 波形フォーマット

};
// 音声データ
struct SoundData{
	// 波型フォーマット
	WAVEFORMATEX wfex;
	// バッファの先頭アドレス
	BYTE* pBuffer;
	// バッファのサイズ
	unsigned int bufferSize;

};
// ブレンドモード
enum BlendMods{
	// ブレンドなし
	kBlendModeNone,
	// 通常aブレンド:デフォルト Src * SrcA +Dest * (1-SrcA)
	kBlendModeNoraml,
	// 加算 Src *SrcA + Dest * 1
	kBlendModeAdd,
	// 減算 Dest* 1 - Src * SrcA
	kBlendModSubtract,
	// 乗算 Src * 0 + Dest * Src
	kBlendModeMultily,
	// スクリーン Src * (1 - Dest) + Dest * 1
	kBlendModeScreen,
	// 利用してはいけない
	kCountOfBlendMode,

};


// Transform変数を作る
Transform transform { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
Transform cameraTransfrom { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-10.0f} };
Transform transformSprite { {1.0f,1.0f,1.0f,},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
Transform uvTransformSprite { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

LogManager logManager;// ログマネージャーのインスタンス
Input input; // 入力クラスのインスタンス



// パーティクル
struct Particle{
	Transform transform; // パーティクルの座標変換情報
	Vector3 velocity; // パーティクルの速度
	Vector4 color; // パーティクルの色
	float lifeTime; // パーティクルの寿命
	float currentTime; // パーティクルの現在の生存時間
};

std::random_device seedGenerator; // 乱数の種を生成するオブジェクト
std::mt19937 randomEngine(seedGenerator()); // メルセンヌ・ツイスタの乱数エンジン




#pragma region リソースリークチェック

struct D3DResourceLeakChecker{
	~D3DResourceLeakChecker(){
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if ( SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))) ) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}


};

#pragma endregion







#pragma region Resource関数

Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, size_t sizeInBytes){
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
	// DXGIファクトリー
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties {};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;// UploadHeapを使う
	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourcceDesc {};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourcceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourcceDesc.Width = sizeInBytes;// リソースのサイズ。今回はVector4を3頂点分
	// バッファの場合はこれらは1にする決まり
	vertexResourcceDesc.Height = 1;
	vertexResourcceDesc.DepthOrArraySize = 1;
	vertexResourcceDesc.MipLevels = 1;
	vertexResourcceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにする決まり
	vertexResourcceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;



	hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourcceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexResource));

	return vertexResource;

}

#pragma endregion

#pragma region DescriptorHeap作成関数

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device, D3D12_DESCRIPTOR_HEAP_TYPE heapType,
	UINT numDescriptors, bool shaderVisible){

	// DXGIファクトリー
	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	// ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc {};
	descriptorHeapDesc.Type = heapType; // 連打―ターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescriptors; // ダブルバッファ用に2つ。多くても別に構わない。
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	// ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));
	return descriptorHeap;

}


#pragma endregion

#pragma region TextrueResource関数

DirectX::ScratchImage LoadTexture(const std::string& filePath){

	// テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image {};
	std::wstring filePathw = logManager.ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathw.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages {};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);

	// ミップマップ付きのデータを返す
	return mipImages;

}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, const DirectX::TexMetadata& metadata){

	// metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc {};
	resourceDesc.Width = UINT(metadata.width); // Textrueの幅
	resourceDesc.Height = UINT(metadata.height); // Textrueの高さ
	resourceDesc.MipLevels = UINT16(metadata.mipLevels); // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize); // 奥行きor配列Textrueの配列数
	resourceDesc.Format = metadata.format; //TextrueのFormat 
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textrueの次元数。普段使っているのは2次元

	// 利用するHeapの設定。非常に特殊な運用・
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // 細かい設定を行う

	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;

}

[[nodiscard]] // 03_00EX
Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource>& texture, const DirectX::ScratchImage& mipImages, const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList){

	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, static_cast< UINT >( subresources.size() ));
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = CreateBufferResource(device, intermediateSize);

	UpdateSubresources(commandList.Get(), texture.Get(), intermediateResource.Get(), 0, 0, static_cast< UINT >( subresources.size() ), subresources.data());

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(const Microsoft::WRL::ComPtr<ID3D12Device>& device, int32_t width, int32_t height){

	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc {};
	resourceDesc.Width = width; // Textureの幅
	resourceDesc.Height = height; // Textureの高さ
	resourceDesc.MipLevels = 1; // mipmap
	resourceDesc.DepthOrArraySize = 1; // 奥行きor 配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; //DepthStencilとして使う通知

	// 利用するHrapの設定
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue {};
	depthClearValue.DepthStencil.Depth = 1.0f; // 1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

	// Resorceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, // Heepの設定
		D3D12_HEAP_FLAG_NONE, // Heapの特殊な設定。特になし
		&resourceDesc, // Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
		&depthClearValue, //Clear最適値
		IID_PPV_ARGS(&resource)); // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));

	return resource;
}



#pragma endregion

#pragma region DescriptorHandle関数

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index){

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += ( descriptorSize * index );
	return handleCPU;
}
D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index){

	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += ( descriptorSize * index );
	return handleGPU;
}


#pragma endregion


#pragma region MatrialData構造体と読み込み関数

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename){
	MaterialData materialData; // MaterialDataを構築
	std::string line; // ファイルから読んだ1行を格納するもの
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める
	while ( std::getline(file, line) ){
		std::string identifier; // 行の先頭の識別子を格納する
		std::istringstream s(line); // 先頭の認別子を読む
		s >> identifier; // 先頭の識別子を読み込む

		// identifierに応じた処理
		if ( identifier == "map_Kd" ){
			std::string textureFilename; // テクスチャファイル名を格納する変数
			s >> textureFilename; // テクスチャファイル名を読み込む
			// 連結してファイルパスにする
			materialData.textrueFilePath = directoryPath + "/" + textureFilename;
		}

	}
	return materialData; // 読み込んだMaterialDataを返す

}



#pragma endregion



#pragma region OBJファイルを読み込む関数

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename){


	ModelData modelData; // 構造するModelData
	std::vector<Vector4> positions; // 位置
	std::vector<Vector3> normals; // 法線
	std::vector<Vector2> texcoords; // テクスチャ座標
	std::string line; // ファイルから読んだ1行を格納するもの

	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く

	assert(file.is_open()); // 開けなかったら止める

	while ( std::getline(file, line) ) {

		std::string identifier; // 行の先頭の識別子を格納する
		std::istringstream s(line); // 先頭の認別子を読む
		s >> identifier; // 先頭の識別子を読み込む
		if ( identifier == "v" ) {
			Vector4 position; // 位置を格納する変数
			s >> position.x >> position.y >> position.z; // 位置を読み込む
			// DirectX座標系に合わせて反転
			//position.x *= -1.0f;
			position.x *= 1.0f;
			position.w = 1.0f;
			positions.push_back(position); // 位置を格納する
		} else if ( identifier == "vt" ) {
			Vector2 texcoord; // テクスチャ座標を格納する変数
			s >> texcoord.x >> texcoord.y; // テクスチャ座標を読み込む
			texcoord.y = 1.0f - texcoord.y; // Y座標を反転する（OpenGLとDirectXの違い）
			texcoords.push_back(texcoord); // テクスチャ座標を格納する

		} else if ( identifier == "vn" ) {
			Vector3 normal; // 法線を格納する変数
			s >> normal.x >> normal.y >> normal.z; // 法線を読み込む
			normal.x *= 1.0f;
			//normal.x *= -1.0f;
			normals.push_back(normal); // 法線を格納する
		} else if ( identifier == "f" ) {
			VertexData triangle[3]; // 三角形の頂点データを格納する配列
			for ( int32_t faceVertex = 0; faceVertex < 3; ++faceVertex ) {
				std::string vertexDefinition;
				s >> vertexDefinition;

				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3] = {};
				for ( int32_t element = 0; element < 3; ++element ) {
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}
				triangle[faceVertex] = {
					positions[elementIndices[0] - 1], // 位置は1オリジンなので-1する
					texcoords[elementIndices[1] - 1], // テクスチャ座標も1オリジンなので-1する
					normals[elementIndices[2] - 1] // 法線も1オリジンなので-1する
				};




			}

			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
			modelData.indices.push_back(static_cast< uint32_t >( modelData.vertices.size() - 3 ));
			modelData.indices.push_back(static_cast< uint32_t >( modelData.vertices.size() - 2 ));
			modelData.indices.push_back(static_cast< uint32_t >( modelData.vertices.size() - 1 ));
		} else if ( identifier == "mtllib" ) {
			// マテリアルの指定
			std::string materialFilename; // マテリアル名を格納する変数
			s >> materialFilename; // マテリアル名を読み込む
			// マテリアルデータを読み込む
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}
	return modelData;
}



#pragma endregion

#pragma region 音声データ関数

SoundData SoundLoadWave(const char* filename){
	std::ifstream file(filename, std::ios::binary);
	assert(file.is_open());

	// RIFFヘッダー確認
	RiffHeader riff;
	file.read(( char* ) &riff, sizeof(riff));
	assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
	assert(strncmp(riff.type, "WAVE", 4) == 0);

	WAVEFORMATEX wfex = {};
	BYTE* pBuffer = nullptr;
	uint32_t bufferSize = 0;

	// チャンク走査
	ChunkHeader chunk;
	while ( file.read(( char* ) &chunk, sizeof(chunk)) ) {
		if ( strncmp(chunk.id, "fmt ", 4) == 0 ) {
			assert(chunk.size <= sizeof(WAVEFORMATEX));
			file.read(( char* ) &wfex, chunk.size);
		} else if ( strncmp(chunk.id, "data", 4) == 0 ) {
			pBuffer = new BYTE[chunk.size];
			file.read(( char* ) pBuffer, chunk.size);
			bufferSize = chunk.size;
		} else {
			// JUNK、LISTなどはスキップ
			file.seekg(chunk.size, std::ios_base::cur);
		}

		// 目的の両方を読み込んだら抜ける
		if ( wfex.cbSize != 0 && pBuffer != nullptr ) {
			break;
		}
	}

	file.close();

	SoundData soundData = {};
	soundData.wfex = wfex;
	soundData.pBuffer = pBuffer;
	soundData.bufferSize = bufferSize;
	return soundData;
}

void SoundUnload(SoundData* soundData){
	if ( soundData->pBuffer ) {
		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
	}
	soundData->bufferSize = 0;
	soundData->wfex = {};
}
// 音声再生関数
void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData){
	// XAudio2 が無効（初期化失敗など）なら再生せずリターン
	if ( !xAudio2 ) {
		OutputDebugStringA("[エラー] SoundPlayWave: xAudio2 が null です。\n");
		return;
	}

	HRESULT result;

	// SourceVoice（個別の音声再生チャンネル）を生成
	// soundData.wfex は再生する音声のフォーマット情報
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	if ( FAILED(result) ) {
		OutputDebugStringA("[エラー] CreateSourceVoice に失敗\n");
		return;
	}

	// 再生用のバッファ構造体を用意
	XAUDIO2_BUFFER buf = {};
	buf.pAudioData = soundData.pBuffer;     // 音声データの先頭ポインタ
	buf.AudioBytes = soundData.bufferSize;  // 音声データのサイズ（バイト単位）
	buf.Flags = XAUDIO2_END_OF_STREAM;      // 再生終了を通知するフラグ

	// SourceVoice にバッファを登録（実際の音声データを送る）
	result = pSourceVoice->SubmitSourceBuffer(&buf);

	// 音声再生開始
	result = pSourceVoice->Start();
}




#pragma endregion

#pragma region Particle関数
// パーティクル生成関数
Particle MakeNewParticle(std::mt19937& randomEngine){
	std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
	std::uniform_real_distribution<float> distTime(1.0f, 3.0f);
	Particle particle;
	particle.transform.scale = { 1.0f,1.0f,1.0f };
	particle.transform.rotate = { 0.0f,0.0f,0.0f };
	particle.transform.translate = { distribution(randomEngine), distribution(randomEngine), distribution(randomEngine) };
	particle.velocity = { distribution(randomEngine), distribution(randomEngine), distribution(randomEngine) };
	particle.color = { distribution(randomEngine) , distribution(randomEngine) , distribution(randomEngine),1.0f };
	particle.lifeTime = distTime(randomEngine);
	particle.currentTime = 0;

	return particle;

}

#pragma endregion



// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int){

	// COMの初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// 誰も補掟しなかった場合に(Unhandled),補掟する関数を登録
	// main関数は始まってすぐに登録するといい
	CrashDumper::Install();


	D3DResourceLeakChecker leakCheck;

	Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	// リソースチェック
	Microsoft::WRL::ComPtr<IDXGIDebug1> debug;

	logManager.Initialize(); // ログの初期化


	// ウィンドウのタイトル
	WindowProc windowProc;

	WNDCLASS wc = {}; // ウィンドウクラスの初期化
	wc.style = CS_HREDRAW | CS_VREDRAW; // ウィンドウのスタイル
	wc.hbrBackground = reinterpret_cast< HBRUSH >( COLOR_WINDOW + 1 ); // 背景色
	wc.lpfnWndProc = windowProc.WndProc; // ウィンドウプロシージャの関数ポインタ

	//const int kClientWidth = 1280; // ウィンドウの幅
	//const int kClientHeight = 720; // ウィンドウの高さ

	// ウィンドウプロシージャの初期化
	windowProc.Initialize(wc, windowProc.GetClientWidth(), windowProc.GetClientHeight());

	// 入力クラスの初期化
	input.Initialize();





	// ShaderCompilerのインスタンス
	ShaderCompiler shaderCompiler;
	shaderCompiler.Initialize(); // いまは何もしない（将来の拡張用）





#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) ) {
		// デバックレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif // _DEBUG


#pragma region DirectX12を初期化しよう


	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、
	// どうにもできない場合が多いのでassertにしとく
	assert(SUCCEEDED(hr));

	// 使用するアダプタ用の変数。最初にnullptrを入れておく
	Microsoft::WRL::ComPtr<IDXGIAdapter4> useAdapter = nullptr;
	// いい順にアダプタを頼む
	for ( UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; ++i ) {
		// アダプタ―の情報を習得する
		DXGI_ADAPTER_DESC3 adapterDesc {};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));// 取得できないのは一大事
		// ソフトウェアアダプタでなければ採用！
		if ( !( adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE ) ) {
			// 採用したアダプタの情報をログに出力。wstringの方なので注意
			logManager.Log(logManager.ConvertString(std::format(L"Use Adapater:{}\n", adapterDesc.Description)));
			break;
		}
		useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする

	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);


	// 昨日レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	// 高い順に生成できるか試していく
	for ( size_t i = 0; i < _countof(featureLevels); ++i ) {
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if ( SUCCEEDED(hr) ) {
			// 生成できたのでログ出力を行ってループを抜ける
			logManager.Log(std::format("FeatrueLevel : {}\n", featureLevelStrings[i]));
			break;
		}

	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);
	logManager.Log(logManager.ConvertString(L"Complete create D3D12Device!!!\n"));// 初期化完了のログを出す

#pragma endregion



#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if ( SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))) ) {
		// やばいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に泊まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		// 抑制するメッセージのＩＤ
		D3D12_MESSAGE_ID denyIds[] = {
			// windows11でのDXGIデバックレイヤーとDX12デバックレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE };
		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter {};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示wp抑制する
		infoQueue->PushStorageFilter(&filter);
	}

#endif // _DEBUG


#pragma region Particles

	// パーティクルの数を定義
	const uint32_t kNumMaxInstance = 10;
	// Instancing用のTransformationMatrixリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingRessource = CreateBufferResource(device, sizeof(ParticleForGPU) * kNumMaxInstance);
	// 書き込むためのアドレスを取得
	ParticleForGPU* instancingData = nullptr;
	instancingRessource->Map(0, nullptr, reinterpret_cast< void** >( &instancingData ));

	Particle particles[kNumMaxInstance];
	std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);

	// 単位行列を書き込んでおく
	for ( uint32_t index = 0; index < kNumMaxInstance; ++index ){


		particles[index] = MakeNewParticle(randomEngine);
		instancingData[index].WVP = MakeIdentity4x4();
		instancingData[index].World = MakeIdentity4x4();
		instancingData[index].color = particles[index].color;

	}

#pragma endregion

#pragma region CommandList

	// コマンドキューを生成する
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQuesDesc {};
	hr = device->CreateCommandQueue(&commandQuesDesc, IID_PPV_ARGS(&commandQueue));
	// コマンドキューの生成が上手くいかなかったので起動できない

	assert(SUCCEEDED(hr));
	// コマンドアフロケータを生成
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成が上手くいかなかったので起動出来ない
	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
	// コマンドリストの生成が上手くいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// スワップチェーンを生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc {};
	swapChainDesc.Width = kClientWidth;   //画面の幅。ウィンドウのクライアント領域を同じものにしていく
	swapChainDesc.Height = kClientHeight; //画面の高さ。ウィンドウのクライアント領域を同じようにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //色の形式
	swapChainDesc.SampleDesc.Count = 1; //マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; //描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破棄
	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), windowProc.GetHwnd(), &swapChainDesc, nullptr, nullptr, tempSwapChain.GetAddressOf());
	assert(SUCCEEDED(hr));

	// IDXGISwapChain4 にアップキャストする
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain;
	hr = tempSwapChain.As(&swapChain);
	assert(SUCCEEDED(hr));


	// RTV用のヒープでディスクリプタの数は2。RTVはshader内で触るものではないので,ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	// SPV用のヒープでディスクリプタの数は128。SRVはShader内で触るものなので、ShaderVisibleはtrue
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので,ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHaap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	// DescriptorSizeを取得しておく
	const uint32_t desriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t desriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t desriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);





	// SwapChainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = { nullptr };
	hr = tempSwapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = tempSwapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));
	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2dテクスチャとして読み込む
	// ディスクリプタの先頭を取得する。
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	// まず1つ目を作る。1つ目は最初の所に作る。つくる場所をこちらで指定して上げる必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);
	// ２つ目のディスクリプタハンドルを得る(自力)
	rtvHandles[1].ptr = rtvHandles[0].ptr + desriptorSizeRTV;
	// ２つ目を作る
	device->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);





	// Textrueを読んで転送する
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textrueResource = CreateTextureResource(device, metadata);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = UploadTextureData(textrueResource, mipImages, device, commandList);


	// 2枚目のTextureを読んで転送する
	DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textrueResource2 = CreateTextureResource(device, metadata2);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource2 = UploadTextureData(textrueResource2, mipImages2, device, commandList);
	// モンスターボールか否かをするために宣言
	bool useMonsterBall = false;

	// 3枚目のTexTureを読んで転送する
	DirectX::ScratchImage mipImages3 = LoadTexture("resources/fence.png");
	const DirectX::TexMetadata& metadata3 = mipImages3.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textrueResource3 = CreateTextureResource(device, metadata3);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource3 = UploadTextureData(textrueResource3, mipImages3, device, commandList);

	bool useFence = false;

	// 4枚目のTexTureを読んで転送する
	DirectX::ScratchImage mipImages5 = LoadTexture("resources/circle.png");
	const DirectX::TexMetadata& metadata5 = mipImages5.GetMetadata();
	Microsoft::WRL::ComPtr<ID3D12Resource> textrueResource5 = CreateTextureResource(device, metadata5);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource5 = UploadTextureData(textrueResource5, mipImages5, device, commandList);

	bool useCircle = false;



	// DepthStencilTextureをウィンドウのサイズで作成
	Microsoft::WRL::ComPtr<ID3D12Resource> deptStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);


	//---------------------
	// uvChecker用SRV
	//---------------------

	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc {};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ	
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);


	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 1);
	// SRVの生成
	device->CreateShaderResourceView(textrueResource.Get(), &srvDesc, textureSrvHandleCPU);

	//---------------------
	// monsterBall用SRV2
	//---------------------

	// metaDataを基にSRV2の設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2 {};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ	
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// SRV2を作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 2);

	// SRV2の生成
	device->CreateShaderResourceView(textrueResource2.Get(), &srvDesc2, textureSrvHandleCPU2);

	//---------------------
	// fence用SRV3
	//---------------------

	// metaDataを基にSRV3の設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc3 {};
	srvDesc3.Format = metadata3.format;
	srvDesc3.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc3.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ	
	srvDesc3.Texture2D.MipLevels = UINT(metadata3.mipLevels);

	// SRV3を作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU3 = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 3);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU3 = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 3);

	// SRV3の生成
	device->CreateShaderResourceView(textrueResource3.Get(), &srvDesc3, textureSrvHandleCPU3);


	//---------------------
	// Particle用SRV5
	//---------------------


	// metaDataを基にSRV3の設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc5 {};
	srvDesc5.Format = metadata5.format;
	srvDesc5.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc5.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; // 2Dテクスチャ	
	srvDesc5.Texture2D.MipLevels = UINT(metadata5.mipLevels);

	// SRV3を作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU5 = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 5);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU5 = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 5);

	// SRV3の生成
	device->CreateShaderResourceView(textrueResource5.Get(), &srvDesc5, textureSrvHandleCPU5);

	//---------------------
	// instancingSrvDesc
	//---------------------


	// metaDataを基にSRV4の設定
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc {};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumMaxInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(ParticleForGPU);
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 4);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, desriptorSizeSRV, 4);
	device->CreateShaderResourceView(instancingRessource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);



	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC devDesc {};
	devDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Format。基本的にはResourceに合わせる
	devDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; //2dTexture
	// DSVHeapの設定にDSVをつくる
	device->CreateDepthStencilView(deptStencilResource.Get(), &devDesc, dsvDescriptorHaap->GetCPUDescriptorHandleForHeapStart());

	// DepthStenciLstateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc {};
	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;
	// Depthの書き込みを行わない
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	depthStencilDesc.StencilEnable = false;



#pragma endregion

#pragma region Fence

	// 初期化0でFenceを作る
	Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);
#pragma endregion

#pragma region DXCの初期化

	// dxcCompilerを初期化
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));

	// 現時点でincludeはしないが、includeに対応するための設定を行っておく
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

#pragma endregion



#pragma region PSO

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature {};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// DescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0; //0から始める
	descriptorRange[0].NumDescriptors = 1; // 数は1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // offsetを自動計算

	// Particle用
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};

	descriptorRangeForInstancing[0].BaseShaderRegister = 0; //0から始める
	descriptorRangeForInstancing[0].NumDescriptors = 1; // 数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // offsetを自動計算



	// PootParameter作成。複数設定できるので配列。
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; //PixelShaderで使う 
	rootParameters[0].Descriptor.ShaderRegister = 0; //レジスタ番号0とバインド 
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; //VertexShaderで使う 
	//rootParameters[1].Descriptor.ShaderRegister = 0; //レジスタ番号0とバインド 
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing; // Tableの中身の配列を指定
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する敵

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; //DescriptorTableを使う
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShadaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1; // レジスタ番号1を使う
	descriptionRootSignature.pParameters = rootParameters; //ルートバラメータ配列へのポインタ
	descriptionRootSignature.NumParameters = _countof(rootParameters); //配列の長さ

	// Sampler
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; //バイナリアフィルタ
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; //0∼1の範囲側をリピート 
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; //比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX; // ありったけのMipmap
	staticSamplers[0].ShaderRegister = 0; //レジスタ番号0を使う
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);




	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if ( FAILED(hr) ) {
		logManager.Log(reinterpret_cast< char* >( errorBlob->GetBufferPointer() ));
		assert(false);

	}

	// バイナリを元に生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	// InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc {};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc {};
	// 全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	// NormalBlend 通常ブレンド
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

	// AddBlend 加算合成
	//blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	//blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	//blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	// SubtractBlend 逆減算合成
	//blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	//blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
	//blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

	// MultiplyBlend 乗算合成
	//blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	//blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	//blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;

	// ScreenBlend スクリーン合成
	//blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
	//blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	//blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;


	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;




	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc {};
	// 裏面(時計周り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob =
		shaderCompiler.CompileShader(L"resources/shaders/Particle.VS.hlsl", L"vs_6_0",
			dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob =
		shaderCompiler.CompileShader(L"resources/shaders/Particle.PS.hlsl", L"ps_6_0",
			dxcUtils, dxcCompiler, includeHandler);
	assert(pixelShaderBlob != nullptr);

	// PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc {};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get(); // RootSignatrue
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;  // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
	vertexShaderBlob->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
	pixelShaderBlob->GetBufferSize() }; // PixelShader 
	graphicsPipelineStateDesc.BlendState = blendDesc;// BlensState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;// RasterizerState
	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	// どのように画面に色を打ち込むかの設定(気にしなくて良い)
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;


	// 実際に生成
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPinelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPinelineState));
	assert(SUCCEEDED(hr));


#pragma endregion


#pragma region 頂点データの作成とビュー

	//// 実際に頂点リソースを作る
	//Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * 6);

	//// 頂点バッファビューを作成する
	//D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	//// リソースの先頭のアドレスから使う
	//vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	//// 使用するリソースのサイズは頂点3つ分
	//vertexBufferView.SizeInBytes = sizeof(VertexData) * 6;
	//// 1頂点あたりのサイズ
	//vertexBufferView.StrideInBytes = sizeof(VertexData);

	//// 頂点リソースにデータを書き込む
	//VertexData* vertexData = nullptr;
	//// 書き込むためのアドレス取得
	//vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	//// 左下
	//vertexData[0] = { -0.5f,-0.5f,0.0f,1.0f };
	//vertexData[0].texcoord = { 0.0f,1.0f };
	//// 上
	//vertexData[1] = { 0.0f,0.5f,0.0f,1.0f };
	//vertexData[1].texcoord = { 0.5f,0.0f };
	//// 右下
	//vertexData[2] = { 0.5f,-0.5f,0.0f,1.0f };
	//vertexData[2].texcoord = { 1.0f,1.0f };


	//// 左下2
	//vertexData[3].position = { -0.5f,-0.5f,0.5f,1.0f };
	//vertexData[3].texcoord = { 0.0f,1.0f };
	//// 上2
	//vertexData[4].position = { 0.0f,0.0f,0.0f,1.0f };
	//vertexData[4].texcoord = { 0.5f,0.0f };
	//// 右下2
	//vertexData[5].position = { 0.5f,-0.5f,-0.5f,1.0f };
	//vertexData[5].texcoord = { 1.0f,1.0f };


#pragma endregion

#pragma region Spriteの実装

	// Sprite用の頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite {};
	// リソースの先頭のアドレスから作成する
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点4つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	VertexData* vertexDataSprite = nullptr;
	// 書き込むためのアドレス取得
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast< void** >( &vertexDataSprite ));

	// 1枚目の三角形
	// 左上
	vertexDataSprite[0].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f, 0.0f };
	vertexDataSprite[0].normal = { 0.0f, 0.0f, -1.0f };

	// 左下
	vertexDataSprite[1].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f, 1.0f };
	vertexDataSprite[1].normal = { 0.0f, 0.0f, -1.0f };

	// 右上
	vertexDataSprite[2].position = { 640.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 1.0f, 0.0f };
	vertexDataSprite[2].normal = { 0.0f, 0.0f, -1.0f };

	// 右下
	vertexDataSprite[3].position = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 1.0f, 1.0f };
	vertexDataSprite[3].normal = { 0.0f, 0.0f, -1.0f };
	//// 左上
	//vertexDataSprite[4].position = { 640.0f,0.0f,0.0f,1.0f };
	//vertexDataSprite[4].texcoord = { 1.0f,0.0f };
	//vertexDataSprite[4].normal = { 0.0f,0.0f,0.0f };
	//// 右下
	//vertexDataSprite[5].position = { 640.0f,360.0f,0.0f,1.0f };
	//vertexDataSprite[5].texcoord = { 1.0f,1.0f };
	//vertexDataSprite[5].normal = { 0.0f,0.0f,0.0f };




#pragma endregion

#pragma region Sphereの実装

	const int kSubdivision = 16;
	const int vertexCountX = kSubdivision + 1;
	const int vertexCountY = kSubdivision + 1;

	const int vertexNum = vertexCountX * vertexCountY;
	const int indexNum = kSubdivision * kSubdivision * 6;

	uint32_t latIndex;
	uint32_t lonIndex;


	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSphere = CreateBufferResource(device, sizeof(VertexData) * vertexNum);

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSphere {};
	// リソースの先頭のアドレスから作成する
	vertexBufferViewSphere.BufferLocation = vertexResourceSphere->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点6つ分のサイズ
	vertexBufferViewSphere.SizeInBytes = sizeof(VertexData) * vertexNum;
	// 1頂点あたりのサイズ
	vertexBufferViewSphere.StrideInBytes = sizeof(VertexData);

	VertexData* vertexDataSphere = nullptr;
	// 書き込むためのアドレス取得
	vertexResourceSphere->Map(0, nullptr, reinterpret_cast< void** >( &vertexDataSphere ));

	// 経度分割1つ分の角度 
	const float kLonEvery = std::numbers::pi_v<float>*2.0f / float(kSubdivision);
	// 緯度分割1つ分の角度
	const float kLatEvery = std::numbers::pi_v<float> / float(kSubdivision);
	// 緯度の方向に分割
	for ( latIndex = 0; latIndex < ( kSubdivision + 1 ); ++latIndex ) {

		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * latIndex;
		// 経度の方向に分割しながら線を描く
		for ( lonIndex = 0; lonIndex < ( kSubdivision + 1 ); ++lonIndex ) {

			float lon = lonIndex * kLonEvery;
			// 頂点データを描く
			//頂点A
			VertexData vertA = {
				{
					std::cosf(lat) * std::cosf(lon),
					std::sinf(lat),
					std::cosf(lat) * std::sinf(lon),
					1.0f
				},
				{
					float(lonIndex) / float(kSubdivision),
					1.0f - float(latIndex) / float(kSubdivision)
				},
				{
					std::cosf(lat) * std::cosf(lon),
					std::sinf(lat),
					std::cosf(lat) * std::sinf(lon),
				}
			};
			uint32_t start = ( latIndex * ( kSubdivision + 1 ) + lonIndex );
			vertexDataSphere[start] = vertA;

		}

	}
#pragma endregion

#pragma region indexを使った実装

	// indexSprite用の頂点indexを作る1つ辺りのindexのサイズは32bit
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite {}; // IBV

	// リソースの先頭のアドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();

	//使用するリソースのサイズはindex6つ分のサイズ 
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;

	// indexはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	// indexリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;

	indexResourceSprite->Map(0, nullptr, reinterpret_cast< void** >( &indexDataSprite ));

	indexDataSprite[0] = 0;
	indexDataSprite[1] = 2;
	indexDataSprite[2] = 1;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 2;
	indexDataSprite[5] = 3;

	indexResourceSprite->Unmap(0, nullptr);

#pragma endregion

#pragma region indexを使った実装sphere

	//indexSphere用の頂点indexを作る1つ辺りのindexのサイズは32bit
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSphere = CreateBufferResource(device, sizeof(uint32_t) * indexNum);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere {}; // IBV
	// リソースの先頭のアドレスから使う
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	//使用するリソースのサイズ
	indexBufferViewSphere.SizeInBytes = sizeof(uint32_t) * indexNum;

	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT; // indexはuint32_tとする

	// indexリソースにデータを書き込む
	uint32_t* indexDataSphere = nullptr;

	indexResourceSphere->Map(0, nullptr, reinterpret_cast< void** >( &indexDataSphere ));

	uint32_t idx = 0;
	for ( uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex ) {
		for ( uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex ) {
			// 緯度と経度のインデックスから頂点のインデックスを計算する
			uint32_t topLeft = latIndex * vertexCountX + lonIndex;
			uint32_t bottomLeft = ( latIndex + 1 ) * vertexCountX + lonIndex;
			uint32_t topRight = latIndex * vertexCountX + ( lonIndex + 1 );
			uint32_t bottomRight = ( latIndex + 1 ) * vertexCountX + ( lonIndex + 1 );

			// 1つめの三角形
			indexDataSphere[idx++] = topLeft;
			indexDataSphere[idx++] = bottomLeft;
			indexDataSphere[idx++] = topRight;

			// 2つめの三角形
			indexDataSphere[idx++] = topRight;
			indexDataSphere[idx++] = bottomLeft;
			indexDataSphere[idx++] = bottomRight;
		}
	}

	indexResourceSphere->Unmap(0, nullptr);



#pragma endregion

#pragma region ModelDataを使った実装

	// モデルを読み込む
	ModelData modelData = LoadObjFile("resources", "plane.obj");
	// 1. すべての頂点の合計を求める
	Vector3 center = { 0.0f, 0.0f, 0.0f };
	for ( const auto& v : modelData.vertices ) {
		center.x += v.position.x;
		center.y += v.position.y;
		center.z += v.position.z;
	}

	// 2. 頂点数で割って中心座標を求める
	size_t vertexCount = modelData.vertices.size();
	if ( vertexCount > 0 ) {
		center.x /= vertexCount;
		center.y /= vertexCount;
		center.z /= vertexCount;
	}

	// 3. すべての頂点座標から中心座標を引く
	for ( auto& v : modelData.vertices ) {
		v.position.x -= center.x;
		v.position.y -= center.y;
		v.position.z -= center.z;
	}

	// 頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());

	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView {};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast< void** >( &vertexData ));
	std::copy(modelData.vertices.begin(), modelData.vertices.end(), vertexData);

	// インデックスリソースも同様に
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource = CreateBufferResource(device, sizeof(uint32_t) * modelData.indices.size());
	D3D12_INDEX_BUFFER_VIEW indexBufferView {};
	indexBufferView.BufferLocation = indexResource->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = UINT(sizeof(uint32_t) * modelData.indices.size());
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indexData = nullptr;
	indexResource->Map(0, nullptr, reinterpret_cast< void** >( &indexData ));
	std::copy(modelData.indices.begin(), modelData.indices.end(), indexData);
	indexResource->Unmap(0, nullptr);
#pragma endregion

#pragma region ViewportとScissor

	// ビューボート
	D3D12_VIEWPORT viewport {};
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect {};
	// 基本的にビューボートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;



#pragma endregion

#pragma region Material用

	// マテリアル用のリソースを作る。今回はcolor1つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = CreateBufferResource(device, sizeof(Material));
	// マテリアルにデータを書き込む
	Material* materialData = nullptr;
	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast< void** >( &materialData ));
	// 今回は赤を書き込んでいる
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData->enableLighting = true;
	materialData->uvTransfrom = MakeIdentity4x4();

	// Sprite用のマテリアルリソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = CreateBufferResource(device, sizeof(Material));

	Material* materialDataSprite = nullptr;
	// Mapしてデータを書き込む。色は白設定しておく
	materialResourceSprite->Map(0, nullptr, reinterpret_cast< void** >( &materialDataSprite ));

	materialDataSprite->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	// SpriteはLightingしないのでfalseを設定する
	materialDataSprite->enableLighting = false;
	// UVTransformはSpriteでは使うので設定しておく。今回は単位行列を設定しておく
	materialDataSprite->uvTransfrom = MakeIdentity4x4();



#pragma endregion

#pragma region 平行光源

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalResourceLight = CreateBufferResource(device, sizeof(DirectionalLight));

	DirectionalLight* directionalLightData = nullptr;

	directionalResourceLight->Map(0, nullptr, reinterpret_cast< void** >( &directionalLightData ));

	// デフォルト値はとりあえず以下のようにしておく
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f, -1.0f,0.0f };
	directionalLightData->intensity = 1.0f;



#pragma endregion

#pragma region WVP


	// WVB用のリソースを作る。Matrix4x4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = CreateBufferResource(device, sizeof(ParticleForGPU));

	// データを書き込む
	ParticleForGPU* transformationMatrixData = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatrixData ));

	// 単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();

	// Sprite用のTransformationMatirx用のリソースを作る。Matrix4x4 一つ分のサイズを用意する
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(ParticleForGPU));

	// データを書き込む
	ParticleForGPU* transformationMatirxDataSprite = nullptr;
	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast< void** >( &transformationMatirxDataSprite ));
	// 単位行列を書き込んでおく
	transformationMatirxDataSprite->WVP = MakeIdentity4x4();

#pragma endregion

#pragma region ImGuiの初期化

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowProc.GetHwnd());
	ImGui_ImplDX12_Init(device.Get(),
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());


#pragma endregion



#pragma region サウンド

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
	IXAudio2MasteringVoice* masterVoice = nullptr;
	// XAudioエンジンのinstanceを生成
	HRESULT result = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	// マスターボイスをを生成
	result = xAudio2->CreateMasteringVoice(&masterVoice);

	// 音声読み込み
	SoundData soundData1 = SoundLoadWave("resources/BGM.wav");
	if ( FAILED(result) ) {
		OutputDebugStringA("[エラー] XAudio2 の初期化に失敗\n");
		xAudio2 = nullptr;
		// ここで return またはフラグ立てしてもOK
	}

#pragma endregion



	// 60fps固定用の時間計測
	const float kDeltaTime = 1.0f / 60.0f;


	while ( !windowProc.GetIsClosed() ){

		// ウィンドウ
		windowProc.Update();



		// ImGuiの開始処理
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// 開発用UIの処理。実際に開発用のUIを出す場合はここをゲーム固有の処理に置き換える
		ImGui::ShowDemoWindow();


		// メインループ内
		input.Update(); // ← 追加：Inputの更新処理




		// キーボード情報の取得開始

		// スペースキーが押されたら音を鳴らす
		if ( input.Triggerkey(DIK_SPACE) ){
			SoundPlayWave(xAudio2.Get(), soundData1);

		}



		



		//-------------------------------
		// ゲームの処理
		//-------------------------------



		// これから書き込むバックバッファのインデックスを取得
		UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();



		// TransitionBarrierの設定
		D3D12_RESOURCE_BARRIER barrier {};
		// 今回のバリアはTransition
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		// Noneにしておく
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		// バリアを張る対象のリソース。現在のバックバッファに対して行う
		barrier.Transition.pResource = swapChainResources[backBufferIndex].Get();
		// 遷移前(現在)のResourceState
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		// 遷移後のResoureceState
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		// TransitionBarrierを張る
		commandList->ResourceBarrier(1, &barrier);



		//-------------------------------
		// パーティクル用
		//------------------------------- 



		Matrix4x4 worldMatrix = MakeAffine(transform.scale, transform.rotate, transform.translate);
		Matrix4x4 cameraMatrix = MakeAffine(cameraTransfrom.scale, cameraTransfrom.rotate, cameraTransfrom.translate);
		Matrix4x4 viewMatrix = Inverse(cameraMatrix); // ← 通常カメラの行列

		Matrix4x4 projectionMatrix = PerspectiveFov(1.0f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
		uint32_t numInstance = 0;// 有効なインスタンス数をカウントする変数

		for ( uint32_t index = 0; index < kNumMaxInstance; ++index ){
			// 生存時間を過ぎていたら描画しない
			if ( particles[index].lifeTime <= particles[index].currentTime ){
				continue;
			}
			float alpha = 1.0f - ( particles[index].currentTime / particles[index].lifeTime );
			Matrix4x4 worldMatrix = MakeAffine(particles[index].transform.scale, particles[index].transform.rotate, particles[index].transform.translate);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			instancingData[index].WVP = worldViewProjectionMatrix;
			instancingData[index].World = worldMatrix;
			particles[index].transform.translate += particles[index].velocity * kDeltaTime; // 速度に応じて移動
			particles[index].currentTime += kDeltaTime;// 経過時間を加算
			instancingData[numInstance].WVP = worldViewProjectionMatrix;
			instancingData[numInstance].World = worldMatrix;
			instancingData[numInstance].color = particles[index].color;
			instancingData[numInstance].color.w = alpha; // α値を設定

			numInstance++; // 有効なインスタンス数をカウント
		}


		//Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
		//transformationMatrixData->World = worldMatrix;
		//transformationMatrixData->WVP = worldViewProjectionMatrix;

		//-------------------------------
		// Sprite
		//-------------------------------
		Matrix4x4 worldMatrixSprite = MakeAffine(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
		Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
		Matrix4x4 projectionMatrixSprite = Orthographic(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
		Matrix4x4 viewProjection = Multiply(viewMatrixSprite, projectionMatrixSprite);
		Matrix4x4 worldViewProjectionMatrixSprite = Multiply(viewProjection, worldMatrixSprite);
		transformationMatirxDataSprite->World = worldMatrixSprite;
		transformationMatirxDataSprite->WVP = worldViewProjectionMatrixSprite;



		//-------------------------------
		//ImGui
		//-------------------------------
		ImGui::Begin("Settings");

		ImGui::ColorEdit4("Color", &materialData->color.x);
		ImGui::SliderAngle("RotateX", &transformSprite.rotate.x, -500, 500);
		ImGui::SliderAngle("RotateY", &transformSprite.rotate.y, -500, 500);
		ImGui::SliderAngle("RotateZ", &transformSprite.rotate.z, -500, 500);
		ImGui::DragFloat3("transform", &transformSprite.translate.x, -180, 180);
		ImGui::DragFloat3("transformSprite", &transform.translate.x);
		ImGui::SliderAngle("RotateXSprite", &transform.rotate.x, -500, 500);
		ImGui::SliderAngle("RotateYSprite", &transform.rotate.y, -500, 500);
		ImGui::SliderAngle("RotateZSprite", &transform.rotate.z, -500, 500);

		if ( ImGui::Checkbox("useMonsterBall", &useMonsterBall) ) {
			if ( useMonsterBall ) {
				useFence = false;
				useCircle = false;
			}
		}

		if ( ImGui::Checkbox("fence", &useFence) ) {
			if ( useFence ) {
				useMonsterBall = false;
				useCircle = false;
			}
		}

		if ( ImGui::Checkbox("circle", &useCircle) ) {
			if ( useCircle ) {
				useMonsterBall = false;
				useFence = false;
			}
		}

		ImGui::ColorEdit4("LightColor", &directionalLightData->color.x);
		ImGui::SliderFloat("LightX", &directionalLightData->direction.x, -10.0f, 10.0f);
		ImGui::SliderFloat("LightY", &directionalLightData->direction.y, -10.0f, 10.0f);
		ImGui::SliderFloat("LightZ", &directionalLightData->direction.z, -10.0f, 10.0f);
		ImGui::DragFloat2("UVTransform", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
		ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
		ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);



		directionalLightData->direction = Normalize(directionalLightData->direction);

		Matrix4x4 uvTransformMatrix = MakeScale(uvTransformSprite.scale);
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZ(uvTransformSprite.rotate.z));
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslate(uvTransformSprite.translate));
		materialDataSprite->uvTransfrom = uvTransformMatrix;


		ImGui::End();

		// ImGuiの内部コマンドを生成する
		ImGui::Render();










		// 描画先のRTVとDSVを設定する
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHaap->GetCPUDescriptorHandleForHeapStart();
		commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

		// 指定した色で画面全体をクリアする
		float clearColor[] = { 0.1f,0.25f,0.5f,1.0f }; // 青っぽい色。RGBAの順
		commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
		// 指定した深度で画面全体をクリアする
		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// 描画用のDescriptorHeapの設定
		ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, descriptorHeaps);

		commandList->RSSetViewports(1, &viewport);// Viewportを設定
		commandList->RSSetScissorRects(1, &scissorRect);// Scirssorを設定
		// rootSignatrueを設定。PSOに設定してるけど別途設定が必要
		commandList->SetGraphicsRootSignature(rootSignature.Get());
		commandList->SetPipelineState(graphicsPinelineState.Get()); //PSOを設定
		commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSphere); // VBVを設定


		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
		commandList->IASetIndexBuffer(&indexBufferView);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// ルート定数バッファなどはそのまま
		commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
		//commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());




		commandList->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);

		// 表示するテクスチャのハンドルを一時的に保持する変数
		D3D12_GPU_DESCRIPTOR_HANDLE selectedTextureHandle = textureSrvHandleGPU;

		// 条件に応じて切り替え
		if ( useFence ) {
			selectedTextureHandle = textureSrvHandleGPU3; // 3番目（Fence）
		} else if ( useMonsterBall ) {
			selectedTextureHandle = textureSrvHandleGPU2; // 2番目（MonsterBall）
		} else if ( useCircle ) { // 新しいチェックボックスなどで制御
			selectedTextureHandle = textureSrvHandleGPU5; // 5番目（追加画像）
		}

		// 選択されたテクスチャをセット
		commandList->SetGraphicsRootDescriptorTable(2, selectedTextureHandle);



		commandList->SetGraphicsRootConstantBufferView(3, directionalResourceLight->GetGPUVirtualAddress());


		// インデックス数分描画
		commandList->DrawIndexedInstanced(UINT(modelData.indices.size()), numInstance, 0, 0, 0);



		//-------------------------------
		//// Sphereの描画
		//-------------------------------

		//// 形状の種類を設定（ここでは三角形リスト）
		//commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		//// インデックスバッファのビューを設定
		//commandList->IASetIndexBuffer(&indexBufferViewSphere);

		//// ルートパラメータ 0：マテリアル（定数バッファ）
		//commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

		//// ルートパラメータ 1：WVP行列（定数バッファ）
		//commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

		//// ルートパラメータ 2：テクスチャ（SRV）
		//// チェックボックスの状態によって使うテクスチャを切り替える
		//commandList->SetGraphicsRootDescriptorTable(
		//	2,
		//	useMonsterBall ? textureSrvHandleGPU : textureSrvHandleGPU2
		//);

		//// ルートパラメータ 3：ディレクショナルライト（定数バッファ）
		//commandList->SetGraphicsRootConstantBufferView(3, directionalResourceLight->GetGPUVirtualAddress());

		//// 描画コマンドの発行（Draw Call）
		//// 1インスタンスあたりのインデックス数：sphereVertexNum
		//commandList->DrawIndexedInstanced(indexNum, 1, 0, 0, 0);




		// トポロジの設定
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		// VBVを設定 : IBVを設定
		commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
		commandList->IASetIndexBuffer(&indexBufferViewSprite);

		// マテリアル定数バッファ
		commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

		// 行列定数バッファ
		//commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

		// ここで更新してSpriteの画像を変えないようにする
		commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);


		//描画！(DraoCall/ドローコール)
	   //commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);






	   // 実際のcommandListのImGuiの描画コマンドを積む
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

		// 画面に描く処理はすべて終わり、画面に移すので、状態を遷移
		// 今回はRenderTargetからPresentにする
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;


		// TransitionBarrierを張る
		commandList->ResourceBarrier(1, &barrier);


		// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseする事
		hr = commandList->Close();
		assert(SUCCEEDED(hr));

		// GPUにコマンドリストの実行を行われる
		Microsoft::WRL::ComPtr<ID3D12CommandList> commandLists[] = { commandList };
		commandQueue->ExecuteCommandLists(1, commandLists->GetAddressOf());

		// GPUとOSに画面の交換を行うよう通知する
		swapChain->Present(1, 0);

		// Fenseの値を更新
		fenceValue++;
		// GPUがここまでたどり着いたときに、Fenceの値を指定した値の代入するようにSignalを送る
		commandQueue->Signal(fence.Get(), fenceValue);

		// Fenceの値が指定したSignal値にたどり着いているか確認する
		// GetCompletedValueの初期値はFence作成時に渡した初期値
		if ( fence->GetCompletedValue() < fenceValue ) {
			// 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
			fence->SetEventOnCompletion(fenceValue, fenceEvent);
			// イベント待つ
			WaitForSingleObject(fenceEvent, INFINITE);
		}


		// 次のフレーム用のコマンドリストを準備
		hr = commandAllocator->Reset();
		assert(SUCCEEDED(hr));
		hr = commandList->Reset(commandAllocator.Get(), nullptr);
		assert(SUCCEEDED(hr));

		if ( input.Triggerkey(DIK_ESCAPE) ) {
			break;
		}



	}

	// ImGuiの終了処理
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

#pragma region オブジェクトを解放

	CloseHandle(fenceEvent);
	//XAudio2解放
	xAudio2.Reset();
	// 音声データ解放
	SoundUnload(&soundData1);


	CloseWindow(windowProc.GetHwnd());

	//出力ウィンドウへの文字出力
	logManager.Log("HelloWored\n");
	logManager.Log(logManager.ConvertString(std::format(L"WSTRING{}\n", kClientWidth)));

	logManager.Finalize();

#pragma endregion




	// COMの終了処理
	CoUninitialize();

	return 0;

}