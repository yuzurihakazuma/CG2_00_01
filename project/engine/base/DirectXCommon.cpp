#include "DirectXCommon.h"

// --- 標準ライブラリ ---
#include <cassert>
#include <thread>

// --- エンジン側のファイル ---
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/base/WindowProc.h"

using namespace logs;

void DirectXCommon::Finalize(){
#ifdef _DEBUG
	logManager_.Log("DirectXCommon::Finalize() start");
#endif

	//  GPU完了待ち
	WaitForGPU();

	//  参照順に解放（超重要）
	for ( UINT i = 0; i < kBackBufferCount; ++i ) {
		backBuffers_[i].Reset();
	}

	depthStencilResource_.Reset();
	depthBuffer_.Reset();


	includeHandler_.Reset();
	dxcCompiler_.Reset();
	dxcUtils_.Reset();
	resource.Reset();

	rtvDescriptorHeap_.Reset();
	dsvDescriptorHeap_.Reset();
	srvDescriptorHeap_.Reset();

	commandList_.Reset();
	commandAllocator_.Reset();
	commandQueue_.Reset();

	fence_.Reset();

	swapChain_.Reset();
	device_.Reset();
	useAdapter_.Reset();
	dxgiFactory_.Reset();

	if ( fenceEvent_ ) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}

#ifdef _DEBUG
	logManager_.Log("DirectXCommon::Finalize() end");
#endif
}

// ウィンドウサイズ変更時の処理
void DirectXCommon::OnResize(){

	// GPU完了待ち
	WaitForGPU();


	// バックバッファの解放
	for ( UINT i = 0; i < kBackBufferCount; ++i ){
		// バックバッファの解放
		backBuffers_[i].Reset();

	}

	
	// ステンシルリソースの解放
	depthStencilResource_.Reset(); 

	// 深度バッファもリサイズするためにいったん解放
	depthBuffer_.Reset(); 

	// スワップチェーンのリサイズ
	hr_=swapChain_->ResizeBuffers(
		kBackBufferCount, // バッファ数
		windowProc_->GetClientWidth(), // 横幅
		windowProc_->GetClientHeight(), // 縦幅
		DXGI_FORMAT_R8G8B8A8_UNORM, // フォーマット
		0); // オプション

	assert(SUCCEEDED(hr_));

	CreateRenderTargetViews(); // バックバッファのRTV再作成
	CreateDepthBuffer(); // 深度バッファの再作成
	CreateDepthStencilView(); // 深度ステンシルビューの再作成
	InitializeScissorRect(); // シザー矩形の再設定


}

