#include "SplineRail.h"
#include "engine/math/VectorMath.h"
#include <cmath>
#include <algorithm>

// 共通の数学関数（Length/Cross/Dot/Lerp/NormalizeSafe）は VectorMath に一本化した
using namespace VectorMath;

// ========================================================
// 追加: 指定したt（進行度）における座標を計算して返す関数
// （Catmull-Rom スプラインを使用して滑らかな曲線を計算します）
// ========================================================
Vector3 SplineRail::EvaluatePosition(float t) const{
    if ( nodes.empty() ) return { 0.0f, 0.0f, 0.0f };
    if ( nodes.size() == 1 ) {
        return { nodes[0].x + animOffset.x, nodes[0].y + animOffset.y, nodes[0].z + animOffset.z };
    }

    // tの整数部分(インデックス)と小数部分(ローカルt)を分ける
    int p1_index = static_cast< int >( t );
    float localT = t - p1_index;

    // 中央2点(p1,p2)のインデックスを安全にクランプ
    int maxIndex = static_cast< int >( nodes.size() - 1 );
    if ( p1_index < 0 ) p1_index = 0;
    if ( p1_index > maxIndex ) p1_index = maxIndex;
    int p2_index = p1_index + 1;
    if ( p2_index > maxIndex ) p2_index = maxIndex;

    Vector3 p1 = nodes[p1_index];
    Vector3 p2 = nodes[p2_index];

    // 直線モード（カクカク）：制御点を使わず p1→p2 を線形補間するだけ。
    //   ノードの位置はスプラインと同じで「つなぎ方」だけが変わる。
    //   距離テーブル・等速移動・接続などの仕組みは共通のままそのまま働く。
    if ( lineMode == 1 ) {
        return { p1.x + ( p2.x - p1.x ) * localT + animOffset.x,
                 p1.y + ( p2.y - p1.y ) * localT + animOffset.y,
                 p1.z + ( p2.z - p1.z ) * localT + animOffset.z };
    }

    // 端の制御点(p0,p3)は配列外なら「鏡映(reflection)」で補う。
    // 従来は端を同じ点にクランプしていたため端の接線が劣化し、向きがカクついた。
    // p0 = p1 を p2 の反対側へ折り返す / p3 = p2 を p1 の反対側へ折り返す
    // ループ（front==backに溶接済み）の場合は反対側のノードを使い、継ぎ目も滑らかな円にする
    Vector3 p0;
    if ( p1_index - 1 >= 0 ) {
        p0 = nodes[p1_index - 1];
    } else if ( isLoop && maxIndex >= 2 ) {
        p0 = nodes[maxIndex - 1]; // 末尾の1つ手前（front==back なので物理的に手前のノード）
    } else {
        p0 = { 2.0f * p1.x - p2.x, 2.0f * p1.y - p2.y, 2.0f * p1.z - p2.z };
    }
    Vector3 p3;
    if ( p1_index + 2 <= maxIndex ) {
        p3 = nodes[p1_index + 2];
    } else if ( isLoop && maxIndex >= 2 ) {
        p3 = nodes[1];            // 先頭の次（front==back なので物理的に次のノード）
    } else {
        p3 = { 2.0f * p2.x - p1.x, 2.0f * p2.y - p1.y, 2.0f * p2.z - p1.z };
    }

    // Catmull-Rom スプラインの公式を使って曲線を計算
    Vector3 result;
    result.x = 0.5f * ( ( 2.0f * p1.x ) + ( -p0.x + p2.x ) * localT + ( 2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x ) * ( localT * localT ) + ( -p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x ) * ( localT * localT * localT ) );
    result.y = 0.5f * ( ( 2.0f * p1.y ) + ( -p0.y + p2.y ) * localT + ( 2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y ) * ( localT * localT ) + ( -p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y ) * ( localT * localT * localT ) );
    result.z = 0.5f * ( ( 2.0f * p1.z ) + ( -p0.z + p2.z ) * localT + ( 2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z ) * ( localT * localT ) + ( -p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z ) * ( localT * localT * localT ) );

    // 動くレール：現在のアニメオフセットを加算（剛体移動なので距離テーブルは不変）
    result.x += animOffset.x;
    result.y += animOffset.y;
    result.z += animOffset.z;

    return result;
}

