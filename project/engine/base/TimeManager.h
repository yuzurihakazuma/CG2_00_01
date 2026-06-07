#pragma once
// =============================================================
//  Time : エンジン共通の時間管理（デルタタイム / ポーズ / スロー）
//  - ヘッダオンリー（.vcxproj への追加不要）
//  - Framework::Update() の先頭で Update() を毎フレーム呼ぶ
//  - 既定 timeScale = 1.0 なので導入しても従来挙動は変わらない
//      GetDeltaTime()         : timeScale 適用済み（ゲーム・アニメ用。ポーズで0になる）
//      GetUnscaledDeltaTime() : 実時間（UI やエディタなど止めたくないもの用）
//  ※ ファイル名は Time.h にすると C標準 <time.h> と衝突するため TimeManager.h
// =============================================================
#include <chrono>
#include <cstdint>

class Time {
public:
	static Time* GetInstance() {
		static Time instance;
		return &instance;
	}

	// 毎フレーム1回、フレーム先頭で呼ぶ
	void Update() {
		const auto now = clock::now();
		if (!started_) {
			last_ = now;
			started_ = true;
		}
		float dt = std::chrono::duration<float>(now - last_).count();
		last_ = now;

		// ブレークポイントや初回フレームでの異常な大ジャンプを防ぐためにクランプ
		if (dt > maxDelta_) { dt = maxDelta_; }
		if (dt < 0.0f) { dt = 0.0f; }

		unscaledDeltaTime_ = dt;
		deltaTime_ = dt * timeScale_;
		totalTime_ += deltaTime_;
		unscaledTotalTime_ += unscaledDeltaTime_;
		++frameCount_;
	}

	// timeScale を適用したデルタタイム（秒）。ポーズ中は 0
	float GetDeltaTime() const { return deltaTime_; }
	// 実時間のデルタタイム（秒）。ポーズの影響を受けない
	float GetUnscaledDeltaTime() const { return unscaledDeltaTime_; }
	// 起動からの累計（scaled / unscaled）
	float GetTotalTime() const { return totalTime_; }
	float GetUnscaledTotalTime() const { return unscaledTotalTime_; }
	// 起動からのフレーム数
	uint64_t GetFrameCount() const { return frameCount_; }

	// 時間の倍率。0=ポーズ、0.5=スロー、2.0=倍速
	void SetTimeScale(float scale) { timeScale_ = (scale < 0.0f) ? 0.0f : scale; }
	float GetTimeScale() const { return timeScale_; }
	bool IsPaused() const { return timeScale_ <= 0.0f; }

	// デルタタイムの上限（秒）。既定 0.1（=最低10fps相当でクランプ）
	void SetMaxDelta(float seconds) { maxDelta_ = seconds; }

private:
	Time() = default;
	Time(const Time&) = delete;
	Time& operator=(const Time&) = delete;

	using clock = std::chrono::steady_clock;

	clock::time_point last_{};
	bool   started_ = false;
	float  deltaTime_ = 0.0f;
	float  unscaledDeltaTime_ = 0.0f;
	float  totalTime_ = 0.0f;
	float  unscaledTotalTime_ = 0.0f;
	float  timeScale_ = 1.0f;
	float  maxDelta_ = 0.1f;
	uint64_t frameCount_ = 0;
};