void DirectXCommon::Initialize(WindowProc* windowProc){
	// NULLチェック
	assert(windowProc);
	
	
	this->windowProc_ = windowProc;

	// FPSの初期化
	InitializeFixFPS();
	// デバッグレイヤーの初期化
	InitializeDebugLayer();

	// デバイスの生成
	CreateFactory();
	// GPUアダプタの選択
	SelectAdapter();
	// D3D12デバイスの生成
	CreateDevice();
	assert(device_ != nullptr);
	// 情報キューの初期化
	InitializeInfoQueue();

	// コマンド関連の初期化
	CreateCommand();
	assert(commandList_ != nullptr);

	// スワップチェーンの生成
	CreateSwapChain();
	// 深度バァファの生成
	CreateDepthBuffer();
	// 各種でスクリプタヒープの生成
	CreateDescriptorHeaps();
	// レンダーターゲットビューの初期化
	CreateRenderTargetViews();
	// 深度ステンシルビューの初期化
	CreateDepthStencilView();
	// フェンスの初期化
	CreateFence();
	// ビューポートの初期化
	InitializeViewport();
	// シザー矩形の初期化
	InitializeScissorRect();
	// DXCコンパイラの生成
	CreateDXCCompiler();

	// LogManager の初期化ログ出力（←ログ自体の初期化は不要）
	logManager_.Log("DirectXCommon::Initialize() 開始");

	// DXCコンパイラの生成（ShaderCompilerの初期化）
	bool result = shaderCompiler_.Initialize();
	assert(result);
	logManager_.Log("ShaderCompiler 初期化成功");

	logManager_.Log("DirectXCommon::Initialize() 完了");

}
// 描画前処理
void DirectXCommon::PreDraw(){

	// コマンドリストの内容をクリア
	ResetCommand();

	// SrvManagerの描画前処理を呼ぶ
	SrvManager::GetInstance()->PreDraw();

	// これから書き込むバックバッファのインデックスを取得
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	// TransitionBarrierの設定
	D3D12_RESOURCE_BARRIER barrier {};
	// 今回のバリアはTransition
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	// Noneにしておく
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	// バリアを張る対象のリソース。現在のバックバッファに対して行う
	barrier.Transition.pResource = backBuffers_[backBufferIndex].Get();
	// 遷移前(現在)のResourceState
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	// 遷移後のResoureceState
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	// TransitionBarrierを張る
	commandList_->ResourceBarrier(1, &barrier);

	// 今のバックバッファ用 RTV ハンドル計算
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
		rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += static_cast< SIZE_T >( backBufferIndex ) * rtvDescSize_;

	// DSV ハンドル取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// レンダーターゲット / 深度ステンシルをセット
	commandList_->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// 画面クリア & 深度クリア
	const float clearColor[4] = { 0.04f, 0.03f, 0.05f, 1.0f }; // 洞窟の闇（暗紫黒）
	commandList_->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	commandList_->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f, 0, 0, nullptr);

	// ビューポート / シザー設定
	commandList_->RSSetViewports(1, &viewport_);
	commandList_->RSSetScissorRects(1, &scissorRect_);


	

}
// 描画後処理
void DirectXCommon::PostDraw(){

	// これから表示するバックバッファのインデックスを取得
	UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

	// バックバッファを「描画用 → 表示用(PRESENT)」に戻すバリア
	D3D12_RESOURCE_BARRIER barrier {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = backBuffers_[backBufferIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	commandList_->ResourceBarrier(1, &barrier);

	
	// コマンドリストのクローズ
	ExecuteCommand();

	// 画面の表示
	hr_ = swapChain_->Present(0, 0);
	assert(SUCCEEDED(hr_));

	WaitForGPU();
	
	// FPS固定の更新
	UpdateFixFPS();

}

// 初期化時のコマンド記録開始
void DirectXCommon::BeginCommandRecording(){
	// 共通パーツを呼ぶだけ！
	ResetCommand();
}

// 初期化時のコマンド記録終了
void DirectXCommon::EndCommandRecording(){
	// 共通パーツを呼ぶだけ！
	ExecuteCommand();
	WaitForGPU();
}


/// DXGIファクトリーの生成
void DirectXCommon::CreateFactory(){
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEDEDマクロで判定できる
	UINT flags = 0; // フラグ

#if _DEBUG
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	hr_ = CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgiFactory_));
	assert(SUCCEEDED(hr_));
}
///  //GPUアダプタの選択
void DirectXCommon::SelectAdapter(){

	ComPtr<IDXGIAdapter4> adapter;

	for ( UINT i = 0;
		dxgiFactory_->EnumAdapterByGpuPreference(
			i,
			DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(&adapter)
		) != DXGI_ERROR_NOT_FOUND;
		++i )
	{
		DXGI_ADAPTER_DESC3 desc {};
		adapter->GetDesc3(&desc);

		if ( !( desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE ) ) {
			// ★ここで代入する！
			useAdapter_ = adapter;

			logManager_.Log(
				logManager_.ConvertString(
					std::format(L"Use Adapter: {}\n", desc.Description)
				)
			);
			return;
		}

	}
	adapter.Reset();
	assert(false && "No hardware adapter found!");
}
/// D3D12デバイスの生成
void DirectXCommon::CreateDevice(){



	assert(useAdapter_ != nullptr && "Adapter is null! SelectAdapter() failed.");


	// 昨日レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2","12.1","12.0" };
	// 高い順に生成できるか試していく
	for ( size_t i = 0; i < _countof(featureLevels); ++i ) {
		// 採用したアダプターでデバイスを生成
		hr_ = D3D12CreateDevice(useAdapter_.Get(), featureLevels[i], IID_PPV_ARGS(&device_));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if ( SUCCEEDED(hr_) ) {
			// 生成できたのでログ出力を行ってループを抜ける
			logManager_.Log(std::format("FeatrueLevel : {}\n", featureLevelStrings[i]));
			break;
		}

	}
	// デバイスの生成がうまくいかなかったので起動できない
	assert(device_ != nullptr);
	logManager_.Log(logManager_.ConvertString(L"Complete create D3D12Device!!!\n"));// 初期化完了のログを出す
}
/// コマンド関連の初期化
void DirectXCommon::CreateCommand(){
	assert(device_); // デバイスが生成済みであることを確認

	// コマンドキューの生成
	D3D12_COMMAND_QUEUE_DESC commandQuesDesc {};

	commandQuesDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 標準的なコマンドキュー
	commandQuesDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; // 標準的な優先度
	commandQuesDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; // フラグ無し
	commandQuesDesc.NodeMask = 0; // マルチアダプタ用。通常は0

	// コマンドキューを生成する
	hr_ = device_->CreateCommandQueue(&commandQuesDesc, IID_PPV_ARGS(&commandQueue_));
	// コマンドキューの生成が上手くいかなかったので起動できない
	assert(SUCCEEDED(hr_));
	// コマンドアフロケータを生成
	hr_ = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	// コマンドアロケータの生成が上手くいかなかったので起動出来ない
	assert(SUCCEEDED(hr_));
	// コマンドリストを生成する
	hr_ = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	// コマンドリストの生成が上手くいかなかったので起動できない
	assert(SUCCEEDED(hr_));

	// ここ重要：初期はCloseしておく
	commandList_->Close();
}
/// スワップチェーンの生成
void DirectXCommon::CreateSwapChain(){

	// スワップチェーンを生成する
	Microsoft::WRL::ComPtr<IDXGISwapChain1> tempSwapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc {};
	swapChainDesc.Width = windowProc_->GetClientWidth();   // 画面の幅。ウィンドウのクライアント領域を同じものにしていく
	swapChainDesc.Height = windowProc_->GetClientHeight(); // 画面の高さ。ウィンドウのクライアント領域を同じようにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
	swapChainDesc.SampleDesc.Count = 1; // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2; // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破棄
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; // アルファチャネルは使わない


	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr_ = dxgiFactory_->CreateSwapChainForHwnd(commandQueue_.Get(), windowProc_->GetHwnd(), &swapChainDesc, nullptr, nullptr, tempSwapChain.GetAddressOf());
	assert(SUCCEEDED(hr_));
	// IDXGISwapChain4を取得する
	hr_ = tempSwapChain.As(&swapChain_);
	assert(SUCCEEDED(hr_));

}
/// 深度バァファの生成
void DirectXCommon::CreateDepthBuffer(){
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 2Dテクスチャ
	desc.Width = static_cast< UINT >( windowProc_->GetClientWidth() ); // Textureの幅
	desc.Height = static_cast< UINT >( windowProc_->GetClientHeight() ); // Textureの幅
	desc.DepthOrArraySize = 1; // 奥行きor 配列Textureの配列数
	desc.MipLevels = 1; // mipmap
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
	desc.SampleDesc.Count = 1; // サンプリングカウント。1固定
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; // 標準的なレイアウト
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // デプスステンシルとして使うのでフラグを立てる
	// 利用するHeapの設定。非常に特殊な運用・
	D3D12_HEAP_PROPERTIES heapProperties {};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // 細かい設定を行う
	// 深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue {};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// 4. リソース作成
	hr_ = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue, IID_PPV_ARGS(&depthStencilResource_)
	);
	assert(SUCCEEDED(hr_));


}
///  各種でスクリプタヒープの生成
void DirectXCommon::CreateDescriptorHeaps(){

	rtvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 50, false);
	srvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	dsvDescriptorHeap_ = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	// DescriptorSizeを取得しておく
	desriptorSizeSRV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	desriptorSizeRTV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	desriptorSizeDSV_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);



}
/// レンダーターゲットビューの初期化
void DirectXCommon::CreateRenderTargetViews(){

	// DescriptorSizeを取得しておく
	rtvDescSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc {}; // レンダーターゲットビューの設定
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2dテクスチャとして読み込む
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // リニア出力なら UNORM に変える
	rtvDesc.Texture2D.MipSlice = 0; // ミップマップの何番目を使うか
	rtvDesc.Texture2D.PlaneSlice = 0; // プレーン何番目を使うか

	// ★ ハンドルをちゃんと受け取る
	D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// バックバッファごとにRTV作成
	for ( UINT i = 0; i < kBackBufferCount; ++i ) {
		hr_ = swapChain_->GetBuffer(i, IID_PPV_ARGS(&backBuffers_[i]));
		assert(SUCCEEDED(hr_));
		// RTVの生成
		device_->CreateRenderTargetView(backBuffers_[i].Get(), &rtvDesc, handle);

		// 次のディスクリプタへ
		handle.ptr += rtvDescSize_;
	}

}
/// 深度ステンシルビューの初期化
void DirectXCommon::CreateDepthStencilView(){


	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; //2dTexture

	// ヒープの先頭ハンドル取得
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();

	// DSVHeapの設定にDSVをつくる
	device_->CreateDepthStencilView(depthStencilResource_.Get(), &dsvDesc, dsvHandle);


}
/// フェンスの初期化
void DirectXCommon::CreateFence(){

	// 初期化0でFenceを作る

	fenceValue_ = 0;
	hr_ = device_->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr_));

	// FenceのSignalを待つためのイベントを作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_ != nullptr);

}
/// ビューポートの初期化
void DirectXCommon::InitializeViewport(){

	// クライアントサイズ取得
	float width = static_cast< float >( windowProc_->GetClientWidth() );
	float height = static_cast< float >( windowProc_->GetClientHeight() );

	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport_.Width = width;
	viewport_.Height = height;
	viewport_.TopLeftX = 0.0f;
	viewport_.TopLeftY = 0.0f;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

}
/// シザー矩形の初期化
void DirectXCommon::InitializeScissorRect(){



	// 基本的にビューボートと同じ矩形が構成されるようにする
	scissorRect_.left = 0;
	scissorRect_.right = windowProc_->GetClientWidth();
	scissorRect_.top = 0;
	scissorRect_.bottom = windowProc_->GetClientHeight();

}
/// DXCコンパイラの生成
void DirectXCommon::CreateDXCCompiler(){


	hr_ = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr_));
	hr_ = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr_));
	hr_ = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr_));

}

