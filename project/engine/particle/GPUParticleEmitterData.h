#pragma once
#include <string>

#include "engine/math/struct.h"

// GPUパーティクルエミッタのデータ構造体
struct GPUParticleEmitterData{
	

	std::string name = "emitter"; // エミッタの名前

	Vector3 position { 0.0f, 0.0f, 0.0f }; // 発生位置

	float emitRate = 10.0f;  // 1秒に何個出すか
	bool  loop = true;   // trueなら永遠に出し続ける
	bool  active = true;   // falseにすると一時停止


	Vector3 velocity { 0.0f, 1.0f, 0.0f }; // 初速
	float velocitySpread = 0.0f; // 速度にランダム値を加えるときの最大値 (0ならランダムなし)


	// パーティクルの寿命
	float lifeTimeMin = 1.0f;
	float lifeTimeMax = 2.0f;



	float scaleMin = 0.1f; // スケールの最小値
	float scaleSpread = 0.0f; // スケールにランダム値を加えるときの最大値 (0ならランダムなし)
	float scaleMax = 1.0f; // スケールの最大値

	Vector4 startColor { 1.0f, 1.0f, 1.0f, 1.0f }; // 開始色

	Vector4 endColor { 1.0f, 1.0f, 1.0f, 0.0f }; // 終了色


	float gravityY = -0.098f; // 重力の強さ (Y方向)

	int burstCount = 10;
};