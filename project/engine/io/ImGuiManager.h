//#pragma once
//#include "externals/imgui/imgui_impl_dx12.h"
//#include "externals/imgui/imgui_impl_win32.h"
//#include<cstdint>
//#include <wtypes.h>
//#include <d3d12.h>
//#include <wrl.h> // Microsoft::WRL::ComPtr
//
//using Microsoft::WRL::ComPtr;
//
//
//class ImGuiManager{
//public:
//    void Initialize(HWND hwnd, ComPtr<ID3D12Device> device, DXGI_FORMAT rtvFormat, UINT numFramesInFlight, ComPtr<ID3D12DescriptorHeap> srvHeap);
//    void Begin();
//    void End(ComPtr<ID3D12GraphicsCommandList> commandList);
//    void Render(ComPtr<ID3D12GraphicsCommandList> commandList);
//    void Shutdown();
//
//    bool WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
//
//private:
//    HWND hwnd_ = nullptr;
//    UINT numFrames_ = 0;
//    ComPtr<ID3D12Device> device_ = nullptr;
//    ComPtr<ID3D12DescriptorHeap> srvHeap_ = nullptr;
//};
//
