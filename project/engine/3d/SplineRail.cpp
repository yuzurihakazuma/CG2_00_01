#include "SplineRail.h"
#include <algorithm>
#include <cmath>

// 進行度totalTに応じた位置を計算して返す関数
Vector3 SplineRail::EvaluatePosition(float totalT) const{

	// ノードが4つ以上必要
	if ( nodes.size() < 2 ) {
		// ノードが足りない場合は、デフォルトの位置を返す（例: 原点）
		return nodes.empty() ? Vector3 { 0,0,0 } : nodes[0].position;
	}

	// ノード数に応じた最大のT値
	float maxT = static_cast< float >( nodes.size() - 1 );

	// totalTが0未満なら最初のノードの位置を、maxTを超えるなら最後のノードの位置を返す
	if ( totalT <= 0.0f ){ return nodes.front().position; } // 0未満なら最初のノードの位置を返す
	if ( totalT >= maxT ){ return nodes.back().position; } // maxTを超えるなら最後のノードの位置を返す

	// totalTを整数部分と小数部分に分ける
	int index = static_cast< int >( std::floor(totalT) );
	float t = totalT - index; // 小数部分

	// Catmull-Romスプラインの4点を選ぶ

	Vector3 p0 = nodes[std::max(index - 1, 0 )].position; // 前のノード（存在しない場合は最初のノードを使う）
	Vector3 p1 = nodes[index].position; // 現在のノード
	Vector3 p2 = nodes[std::min(index + 1, static_cast< int >( nodes.size() ) - 1)].position; // 次のノード（存在しない場合は最後のノードを使う）
	Vector3 p3 = nodes[std::min(index + 2, static_cast< int >( nodes.size() ) - 1)].position; // 次の次のノード（存在しない場合は最後のノードを使う）

	// 4点とtを使ってCatmull-Romスプラインの位置を計算して返す
	return VectorMath::CatmullRom(p0, p1, p2, p3, t);

}

// デバッグ用の描画関数
void SplineRail::DrawDebug(){

	if ( nodes.size() < 2 ){ return; }
	// ノード数に応じた最大のT値
	float maxT = static_cast< float >( nodes.size() - 1 );
	float step = 0.1f; // 描画の分割数（小さいほど滑らかになるが重くなる）

	Vector3 prevPos = EvaluatePosition(0.0f); // 最初の位置
	// 0からmaxTまで、stepごとに位置を計算して線を描く
	
	for ( float t = step; t <= maxT; t += step ){ // tは0.1刻みで進める

		// 進行度tにおける位置を計算
		Vector3 currentPos = EvaluatePosition(t);

		// prevPosからcurrentPosまでの線を描く
		prevPos = currentPos;

	}


}
