#pragma once

// --- エンジン側のファイル ---
#include "struct.h"



// --- 標準ライブラリ ---]
#include <vector>
#include <string>
#include <map>



// キーフレームを表す構造体
template<typename tValue>

struct Keyframe{
	float time; // キーフレームの時間
	tValue value; // キーフレームの値
};

// キーフレームの型エイリアス
using KeyFrameVector3 = Keyframe<Vector3>;
using KeyFrameQuaternion = Keyframe<Quaternion>;

// アニメーションカーブを表す構造体
template <typename tValue>
struct AnimationCurve{
	std::vector<Keyframe<tValue>> keyframes;
};

struct NodeAnimation{
	AnimationCurve<Vector3> translate; // 移動（位置）のカーブ
	AnimationCurve<Quaternion> rotate; // 回転のカーブ
	AnimationCurve<Vector3> scale;     // 拡大縮小のカーブ
};

// アニメーション全体を表す構造体
struct Animation{
	float duration; // アニメーション全体の長さ（尺：秒）

	// 「骨の名前（文字列）」をキーにして、その骨の動きのデータを引き出せる辞書
	std::map<std::string, NodeAnimation> nodeAnimations;
};


Animation LoadAnimationFromFile(const std::string& directoryPath, const std::string& filename, uint32_t animationIndex = 0);

// ファイル内のクリップ名一覧（glTFの複数アニメーション対応。名前→indexの対応は並び順）
std::vector<std::string> GetAnimationClipNames(const std::string& directoryPath, const std::string& filename);


// 指定した時刻（time）の Vector3 の値を計算する関数（移動・スケール用）
Vector3 CalculateValue(const std::vector<KeyFrameVector3>& keyframes, float time);

// 指定した時刻（time）の Quaternion の値を計算する関数（回転用）
Quaternion CalculateValue(const std::vector<KeyFrameQuaternion>& keyframes, float time);