// FPS固定の初期化
void DirectXCommon::InitializeFixFPS(){

	// 現在時間を記録する
	reference_ = std::chrono::steady_clock::now();

	// OSのタイマー精度を1msにする
	// これを行わないと sleep_for(1us) でも 15ms ほど寝てしまうことがある
	timeBeginPeriod(1);

}
// FPS固定の更新
void DirectXCommon::UpdateFixFPS(){
	// 1/60秒数ぴったりの時間
	const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));
	// 1/60秒よりわずかに短い時間
	const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));
	// 現在時間を取得
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	// 前回記録からの経過時間を取得する
	std::chrono::microseconds elapsed = std::chrono::duration_cast< std::chrono::microseconds >( now - reference_ );

	// 無理に取り戻そうとせず、基準時間を現在時刻にリセットして諦める
	if ( elapsed > std::chrono::microseconds(100000) ) { // 0.1秒以上ズレたら
		reference_ = now;
		elapsed = std::chrono::microseconds(0); // 経過時間もリセット
	}

	// 1/60秒（よりわずかに短い時間）たっていない場合
	if ( elapsed < kMinCheckTime ){

		// 1/60秒経過するまで微小なスリープを繰り返す
		while ( std::chrono::steady_clock::now() - reference_ < kMinTime ){
			// 1マイクロ秒スリープ
			std::this_thread::sleep_for(std::chrono::microseconds(1));
		}
	}
	// 現在の時間を記録する
	reference_ = std::chrono::steady_clock::now();

}

