#include "PerformanceMonitor.h"

#include <algorithm>
#include <cstdio>

#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "dxgi.lib")

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
#include "engine/base/TimeManager.h"
#include "engine/utils/RenderStats.h"

void PerformanceMonitor::Initialize() {
    // VRAM 照会用に IDXGIAdapter3 を自前で取得（エンジン本体は触らない）
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if ( SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if ( SUCCEEDED(factory->EnumAdapters1(0, &adapter)) ) {
            if ( SUCCEEDED(adapter.As(&adapter3_)) ) {
                vramAvailable_ = true;
            }
        }
    }
}

void PerformanceMonitor::PushSample(float* buf, float v) {
    buf[offset_] = v;
}

void PerformanceMonitor::MinAvgMax(const float* buf, float& outMin, float& outAvg, float& outMax) const {
    int n = filled_ < kHistory ? filled_ : kHistory;
    if ( n <= 0 ) { outMin = outAvg = outMax = 0.0f; return; }
    float mn = buf[0], mx = buf[0], sum = 0.0f;
    for ( int i = 0; i < n; ++i ) {
        mn = ( std::min )( mn, buf[i] );
        mx = ( std::max )( mx, buf[i] );
        sum += buf[i];
    }
    outMin = mn; outAvg = sum / ( float ) n; outMax = mx;
}

void PerformanceMonitor::DrawDebugUI(float cpuUpdateMs, float cpuDrawMs) {
#ifdef USE_IMGUI
    ImGui::Begin("パフォーマンスモニター");

    float fps = ImGui::GetIO().Framerate;
    float frameMs = ( fps > 0.0f ) ? 1000.0f / fps : 0.0f;
    float drawCalls = ( float ) RenderStats::GetInstance()->GetDrawCalls();

    // --- サンプル更新（一時停止中は積まない） ---
    if ( !paused_ ) {
        PushSample(fpsHistory_,       fps);
        PushSample(frameMsHistory_,   frameMs);
        PushSample(cpuUpdateHistory_, cpuUpdateMs);
        PushSample(cpuDrawHistory_,   cpuDrawMs);
        PushSample(drawCallHistory_,  drawCalls);
        offset_ = ( offset_ + 1 ) % kHistory;
        if ( filled_ < kHistory ) ++filled_;
    }

    ImGui::Checkbox("グラフを一時停止", &paused_);

    // =========================================================
    // FPS
    // =========================================================
    float mn, avg, mx;
    MinAvgMax(fpsHistory_, mn, avg, mx);
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "FPS %.1f (min %.0f / avg %.0f / max %.0f)", fps, mn, avg, mx);
    ImGui::PlotLines("##fps", fpsHistory_, kHistory, offset_, overlay, 0.0f, 120.0f, ImVec2(-1, 60));

    // =========================================================
    // フレーム時間（16.6ms 基準線の代わりに範囲を0〜33msに固定）
    // =========================================================
    MinAvgMax(frameMsHistory_, mn, avg, mx);
    snprintf(overlay, sizeof(overlay), "フレーム %.2f ms (avg %.2f / max %.2f)", frameMs, avg, mx);
    ImGui::PlotLines("##frame", frameMsHistory_, kHistory, offset_, overlay, 0.0f, 33.3f, ImVec2(-1, 50));

    if ( ImGui::CollapsingHeader("CPU 内訳", ImGuiTreeNodeFlags_DefaultOpen) ) {
        MinAvgMax(cpuUpdateHistory_, mn, avg, mx);
        snprintf(overlay, sizeof(overlay), "Update %.3f ms (avg %.3f)", cpuUpdateMs, avg);
        ImGui::PlotLines("##cpuU", cpuUpdateHistory_, kHistory, offset_, overlay, 0.0f, 8.0f, ImVec2(-1, 40));
        MinAvgMax(cpuDrawHistory_, mn, avg, mx);
        snprintf(overlay, sizeof(overlay), "Draw %.3f ms (avg %.3f)", cpuDrawMs, avg);
        ImGui::PlotLines("##cpuD", cpuDrawHistory_, kHistory, offset_, overlay, 0.0f, 8.0f, ImVec2(-1, 40));
    }

    if ( ImGui::CollapsingHeader("描画統計", ImGuiTreeNodeFlags_DefaultOpen) ) {
        MinAvgMax(drawCallHistory_, mn, avg, mx);
        snprintf(overlay, sizeof(overlay), "ドローコール %u (max %.0f)",
            RenderStats::GetInstance()->GetDrawCalls(), mx);
        ImGui::PlotLines("##dc", drawCallHistory_, kHistory, offset_, overlay, 0.0f, mx * 1.5f + 1.0f, ImVec2(-1, 40));
        ImGui::Text("時間 (Time) : 経過 %.1f s / フレーム %llu",
            Time::GetInstance()->GetTotalTime(),
            ( unsigned long long ) Time::GetInstance()->GetFrameCount());
    }

    // =========================================================
    // メモリ / VRAM
    // =========================================================
    if ( ImGui::CollapsingHeader("メモリ", ImGuiTreeNodeFlags_DefaultOpen) ) {
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        if ( GetProcessMemoryInfo(GetCurrentProcess(),
                ( PROCESS_MEMORY_COUNTERS* ) &pmc, sizeof(pmc)) ) {
            float ramMB = ( float ) pmc.WorkingSetSize / ( 1024.0f * 1024.0f );
            ImGui::Text("RAM 使用量  : %.1f MB", ramMB);
        }
        if ( vramAvailable_ ) {
            DXGI_QUERY_VIDEO_MEMORY_INFO vmi{};
            if ( SUCCEEDED(adapter3_->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vmi)) ) {
                float usedMB   = ( float ) vmi.CurrentUsage / ( 1024.0f * 1024.0f );
                float budgetMB = ( float ) vmi.Budget       / ( 1024.0f * 1024.0f );
                ImGui::Text("VRAM 使用量 : %.1f / %.1f MB", usedMB, budgetMB);
                float rate = budgetMB > 0.0f ? usedMB / budgetMB : 0.0f;
                ImVec4 col = rate > 0.85f ? ImVec4(1, 0.3f, 0.3f, 1)
                           : rate > 0.6f  ? ImVec4(1, 0.8f, 0.2f, 1)
                                          : ImVec4(0.3f, 0.9f, 0.3f, 1);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
                ImGui::ProgressBar(rate, ImVec2(-1, 0));
                ImGui::PopStyleColor();
            }
        }
    }

    // =========================================================
    // 診断
    // =========================================================
    ImGui::Separator();
    float totalCpu = cpuUpdateMs + cpuDrawMs;
    if ( fps < 55.0f ) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " 警告: 処理落ちが発生しています！");
        if ( totalCpu > 16.0f ) {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                " 原因: CPUの処理が重いです\n（計算やループ処理が多すぎます）");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                " 原因: GPUの処理が重いです\n（描画する量が多すぎるか、シェーダーが重いです）");
        }
    } else {
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), " 快適に動作しています！ (60 FPS維持)");
    }

    ImGui::End();
#endif
}