// front→back の主軸からタイプを自動判定する
//   斜めのレールは「横」扱いを優先する。縦と判定されるには Z の伸びが X の
//   1.5倍以上必要（少し奥に向けただけで操作キーが W/S に切り替わるのを防ぐ）。
//   ※どうしても縦にしたい/横にしたい時はエディタの 横/縦 ボタンで手動指定できる
void SplineRail::AutoDetectType(){
    if ( nodes.size() < 2 ) { type = RailType::Horizontal; return; }
    float deltaX = nodes.back().x - nodes.front().x;
    float deltaZ = nodes.back().z - nodes.front().z;
    const float kVerticalBias = 1.5f; // 縦判定に必要な Z/X 比
    type = ( std::abs(deltaZ) > std::abs(deltaX) * kVerticalBias ) ? RailType::Vertical : RailType::Horizontal;
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
    // 大きいほど弧長(=等速移動)が正確になる。曲がりの強いレールでもブレにくくする
    const int resolution = 40;
    int totalSamples = static_cast< int >( maxT * resolution );
    if ( totalSamples < 1 ) totalSamples = 1;

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

    // 距離テーブルが更新されたらフレームテーブルも作り直す（道システムの土台）
    BuildFrameCache();
}



// FrameCache の構築：0.25m刻みで RMF（平行移動フレーム）を運ぶ。
//   right(0) = normalize(cross(worldUp, T))
//   right(i) = 前の right を現在の接線に直交化（ねじれ最小）
//   接線が水平寄り(|T.y|<0.7)の間は「理想のright（水平）」へ15%/stepだけ戻す（ロール回復）
void SplineRail::BuildFrameCache(){
    frameCache_.clear();
    if ( nodes.size() < 2 || totalLength_ <= 0.0f ) return;

    const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };
    const int count = static_cast< int >( totalLength_ / kFrameStep ) + 2; // 終端を必ず含む
    frameCache_.reserve(count);

    Vector3 prevRight { 1.0f, 0.0f, 0.0f };
    for ( int i = 0; i < count; ++i ) {
        float frameDistance = ( std::min )( i * kFrameStep, totalLength_ );

        RailFrame frame;
        frame.position = GetPositionByDistance(frameDistance);
        frame.tangent  = NormalizeSafe(GetTangentByDistance(frameDistance), { 1.0f, 0.0f, 0.0f });
        const Vector3& T = frame.tangent;

        Vector3 right;
        if ( i == 0 ) {
            right = Cross(worldUp, T);
            if ( Dot(right, right) < 1e-8f ) { right = Cross({ 0.0f, 0.0f, 1.0f }, T); } // 真上向きの保険
            right = NormalizeSafe(right, { 1.0f, 0.0f, 0.0f });
        } else {
            // RMF: 前フレームの right を現在の接線に直交化して運ぶ
            right = { prevRight.x - T.x * Dot(prevRight, T),
                  prevRight.y - T.y * Dot(prevRight, T),
                  prevRight.z - T.z * Dot(prevRight, T) };
            if ( Dot(right, right) < 1e-8f ) {
                right = Cross(worldUp, T);
                if ( Dot(right, right) < 1e-8f ) { right = Cross({ 0.0f, 0.0f, 1.0f }, T); }
            }
            right = NormalizeSafe(right, { 1.0f, 0.0f, 0.0f });

            // ロール回復：接線が水平寄りなら理想の right（水平）へ少しずつ戻す
            if ( std::abs(T.y) < 0.7f ) {
                Vector3 ideal = Cross(worldUp, T);
                if ( Dot(ideal, ideal) > 1e-8f ) {
                    ideal = NormalizeSafe(ideal, { 1.0f, 0.0f, 0.0f });
                    if ( Dot(ideal, right) < 0.0f ) { ideal = { -ideal.x, -ideal.y, -ideal.z }; } // 反転側へ回復しない
                    right = NormalizeSafe(Lerp(right, ideal, 0.15f), { 1.0f, 0.0f, 0.0f });
                }
            }
        }
        prevRight = right;

        frame.right = right;
        frame.up = NormalizeSafe(Cross(T, right), { 1.0f, 0.0f, 0.0f });
        frameCache_.push_back(frame);
    }
}

