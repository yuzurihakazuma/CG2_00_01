#include "SplineRail.h"
#include <cmath>
#include <algorithm>

// --- ベクトルの長さを求める補助関数 ---
inline float Length(const Vector3& v){
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

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
    float dx = nodes.back().x - nodes.front().x;
    float dz = nodes.back().z - nodes.front().z;
    const float kVerticalBias = 1.5f; // 縦判定に必要な Z/X 比
    type = ( std::abs(dz) > std::abs(dx) * kVerticalBias ) ? RailType::Vertical : RailType::Horizontal;
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

namespace {
    // FrameCache 用のベクトル補助（このファイル内だけで使う）
    inline Vector3 FC_Cross(const Vector3& a, const Vector3& b){
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    inline float FC_Dot(const Vector3& a, const Vector3& b){
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline Vector3 FC_Normalize(const Vector3& v){
        float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if ( l < 1e-6f ) return { 1.0f, 0.0f, 0.0f };
        return { v.x / l, v.y / l, v.z / l };
    }
    inline Vector3 FC_Lerp(const Vector3& a, const Vector3& b, float t){
        return { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t };
    }
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
        float d = ( std::min )( i * kFrameStep, totalLength_ );

        RailFrame f;
        f.position = GetPositionByDistance(d);
        f.tangent  = FC_Normalize(GetTangentByDistance(d));
        const Vector3& T = f.tangent;

        Vector3 r;
        if ( i == 0 ) {
            r = FC_Cross(worldUp, T);
            if ( FC_Dot(r, r) < 1e-8f ) { r = FC_Cross({ 0.0f, 0.0f, 1.0f }, T); } // 真上向きの保険
            r = FC_Normalize(r);
        } else {
            // RMF: 前フレームの right を現在の接線に直交化して運ぶ
            r = { prevRight.x - T.x * FC_Dot(prevRight, T),
                  prevRight.y - T.y * FC_Dot(prevRight, T),
                  prevRight.z - T.z * FC_Dot(prevRight, T) };
            if ( FC_Dot(r, r) < 1e-8f ) {
                r = FC_Cross(worldUp, T);
                if ( FC_Dot(r, r) < 1e-8f ) { r = FC_Cross({ 0.0f, 0.0f, 1.0f }, T); }
            }
            r = FC_Normalize(r);

            // ロール回復：接線が水平寄りなら理想の right（水平）へ少しずつ戻す
            if ( std::abs(T.y) < 0.7f ) {
                Vector3 ideal = FC_Cross(worldUp, T);
                if ( FC_Dot(ideal, ideal) > 1e-8f ) {
                    ideal = FC_Normalize(ideal);
                    if ( FC_Dot(ideal, r) < 0.0f ) { ideal = { -ideal.x, -ideal.y, -ideal.z }; } // 反転側へ回復しない
                    r = FC_Normalize(FC_Lerp(r, ideal, 0.15f));
                }
            }
        }
        prevRight = r;

        f.right = r;
        f.up = FC_Normalize(FC_Cross(T, r));
        frameCache_.push_back(f);
    }
}

// 距離 d のフレームを取得（テーブルの線形補間＋再正規化）
SplineRail::RailFrame SplineRail::GetFrameAtDistance(float distance) const{
    if ( frameCache_.size() < 2 ) {
        // フォールバック：キャッシュ未構築時は都度計算（RMFなしの簡易フレーム）
        RailFrame f;
        f.position = GetPositionByDistance(distance);
        f.tangent  = FC_Normalize(GetTangentByDistance(distance));
        Vector3 r = FC_Cross({ 0.0f, 1.0f, 0.0f }, f.tangent);
        if ( FC_Dot(r, r) < 1e-8f ) { r = FC_Cross({ 0.0f, 0.0f, 1.0f }, f.tangent); }
        f.right = FC_Normalize(r);
        f.up = FC_Normalize(FC_Cross(f.tangent, f.right));
        return f;
    }

    float d = std::clamp(distance, 0.0f, totalLength_);
    float fi = d / kFrameStep;
    int i0 = ( std::min )( static_cast< int >( fi ), static_cast< int >( frameCache_.size() ) - 2 );
    float t = std::clamp(fi - i0, 0.0f, 1.0f);

    const RailFrame& a = frameCache_[i0];
    const RailFrame& b = frameCache_[i0 + 1];
    RailFrame f;
    f.position = FC_Lerp(a.position, b.position, t);
    f.tangent  = FC_Normalize(FC_Lerp(a.tangent, b.tangent, t));
    f.right    = FC_Normalize(FC_Lerp(a.right, b.right, t));
    f.up       = FC_Normalize(FC_Cross(f.tangent, f.right)); // 補間後も直交を保証
    return f;
}

// ② 進んだ距離から「t」を求める（これが等速移動の要！）
float SplineRail::GetTFromDistance(float targetDistance) const{
    if ( distanceTable_.empty() ) return 0.0f;
    if ( targetDistance <= 0.0f ) return 0.0f;
    if ( targetDistance >= totalLength_ ) return tTable_.back();

    // 二分探索：targetDistance 以上になる最初の位置を探す（テーブルは昇順）
    auto it = std::lower_bound(distanceTable_.begin(), distanceTable_.end(), targetDistance);
    size_t hi = static_cast< size_t >( it - distanceTable_.begin() );
    if ( hi == 0 ) return tTable_.front();
    size_t lo = hi - 1;

    // 挟んだ2点を線形補間して正確な t を割り出す
    float segmentDist = distanceTable_[hi] - distanceTable_[lo];
    float ratio = ( segmentDist > 0.0f ) ? ( targetDistance - distanceTable_[lo] ) / segmentDist : 0.0f;
    return tTable_[lo] + ( tTable_[hi] - tTable_[lo] ) * ratio;
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
    const float ds = 0.1f; // 前後 10cm を見て向きを求める
    float s1 = distance - ds;
    float s2 = distance + ds;

    if ( isLoop && totalLength_ > ds * 4.0f ) {
        // ループは継ぎ目をまたいでラップしてサンプル（シームでも向きが滑らか）
        auto wrap = [&](float s) -> float{
            while ( s < 0.0f )          s += totalLength_;
            while ( s > totalLength_ )  s -= totalLength_;
            return s;
            };
        Vector3 a = GetPositionByDistance(wrap(s1));
        Vector3 b = GetPositionByDistance(wrap(s2));
        Vector3 dir = { b.x - a.x, b.y - a.y, b.z - a.z };
        float len = Length(dir);
        if ( len > 0.0f ) { dir.x /= len; dir.y /= len; dir.z /= len; } else { dir = { 0.0f, 0.0f, 1.0f }; }
        return dir;
    }

    if ( s1 < 0.0f ) s1 = 0.0f;
    if ( s2 > totalLength_ ) s2 = totalLength_;
    if ( s2 - s1 < 1e-5f ) return { 0.0f, 0.0f, 1.0f };

    Vector3 a = GetPositionByDistance(s1);
    Vector3 b = GetPositionByDistance(s2);
    Vector3 dir = { b.x - a.x, b.y - a.y, b.z - a.z };
    float len = Length(dir);
    if ( len > 0.0f ) { dir.x /= len; dir.y /= len; dir.z /= len; } else { dir = { 0.0f, 0.0f, 1.0f }; }
    return dir;
}

// 指定ワールド座標に最も近いレール上の距離 s（スナップ／分岐検出用）。
//   毎フレーム全レールに対して呼ばれるため「粗く探して近傍だけ細かく」の2段探索で高速化。
//   （全サンプル線形走査だとレール数×サンプル数で毎フレーム効いてくる）
float SplineRail::GetClosestDistance(const Vector3& worldPos) const{
    if ( distanceTable_.empty() ) return 0.0f;
    const int n = ( int ) distanceTable_.size();

    // 1. 粗い走査：8個おきにサンプルして最も近い大まかな位置を見つける
    const int stride = 8;
    float bestDistSq = 1e30f;
    int   bestI = 0;
    for ( int i = 0; i < n; i += stride ) {
        Vector3 p = EvaluatePosition(tTable_[i]);
        float dx = p.x - worldPos.x, dy = p.y - worldPos.y, dz = p.z - worldPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if ( d2 < bestDistSq ) { bestDistSq = d2; bestI = i; }
    }
    // 末尾も必ず見る（stride の切り捨てで漏れないように）
    {
        Vector3 p = EvaluatePosition(tTable_[n - 1]);
        float dx = p.x - worldPos.x, dy = p.y - worldPos.y, dz = p.z - worldPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if ( d2 < bestDistSq ) { bestDistSq = d2; bestI = n - 1; }
    }

    // 2. 細かい走査：粗い最良点の前後 1 ストライドぶんだけ全サンプル確認
    int lo = std::max(0, bestI - stride);
    int hi = std::min(n - 1, bestI + stride);
    float bestS = distanceTable_[bestI];
    for ( int i = lo; i <= hi; ++i ) {
        Vector3 p = EvaluatePosition(tTable_[i]);
        float dx = p.x - worldPos.x, dy = p.y - worldPos.y, dz = p.z - worldPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if ( d2 < bestDistSq ) { bestDistSq = d2; bestS = distanceTable_[i]; }
    }
    return bestS;
}

// 穴フラグの連続ノード列を距離区間へ変換する（見た目と落下判定の共通ソース）。
//   区間境界は隣接ノードとの中間点。従来の「最も近いノードが穴指定なら穴」判定と
//   全く同じ範囲になる（＝この置き換えでゲームプレイは変わらない）。
std::vector<SplineRail::HoleInterval> SplineRail::GetHoleIntervals() const{
    std::vector<HoleInterval> out;
    int n = static_cast< int >( nodes.size() );
    if ( n < 2 || nodeHole.empty() ) return out;
    int lim = std::min(n, static_cast< int >( nodeHole.size() ));

    // ノード i の弧長距離（t=i に対応）
    auto nodeDist = [&](int i) -> float{ return GetDistanceFromT(static_cast< float >( i )); };

    int runStart = -1;
    for ( int i = 0; i <= lim; ++i ) {
        bool hole = ( i < lim ) && ( nodeHole[i] != 0 );
        if ( hole && runStart < 0 ) { runStart = i; }
        if ( !hole && runStart >= 0 ) {
            int runEnd = i - 1;
            HoleInterval hv;
            hv.d0 = ( runStart == 0 )      ? 0.0f
                                           : ( nodeDist(runStart - 1) + nodeDist(runStart) ) * 0.5f;
            hv.d1 = ( runEnd == lim - 1 && runEnd == n - 1 ) ? totalLength_
                                           : ( nodeDist(runEnd) + nodeDist(runEnd + 1) ) * 0.5f;
            if ( hv.d1 > hv.d0 ) { out.push_back(hv); }
            runStart = -1;
        }
    }
    return out;
}

// 指定距離(s)が「穴」区間か：GetHoleIntervals と同じ区間を参照する
bool SplineRail::IsHoleAtDistance(float distance) const{
    for ( const HoleInterval& hv : GetHoleIntervals() ) {
        if ( distance >= hv.d0 && distance <= hv.d1 ) return true;
    }
    return false;
}

float SplineRail::GetDistanceFromT(float t) const{
    if ( tTable_.empty() ) return 0.0f;
    if ( t <= 0.0f ) return 0.0f;
    if ( t >= tTable_.back() ) return totalLength_;

    // 二分探索：t 以上になる最初の位置を探す
    auto it = std::lower_bound(tTable_.begin(), tTable_.end(), t);
    size_t hi = static_cast< size_t >( it - tTable_.begin() );
    if ( hi == 0 ) return distanceTable_.front();
    size_t lo = hi - 1;

    float segT = tTable_[hi] - tTable_[lo];
    float ratio = ( segT > 0.0f ) ? ( t - tTable_[lo] ) / segT : 0.0f;
    return distanceTable_[lo] + ( distanceTable_[hi] - distanceTable_[lo] ) * ratio;
}