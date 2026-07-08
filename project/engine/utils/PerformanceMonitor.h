#pragma once
#include <cstdint>
#include <wrl.h>
#include <dxgi1_4.h>

// =====================================================================
//  PerformanceMonitor
//   FPS/フレーム時間/CPU時間の履歴グラフ、min/avg/max、
//   メモリ(RAM)・VRAM 使用量、ドローコール数などをまとめて表示する。
//   （EditorManager にあったインライン実装をクラス化して強化）
// =====================================================================
class PerformanceMonitor {
public:
    void Initialize();

    // 毎フレーム呼ぶ（cpuUpdateMs/cpuDrawMs はシーン計測値）
    void DrawDebugUI(float cpuUpdateMs, float cpuDrawMs);

private:
    // リングバッファ長（4秒分@60fps）
    static constexpr int kHistory = 240;

    void PushSample(float* buf, float v);
    void MinAvgMax(const float* buf, float& outMin, float& outAvg, float& outMax) const;

    float fpsHistory_[kHistory] = {};
    float frameMsHistory_[kHistory] = {};
    float cpuUpdateHistory_[kHistory] = {};
    float cpuDrawHistory_[kHistory] = {};
    float drawCallHistory_[kHistory] = {};
    int   offset_ = 0;      // 全バッファ共通の書き込み位置
    int   filled_ = 0;      // 有効サンプル数（起動直後のゴミ除け）
    bool  paused_ = false;  // グラフ更新の一時停止

    // VRAM 取得用（自前でアダプタを列挙するのでエンジン本体に手を入れない）
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3_;
    bool vramAvailable_ = false;
};
