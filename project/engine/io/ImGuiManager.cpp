//#include "ImGuiManager.h"
//extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
//
//
//void ImGuiManager::Initialize(HWND hwnd, ComPtr<ID3D12Device> device, DXGI_FORMAT rtvFormat, UINT numFramesInFlight, ComPtr<ID3D12DescriptorHeap> srvHeap){
//
//	hwnd_ = hwnd;
//	numFrames_ = numFramesInFlight;
//	device_ = device;
//	srvHeap_ = srvHeap;
//
//	ImGui::CreateContext();
//	ImGui::StyleColorsDark();
//	ImGui_ImplWin32_Init(hwnd_);
//	ImGui_ImplDX12_Init(
//		device_.Get(),
//		numFrames_,
//		rtvFormat,
//		srvHeap_.Get(),
//		srvHeap_->GetCPUDescriptorHandleForHeapStart(),
//		srvHeap_->GetGPUDescriptorHandleForHeapStart());
//
//
//
//}
//
//void ImGuiManager::Begin(){
//	ImGui_ImplDX12_NewFrame();
//	ImGui_ImplWin32_NewFrame();
//	ImGui::NewFrame();
//}
//void ImGuiManager::End(ComPtr<ID3D12GraphicsCommandList> commandList){
//	ImGui::Render(); // ImGui描画のデータ生成（内部コマンドバッファみたいなもの）
//	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());
//}
//
//void ImGuiManager::Render(ComPtr<ID3D12GraphicsCommandList> commandList){
//	End(commandList); // EndにまとめているならRenderは呼び出し元では不要
//}
//void ImGuiManager::Shutdown(){
//	ImGui_ImplDX12_Shutdown();
//	ImGui_ImplWin32_Shutdown();
//	ImGui::DestroyContext();
//}
//
//bool ImGuiManager::WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
//	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
//}
