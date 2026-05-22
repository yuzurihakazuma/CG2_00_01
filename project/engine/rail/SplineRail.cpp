#include "SplineRail.h"
#include <cmath>
#include <algorithm>

// --- ベクトルの長さを求める補助関数（ご自身のエンジンに合わせて修正してください） ---
inline float Length(const Vector3& v){
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ========================================================
// 追加: 指定したt（進行度）における座標を計算して返す関数
// （Catmull-Rom スプラインを使用して滑らかな曲線を計算します）
// ========================================================
Vector3 SplineRail::EvaluatePosition(float t) const{
    if ( nodes.empty() ) return { 0.0f, 0.0f, 0.0f };
    if ( nodes.size() == 1 ) return nodes[0];

    // tの整数部分(インデックス)と小数部分(ローカルt)を分ける
    int p1_index = static_cast< int >( t );
    float localT = t - p1_index;

    // 4点（p0, p1, p2, p3）のインデックスを計算
    int p0_index = p1_index - 1;
    int p2_index = p1_index + 1;
    int p3_index = p1_index + 2;

    // 配列の範囲外にアクセスしないようにクランプ（安全装置）
    int maxIndex = static_cast< int >( nodes.size() - 1 );
    if ( p0_index < 0 ) p0_index = 0;
    if ( p1_index > maxIndex ) p1_index = maxIndex;
    if ( p2_index > maxIndex ) p2_index = maxIndex;
    if ( p3_index > maxIndex ) p3_index = maxIndex;

    Vector3 p0 = nodes[p0_index];
    Vector3 p1 = nodes[p1_index];
    Vector3 p2 = nodes[p2_index];
    Vector3 p3 = nodes[p3_index];

    // Catmull-Rom スプラインの公式を使って曲線を計算
    Vector3 result;
    result.x = 0.5f * ( ( 2.0f * p1.x ) + ( -p0.x + p2.x ) * localT + ( 2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x ) * ( localT * localT ) + ( -p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x ) * ( localT * localT * localT ) );
    result.y = 0.5f * ( ( 2.0f * p1.y ) + ( -p0.y + p2.y ) * localT + ( 2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y ) * ( localT * localT ) + ( -p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y ) * ( localT * localT * localT ) );
    result.z = 0.5f * ( ( 2.0f * p1.z ) + ( -p0.z + p2.z ) * localT + ( 2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z ) * ( localT * localT ) + ( -p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z ) * ( localT * localT * localT ) );

    return result;
}

// ① レールの長さを計測してテーブルを作る
void SplineRail::BuildDistanceTable(){
    distanceTable_.clear();
    tTable_.clear();
    totalLength_ = 0.0f;

    if ( nodes.size() < 2 ) return;

    // 全体のtの最大値（ノード数 - 1 など、既存の実装に合わせてください）
    float maxT = static_cast< float >( nodes.size() - 1 );

    // 1つの区間を何分割して距離を測るか（精度）
    const int resolution = 20;
    int totalSamples = static_cast< int >( maxT * resolution );

    Vector3 prevPos = EvaluatePosition(0.0f);
    distanceTable_.push_back(0.0f);
    tTable_.push_back(0.0f);

    for ( int i = 1; i <= totalSamples; ++i ) {
        float t = maxT * ( static_cast< float >( i ) / totalSamples );
        Vector3 currentPos = EvaluatePosition(t);

        // 前の点からの距離を足していく
        totalLength_ += Length({ currentPos.x - prevPos.x, currentPos.y - prevPos.y, currentPos.z - prevPos.z });

        distanceTable_.push_back(totalLength_);
        tTable_.push_back(t);

        prevPos = currentPos;
    }
}

// ② 進んだ距離から「t」を求める（これが等速移動の要！）
float SplineRail::GetTFromDistance(float targetDistance) const{
    if ( distanceTable_.empty() ) return 0.0f;
    if ( targetDistance <= 0.0f ) return 0.0f;
    if ( targetDistance >= totalLength_ ) return tTable_.back();

    // テーブルの中から目的の距離を挟む2点を探す
    for ( size_t i = 0; i < distanceTable_.size() - 1; ++i ) {
        if ( distanceTable_[i] <= targetDistance && targetDistance <= distanceTable_[i + 1] ) {
            // 2点間を線形補間（Lerp）して正確な t を割り出す
            float segmentDist = distanceTable_[i + 1] - distanceTable_[i];
            float ratio = ( targetDistance - distanceTable_[i] ) / segmentDist;
            return tTable_[i] + ( tTable_[i + 1] - tTable_[i] ) * ratio;
        }
    }
    return tTable_.back();
}

Vector3 SplineRail::EvaluateTangent(float t) const{
    float delta = 0.01f;
    float maxT = nodes.empty() ? 0.0f : static_cast< float >( nodes.size() - 1 );

    float t1, t2;
    if ( t + delta <= maxT ) {
        t1 = t;
        t2 = t + delta;
    } else if ( t - delta >= 0.0f ) {
        t1 = t - delta;
        t2 = t;
    } else {
        return { 0.0f, 0.0f, 1.0f };
    }

    Vector3 p1 = EvaluatePosition(t1);
    Vector3 p2 = EvaluatePosition(t2);

    Vector3 tangent = { p2.x - p1.x, p2.y - p1.y, p2.z - p1.z };
    float len = Length(tangent);
    if ( len > 0.0f ){
        tangent.x /= len; tangent.y /= len; tangent.z /= len;
    }
    return tangent;
}

float SplineRail::GetDistanceFromT(float t) const{
    if ( tTable_.empty() ) return 0.0f;
    if ( t <= 0.0f ) return 0.0f;
    if ( t >= tTable_.back() ) return totalLength_;

    for ( size_t i = 0; i < tTable_.size() - 1; ++i ){
        if ( tTable_[i] <= t && t <= tTable_[i + 1] ){
            float segT = tTable_[i + 1] - tTable_[i];
            float ratio = ( segT > 0.0f ) ? ( t - tTable_[i] ) / segT : 0.0f;
            return distanceTable_[i] + ( distanceTable_[i + 1] - distanceTable_[i] ) * ratio;
        }
    }
    return totalLength_;
}