// 距離 d のフレームを取得（テーブルの線形補間＋再正規化）
SplineRail::RailFrame SplineRail::GetFrameAtDistance(float distance) const{
    if ( frameCache_.size() < 2 ) {
        // フォールバック：キャッシュ未構築時は都度計算（RMFなしの簡易フレーム）
        RailFrame fallbackFrame;
        fallbackFrame.position = GetPositionByDistance(distance);
        fallbackFrame.tangent  = NormalizeSafe(GetTangentByDistance(distance), { 1.0f, 0.0f, 0.0f });
        Vector3 right = Cross({ 0.0f, 1.0f, 0.0f }, fallbackFrame.tangent);
        if ( Dot(right, right) < 1e-8f ) { right = Cross({ 0.0f, 0.0f, 1.0f }, fallbackFrame.tangent); }
        fallbackFrame.right = NormalizeSafe(right, { 1.0f, 0.0f, 0.0f });
        fallbackFrame.up = NormalizeSafe(Cross(fallbackFrame.tangent, fallbackFrame.right), { 1.0f, 0.0f, 0.0f });
        return fallbackFrame;
    }

    float clampedDistance = std::clamp(distance, 0.0f, totalLength_);
    float frameIndexFloat = clampedDistance / kFrameStep;
    int baseIndex = ( std::min )( static_cast< int >( frameIndexFloat ), static_cast< int >( frameCache_.size() ) - 2 );
    float t = std::clamp(frameIndexFloat - baseIndex, 0.0f, 1.0f);

    const RailFrame& frameA = frameCache_[baseIndex];
    const RailFrame& frameB = frameCache_[baseIndex + 1];
    RailFrame frame;
    frame.position = Lerp(frameA.position, frameB.position, t);
    frame.tangent  = NormalizeSafe(Lerp(frameA.tangent, frameB.tangent, t), { 1.0f, 0.0f, 0.0f });
    frame.right    = NormalizeSafe(Lerp(frameA.right, frameB.right, t), { 1.0f, 0.0f, 0.0f });
    frame.up       = NormalizeSafe(Cross(frame.tangent, frame.right), { 1.0f, 0.0f, 0.0f }); // 補間後も直交を保証
    return frame;
}

