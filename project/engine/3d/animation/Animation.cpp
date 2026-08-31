#include "Animation.h"
// --- 外部ライブラリ ---
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// --- 標準ライブラリ ---
#include <cassert>

// --- エンジン側のファイル ---
#include "engine/math/QuaternionMath.h"

using namespace QuaternionMath;

// ファイル内のアニメーションクリップ名一覧（glTFの複数クリップ対応。Idle/Walk等の切り替えに使う）
std::vector<std::string> GetAnimationClipNames(const std::string& directoryPath, const std::string& filename){
	std::vector<std::string> names;
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(directoryPath + "/" + filename,
		aiProcess_Triangulate | aiProcess_FlipUVs);
	if ( !scene ) return names;
	for ( uint32_t i = 0; i < scene->mNumAnimations; ++i ) {
		names.push_back(scene->mAnimations[i]->mName.C_Str());
	}
	return names;
}

// アニメーションのファイルからの読み込み（animationIndex でクリップを選べる。既定=先頭）
Animation LoadAnimationFromFile(const std::string& directoryPath, const std::string& filename, uint32_t animationIndex){
	// Assimpを使ってアニメーションを読み込むコードを書いていきます。
	Animation animation;
	Assimp::Importer importer;
	// ファイルのパスを作成
	std::string filePath = directoryPath + "/" + filename;

	// ファイルを読み込む
	const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs);

	// 読み込みに失敗した場合はエラーを出力して終了
	assert(scene && scene->mNumAnimations > 0 && "Failed to load animation file or no animations found!");

	if ( animationIndex >= scene->mNumAnimations ) { animationIndex = 0; } // 範囲外は先頭へ
	aiAnimation* animationAssimp = scene->mAnimations[animationIndex];

	// アニメーションの全体の長さを秒単位で計算
	float ticksPerSecond = static_cast< float >( animationAssimp->mTicksPerSecond != 0.0 ? animationAssimp->mTicksPerSecond : 25.0 ); // デフォルトは25fps

	// 各ノードアニメーションを処理
	animation.duration = static_cast< float >( animationAssimp->mDuration ) / ticksPerSecond; 

	// アニメーションの各チャネル（ノードアニメーション）を処理
	for ( uint32_t channelIndex = 0; channelIndex < animationAssimp->mNumChannels; ++channelIndex ) {
	
		// チャネル（ノードアニメーション）を取得
		aiNodeAnim* nodeAnimationAssimp = animationAssimp->mChannels[channelIndex];
	
		std::string nodeName = nodeAnimationAssimp->mNodeName.C_Str(); // ノードの名前を取得
		NodeAnimation& nodeAnimation = animation.nodeAnimations[nodeName]; // ノードアニメーションを取得（存在しない場合は新規作成される）
		// キーフレームを処理
		for ( uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumPositionKeys; ++keyIndex ){
			
			// キーフレームを取得
			aiVectorKey& keyAssimp = nodeAnimationAssimp->mPositionKeys[keyIndex];
			
			// キーフレームの時間を秒単位で計算
			KeyFrameVector3 keyframe;
			
			// キーフレームの値を変換
			keyframe.time = static_cast< float >(keyAssimp.mTime) / ticksPerSecond;

			// Assimpの座標系は右手系で、エンジンの座標系が左手系の場合は、Z軸を反転させる必要があります。
			keyframe.value = { -keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };

			// キーフレームをノードアニメーションのtranslateカーブに追加
			nodeAnimation.translate.keyframes.push_back(keyframe);
		}
		// 回転のキーフレームを処理
		for ( uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumRotationKeys; ++keyIndex ) {
			// キーフレームを取得
			aiQuatKey& keyAssimp = nodeAnimationAssimp->mRotationKeys[keyIndex];
			// キーフレームの時間を秒単位で計算
			KeyFrameQuaternion keyframe;
			// キーフレームの値を変換
			keyframe.time = static_cast< float >(keyAssimp.mTime) / ticksPerSecond;
			// Assimpの座標系は右手系で、エンジンの座標系が左手系の場合は、Y軸とZ軸を反転させる必要があります。
			keyframe.value = { keyAssimp.mValue.x, -keyAssimp.mValue.y, -keyAssimp.mValue.z, keyAssimp.mValue.w };
			// キーフレームをノードアニメーションのrotateカーブに追加
			nodeAnimation.rotate.keyframes.push_back(keyframe);
		}
		// スケールのキーフレームを処理
		for ( uint32_t keyIndex = 0; keyIndex < nodeAnimationAssimp->mNumScalingKeys; ++keyIndex ) {
			// キーフレームを取得
			aiVectorKey& keyAssimp = nodeAnimationAssimp->mScalingKeys[keyIndex];
			// キーフレームの時間を秒単位で計算
			KeyFrameVector3 keyframe;
			// キーフレームの値を変換
			keyframe.time = static_cast< float >(keyAssimp.mTime) / ticksPerSecond;

			// Assimpの座標系は右手系で、エンジンの座標系が左手系の場合は、Z軸を反転させる必要があります。
			keyframe.value = { keyAssimp.mValue.x, keyAssimp.mValue.y, keyAssimp.mValue.z };
			// キーフレームをノードアニメーションのscaleカーブに追加
			nodeAnimation.scale.keyframes.push_back(keyframe);
		}
	}
	// 読み込んだアニメーションデータを返す
	return animation;
}