// ディスクリプタヒープの生成
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible){

	// ディスクリプタヒープの生成
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc {};
	descriptorHeapDesc.Type = heapType; // 連打―ターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescriptors; // ダブルバッファ用に2つ。多くても別に構わない。
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	hr_ = device_->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	// ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr_));
	return descriptorHeap;
}
/// コマンドリセット
void DirectXCommon::ResetCommand(){

	// コマンドアロケータのリセット
	hr_ = commandAllocator_->Reset();
	assert(SUCCEEDED(hr_));
	// コマンドリストのリセット
	hr_ = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr_));
}
/// コマンド実行
void DirectXCommon::ExecuteCommand(){

	//  コマンドリストを確定
	hr_ = commandList_->Close();
	assert(SUCCEEDED(hr_));
	// コマンドリストをコマンドキューにセットして実行
	ID3D12CommandList* cmdLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(_countof(cmdLists), cmdLists);

}
/// GPU完了待ち
void DirectXCommon::WaitForGPU(){
	// フェンスで GPU 完了待ち
	const UINT64 fenceToWaitFor = ++fenceValue_;
	hr_ = commandQueue_->Signal(fence_.Get(), fenceToWaitFor);
	assert(SUCCEEDED(hr_));
	// GPUが fence の値を fenceToWaitFor まで進めるまで待つ
	if ( fence_->GetCompletedValue() < fenceToWaitFor ) {
		hr_ = fence_->SetEventOnCompletion(fenceToWaitFor, fenceEvent_);
		assert(SUCCEEDED(hr_));
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}
// デバッグレイヤーの初期化
void DirectXCommon::InitializeDebugLayer(){

#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) ) {
		// デバックレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(FALSE);
	}
#endif // _DEBUG


}
/// InfoQueueの初期化
void DirectXCommon::InitializeInfoQueue(){
#ifdef _DEBUG

	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if ( SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&infoQueue))) ) {
		// やばいエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		// 警告時に泊まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
		// 抑制するメッセージのＩＤ
		D3D12_MESSAGE_ID denyIds[] = {
			// windows11でのDXGIデバックレイヤーとDX12デバックレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,D3D12_MESSAGE_ID_LIVE_DEVICE, D3D12_MESSAGE_ID_LIVE_SHADERCACHESESSION, };
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


}

uint32_t DirectXCommon::GetClientWidth() const {
	return windowProc_->GetClientWidth();
}

uint32_t DirectXCommon::GetClientHeight() const {
	return windowProc_->GetClientHeight();
}



#pragma region GetCPU

///　CPUディスクリプタハンドルの取得

D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index){

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += ( descriptorSize * index );
	return handleCPU;
}
///　CPUディスクリプタハンドルの取得
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCPUDescriptorHandle(uint32_t descriptorSize, uint32_t index){
	return GetCPUDescriptorHandle(srvDescriptorHeap_, descriptorSize, index);
}


#pragma endregion

#pragma region GetGPU

///　GPUディスクリプタハンドルの取得
D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& descriptorHeap, uint32_t descriptorSize, uint32_t index){

	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += ( descriptorSize * index );
	return handleGPU;
}
///　GPUディスクリプタハンドルの取得
D3D12_GPU_DESCRIPTOR_HANDLE DirectXCommon::GetGPUDescriptorHandle(uint32_t descriptorSize, uint32_t index){

	return GetGPUDescriptorHandle(srvDescriptorHeap_, descriptorSize, index);

}
#pragma endregion