// ② 進んだ距離から「t」を求める（これが等速移動の要！）
float SplineRail::GetTFromDistance(float targetDistance) const{
    if ( distanceTable_.empty() ) return 0.0f;
    if ( targetDistance <= 0.0f ) return 0.0f;
    if ( targetDistance >= totalLength_ ) return tTable_.back();

    // 二分探索：targetDistance 以上になる最初の位置を探す（テーブルは昇順）
    auto it = std::lower_bound(distanceTable_.begin(), distanceTable_.end(), targetDistance);
    size_t upperIndex = static_cast< size_t >( it - distanceTable_.begin() );
    if ( upperIndex == 0 ) return tTable_.front();
    size_t lowerIndex = upperIndex - 1;

    // 挟んだ2点を線形補間して正確な t を割り出す
    float segmentDist = distanceTable_[upperIndex] - distanceTable_[lowerIndex];
    float ratio = ( segmentDist > 0.0f ) ? ( targetDistance - distanceTable_[lowerIndex] ) / segmentDist : 0.0f;
    return tTable_[lowerIndex] + ( tTable_[upperIndex] - tTable_[lowerIndex] ) * ratio;
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

// ============================================================
// 距離(s)ベースの公開API実装
// ============================================================

// 距離 s における座標（内部で t に変換）
Vector3 SplineRail::GetPositionByDistance(float distance) const{
    if ( distance < 0.0f ) distance = 0.0f;
    if ( distance > totalLength_ ) distance = totalLength_;
    return EvaluatePosition(GetTFromDistance(distance));
}

// 距離 s における進行方向（距離空間で前後をサンプルするので端でも反転しない）
Vector3 SplineRail::GetTangentByDistance(float distance) const{
    const float sampleOffset = 0.1f; // 前後 10cm を見て向きを求める
    float distBehind = distance - sampleOffset;
    float distAhead = distance + sampleOffset;

    if ( isLoop && totalLength_ > sampleOffset * 4.0f ) {
        // ループは継ぎ目をまたいでラップしてサンプル（シームでも向きが滑らか）
        auto wrap = [&](float dist) -> float{
            while ( dist < 0.0f )          dist += totalLength_;
            while ( dist > totalLength_ )  dist -= totalLength_;
            return dist;
            };
        Vector3 posBehind = GetPositionByDistance(wrap(distBehind));
        Vector3 posAhead = GetPositionByDistance(wrap(distAhead));
        Vector3 dir = { posAhead.x - posBehind.x, posAhead.y - posBehind.y, posAhead.z - posBehind.z };
        float len = Length(dir);
        if ( len > 0.0f ) { dir.x /= len; dir.y /= len; dir.z /= len; } else { dir = { 0.0f, 0.0f, 1.0f }; }
        return dir;
    }

    if ( distBehind < 0.0f ) distBehind = 0.0f;
    if ( distAhead > totalLength_ ) distAhead = totalLength_;
    if ( distAhead - distBehind < 1e-5f ) return { 0.0f, 0.0f, 1.0f };

    Vector3 posBehind = GetPositionByDistance(distBehind);
    Vector3 posAhead = GetPositionByDistance(distAhead);
    Vector3 dir = { posAhead.x - posBehind.x, posAhead.y - posBehind.y, posAhead.z - posBehind.z };
    float len = Length(dir);
    if ( len > 0.0f ) { dir.x /= len; dir.y /= len; dir.z /= len; } else { dir = { 0.0f, 0.0f, 1.0f }; }
    return dir;
}

// 指定ワールド座標に最も近いレール上の距離 s（スナップ／分岐検出用）。
//   毎フレーム全レールに対して呼ばれるため「粗く探して近傍だけ細かく」の2段探索で高速化。
//   （全サンプル線形走査だとレール数×サンプル数で毎フレーム効いてくる）
float SplineRail::GetClosestDistance(const Vector3& worldPos) const{
    if ( distanceTable_.empty() ) return 0.0f;
    const int sampleCount = ( int ) distanceTable_.size();

    // 1. 粗い走査：8個おきにサンプルして最も近い大まかな位置を見つける
    const int stride = 8;
    float bestDistSq = 1e30f;
    int   bestIndex = 0;
    for ( int i = 0; i < sampleCount; i += stride ) {
        Vector3 samplePos = EvaluatePosition(tTable_[i]);
        float dx = samplePos.x - worldPos.x, dy = samplePos.y - worldPos.y, dz = samplePos.z - worldPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if ( distSq < bestDistSq ) { bestDistSq = distSq; bestIndex = i; }
    }
    // 末尾も必ず見る（stride の切り捨てで漏れないように）
    {
        Vector3 samplePos = EvaluatePosition(tTable_[sampleCount - 1]);
        float dx = samplePos.x - worldPos.x, dy = samplePos.y - worldPos.y, dz = samplePos.z - worldPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if ( distSq < bestDistSq ) { bestDistSq = distSq; bestIndex = sampleCount - 1; }
    }

    // 2. 細かい走査：粗い最良点の前後 1 ストライドぶんだけ全サンプル確認
    int fineStart = std::max(0, bestIndex - stride);
    int fineEnd = std::min(sampleCount - 1, bestIndex + stride);
    float bestRailDistance = distanceTable_[bestIndex];
    for ( int i = fineStart; i <= fineEnd; ++i ) {
        Vector3 samplePos = EvaluatePosition(tTable_[i]);
        float dx = samplePos.x - worldPos.x, dy = samplePos.y - worldPos.y, dz = samplePos.z - worldPos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if ( distSq < bestDistSq ) { bestDistSq = distSq; bestRailDistance = distanceTable_[i]; }
    }
    return bestRailDistance;
}

// 穴フラグの連続ノード列を距離区間へ変換する（見た目と落下判定の共通ソース）。
//   区間境界は隣接ノードとの中間点。従来の「最も近いノードが穴指定なら穴」判定と
//   全く同じ範囲になる（＝この置き換えでゲームプレイは変わらない）。
std::vector<SplineRail::HoleInterval> SplineRail::GetHoleIntervals() const{
    std::vector<HoleInterval> intervals;
    int nodeCount = static_cast< int >( nodes.size() );
    if ( nodeCount < 2 || nodeHole.empty() ) return intervals;
    int flagCount = std::min(nodeCount, static_cast< int >( nodeHole.size() ));

    // ノード i の弧長距離（t=i に対応）
    auto nodeDist = [&](int i) -> float{ return GetDistanceFromT(static_cast< float >( i )); };

    int runStart = -1;
    for ( int i = 0; i <= flagCount; ++i ) {
        bool hole = ( i < flagCount ) && ( nodeHole[i] != 0 );
        if ( hole && runStart < 0 ) { runStart = i; }
        if ( !hole && runStart >= 0 ) {
            int runEnd = i - 1;
            HoleInterval interval;
            interval.d0 = ( runStart == 0 )      ? 0.0f
                                           : ( nodeDist(runStart - 1) + nodeDist(runStart) ) * 0.5f;
            interval.d1 = ( runEnd == flagCount - 1 && runEnd == nodeCount - 1 ) ? totalLength_
                                           : ( nodeDist(runEnd) + nodeDist(runEnd + 1) ) * 0.5f;
            if ( interval.d1 > interval.d0 ) { intervals.push_back(interval); }
            runStart = -1;
        }
    }
    return intervals;
}

// 指定距離(s)が「穴」区間か：GetHoleIntervals と同じ区間を参照する
bool SplineRail::IsHoleAtDistance(float distance) const{
    for ( const HoleInterval& interval : GetHoleIntervals() ) {
        if ( distance >= interval.d0 && distance <= interval.d1 ) return true;
    }
    return false;
}

float SplineRail::GetDistanceFromT(float t) const{
    if ( tTable_.empty() ) return 0.0f;
    if ( t <= 0.0f ) return 0.0f;
    if ( t >= tTable_.back() ) return totalLength_;

    // 二分探索：t 以上になる最初の位置を探す
    auto it = std::lower_bound(tTable_.begin(), tTable_.end(), t);
    size_t upperIndex = static_cast< size_t >( it - tTable_.begin() );
    if ( upperIndex == 0 ) return distanceTable_.front();
    size_t lowerIndex = upperIndex - 1;

    float segmentT = tTable_[upperIndex] - tTable_[lowerIndex];
    float ratio = ( segmentT > 0.0f ) ? ( t - tTable_[lowerIndex] ) / segmentT : 0.0f;
    return distanceTable_[lowerIndex] + ( distanceTable_[upperIndex] - distanceTable_[lowerIndex] ) * ratio;
}