// 指定した時刻（time）の Vector3 の値を計算する関数（移動・スケール用）
Vector3 CalculateValue(const std::vector<KeyFrameVector3>& keyframes, float time){
	// 安全装置1：キーフレームが空ならエラー（プログラムを止める）
	assert(!keyframes.empty());

	// 安全装置2：キーが1つしかない、または現在時刻が最初のキーフレームより前の場合は、最初の値を返す
	if ( keyframes.size() == 1 || time <= keyframes[0].time ) {
		return keyframes[0].value;
	}

	// すべてのキーフレームを順番に調べて、「今の時間」がどのフレームの間にあるかを探す
	for ( size_t index = 0; index < keyframes.size() - 1; ++index ) {
		size_t nextIndex = index + 1;

		// 今の時間が、index番目 と nextIndex番目 の間にあるか判定
		if ( keyframes[index].time <= time && time <= keyframes[nextIndex].time ) {

			// 「2つのフレームの間の、何％（0.0〜1.0）の地点にいるか」を計算する
			float t = ( time - keyframes[index].time ) / ( keyframes[nextIndex].time - keyframes[index].time );

			// Vector3の線形補間（Lerp）： 開始値 + (終了値 - 開始値) * t
			Vector3 start = keyframes[index].value;
			Vector3 end = keyframes[nextIndex].value;
			return start + ( end - start ) * t;
			// ※ struct.h で operator+ や operator* が定義されているため、この直感的な式がそのまま動きます！
		}
	}

	// もし時間が一番最後のキーフレームを過ぎていたら、最後の値を返す
	return keyframes.back().value;
}

Quaternion CalculateValue(const std::vector<KeyFrameQuaternion>& keyframes, float time){
	// 安全装置1：キーフレームが空ならエラー
	assert(!keyframes.empty());

	// 安全装置2：キーが1つしかない、または現在時刻が最初のキーフレームより前の場合
	if ( keyframes.size() == 1 || time <= keyframes[0].time ) {
		return keyframes[0].value;
	}

	// どのフレームの間にあるかを探す
	for ( size_t index = 0; index < keyframes.size() - 1; ++index ) {
		size_t nextIndex = index + 1;

		if ( keyframes[index].time <= time && time <= keyframes[nextIndex].time ) {

			// 何％（t）の地点にいるかを計算
			float t = ( time - keyframes[index].time ) / ( keyframes[nextIndex].time - keyframes[index].time );

			// 前もって作っておいた魔法の関数「Slerp（球面線形補間）」を使う！
			return QuaternionMath::Slerp(keyframes[index].value, keyframes[nextIndex].value, t);
		}
	}

	// 時間が過ぎていたら最後の値を返す
	return keyframes.back().value;
}