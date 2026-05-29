#pragma once

#include "engine/audio/AudioManager.h"

namespace GameBGM {

	// 各シーン用のBGMパス
	// 今はすべて Morning.mp3 を仮で入れてあり、あとで個別に差し替えやすい形にしている
	inline constexpr const char* kTitle = "resources/Audio/Caravan.mp3";
	inline constexpr const char* kGameScene = "resources/Audio/Battle_in_the_Moonlight.mp3";
	inline constexpr const char* kBoss = "resources/Audio/全てを創造する者「Dominus_Deus」.mp3";
	inline constexpr const char* kGameClear = "resources/Audio/栄光のファンファーレ.mp3";
	inline constexpr const char* kGameOver = "resources/Audio/絶望の淵から.mp3";

	//inline void Play(const char* bgmPath, float fadeInSec = 0.0f) {
	//	// 指定したBGMを再生する
	//	AudioManager::GetInstance()->PlayBGM(bgmPath, fadeInSec);
	//}

	//inline void Change(const char* bgmPath, float fadeSec = 0.5f) {
	//	// 指定したBGMへ切り替える
	//	AudioManager::GetInstance()->ChangeBGM(bgmPath, fadeSec);
	//}

	inline void Play(const char* bgmPath, float fadeInSec = 0.0f) {
		AudioManager* audio = AudioManager::GetInstance();
		if (audio->IsBGMPlaying() && audio->GetCurrentBGMName() == bgmPath) {
			return;
		}
		audio->PlayBGM(bgmPath, fadeInSec);
	}

	inline void Change(const char* bgmPath, float fadeSec = 0.5f) {
		AudioManager* audio = AudioManager::GetInstance();
		if (audio->IsBGMPlaying() && audio->GetCurrentBGMName() == bgmPath) {
			return;
		}
		audio->ChangeBGM(bgmPath, fadeSec);
	}

	// シーンごとの呼び出し関数
	inline void Title(float fadeInSec = 0.0f) { Play(kTitle, fadeInSec); }
	inline void GameScene(float fadeInSec = 0.0f) { Play(kGameScene, fadeInSec); }
	inline void Boss(float fadeSec = 0.5f) { Change(kBoss, fadeSec); }
	inline void GameClear(float fadeSec = 0.5f) { Change(kGameClear, fadeSec); }
	inline void GameOver(float fadeSec = 0.5f) { Change(kGameOver, fadeSec); }
}