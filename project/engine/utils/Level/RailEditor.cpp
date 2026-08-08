#include "RailEditor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>
#include <unordered_map>

#include "engine/utils/ImGuiManager.h"
#include "engine/base/Input.h"
#include "engine/rail/SplineRail.h" // ブロックのノード錨の更新（弧長の計測）に使う

static bool RailLinesEqual(const std::vector<std::vector<Vector3>>& lhs,
                           const std::vector<std::vector<Vector3>>& rhs){
	if ( lhs.size() != rhs.size() ) return false;
	for ( size_t i = 0; i < lhs.size(); ++i ) {
		if ( lhs[i].size() != rhs[i].size() ) return false;
		for ( size_t j = 0; j < lhs[i].size(); ++j ) {
			if ( lhs[i][j].x != rhs[i][j].x || lhs[i][j].y != rhs[i][j].y || lhs[i][j].z != rhs[i][j].z ) return false;
		}
	}
	return true;
}

// 動くレール設定(Vector4)の一致判定（Vector4 に operator== が無いので手動）
static bool MotionsEqual(const std::vector<Vector4>& lhs, const std::vector<Vector4>& rhs){
	if ( lhs.size() != rhs.size() ) return false;
	for ( size_t i = 0; i < lhs.size(); ++i ) {
		if ( lhs[i].x != rhs[i].x || lhs[i].y != rhs[i].y || lhs[i].z != rhs[i].z || lhs[i].w != rhs[i].w ) return false;
	}
	return true;
}

// 折れ線を Catmull-Rom 補間でなめらかにする（手描きキャンバス・なめらか化ボタンで共用）。
//   divisions: 区間あたりの分割数（多いほど滑らか）。点が3個未満ならそのまま返す。
static std::vector<Vector3> SmoothPolylineCR(const std::vector<Vector3>& points, int divisions){
	const int pointCount = static_cast< int >( points.size() );
	if ( pointCount < 3 || divisions < 2 ) return points;
	std::vector<Vector3> smoothed;
	smoothed.reserve(static_cast< size_t >( pointCount ) * divisions + 1);
	auto pointAt = [&]( int i ) -> const Vector3&{ return points[std::clamp(i, 0, pointCount - 1)]; };
	for ( int i = 0; i + 1 < pointCount; ++i ) {
		const Vector3& p0 = pointAt(i - 1);
		const Vector3& p1 = pointAt(i);
		const Vector3& p2 = pointAt(i + 1);
		const Vector3& p3 = pointAt(i + 2);
		for ( int step = 0; step < divisions; ++step ) {
			float t  = static_cast< float >( step ) / static_cast< float >( divisions );
			float t2 = t * t, t3 = t2 * t;
			Vector3 interpolated;
			interpolated.x = 0.5f * ( ( 2.0f * p1.x ) + ( -p0.x + p2.x ) * t + ( 2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x ) * t2 + ( -p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x ) * t3 );
			interpolated.y = 0.5f * ( ( 2.0f * p1.y ) + ( -p0.y + p2.y ) * t + ( 2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y ) * t2 + ( -p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y ) * t3 );
			interpolated.z = 0.5f * ( ( 2.0f * p1.z ) + ( -p0.z + p2.z ) * t + ( 2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z ) * t2 + ( -p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z ) * t3 );
			smoothed.push_back(interpolated);
		}
	}
	smoothed.push_back(points.back());
	return smoothed;
}

// マップ読込/差し替え時：選択・履歴をリセットし、空なら1本用意する
void RailEditor::OnMapChanged(){
    if ( data_->railLines.empty() ) {
        data_->railLines.push_back(std::vector<Vector3>());
    }
    currentEditRailIndex_ = 0;
    selectedRailNode_ = -1;
    multiSelection_.clear();
    pendingStamp_.clear();
    undoStack_.clear();
    redoStack_.clear();
    committedInit_ = false;

    // types/motions を lines と同数に揃えてから「最初期の状態」を記録する
    if ( data_->railTypes.size() != data_->railLines.size() ) {
        data_->railTypes.resize(data_->railLines.size(), -1);
    }
    if ( data_->railMotions.size() != data_->railLines.size() ) {
        data_->railMotions.resize(data_->railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
    }
    if ( data_->railGroundTypes.size() != data_->railLines.size() ) {
        data_->railGroundTypes.resize(data_->railLines.size(), 1); // 既定は Gap
    }
    // 追加分の並列配列も lines と同数に揃える
    data_->railMotionTypes.resize(data_->railLines.size(), 0);
    data_->railMotionPhases.resize(data_->railLines.size(), 0.0f);
    data_->railOneWay.resize(data_->railLines.size(), 0);
    data_->railSpeedMuls.resize(data_->railLines.size(), 1.0f);
    initialLines_   = data_->railLines;
    initialTypes_   = data_->railTypes;
    initialMotions_ = data_->railMotions;
    initialCoins_   = data_->coins;
    initialBlocks_  = data_->blocks;
    hasInitial_     = true;
    ++blockVersion_; // マップ差し替え時もゲーム側にブロック作り直しを通知

    RebuildRailPoints();
}

// 編集の世代番号だけを進めて変化を通知する（緑線の描画はゲーム側が担当）
void RailEditor::RebuildRailPoints(){
    ++railVersion_;
}

// --- マウス編集サポート ---
int RailEditor::GetCurrentRailNodeCount() const{
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return 0;
	return ( int ) data_->railLines[currentEditRailIndex_].size();
}
bool RailEditor::GetRailNodePos(int idx, Vector3& out) const{
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return false;
	const auto& line = data_->railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return false;
	out = line[idx];
	return true;
}
void RailEditor::SetRailNodePos(int idx, const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return;
	line[idx] = ApplyNodeSnap(p); // 他レールの端点が近ければ吸着
	++railVersion_; // 緑線・ゲーム側へライブ反映
}
float RailEditor::SnapValue(float v) const{
	if ( !railSnap_ || railGridSize_ <= 0.0f ) return v;
	return std::round(v / railGridSize_) * railGridSize_;
}

// 直角ロック＋グリッド＋ノード吸着を適用した「実際に置かれる位置」を返す（プレビューと共用）
Vector3 RailEditor::ComputePlacement(const Vector3& raw) const{
	Vector3 pos = raw;

	if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) data_->railLines.size() ) {
		const auto& line = data_->railLines[currentEditRailIndex_];
		// 直角モード：前ノードから X か Z のどちらか（差が大きい軸）だけ動かす
		if ( railAxisLock_ && !line.empty() ) {
			const Vector3& prev = line.back();
			float dx = pos.x - prev.x;
			float dz = pos.z - prev.z;
			if ( std::abs(dx) >= std::abs(dz) ) pos.z = prev.z; // X方向に固定
			else                                pos.x = prev.x; // Z方向に固定
		}
	}

	// グリッド吸着（X,Z。Y は配置高さのまま）
	pos.x = SnapValue(pos.x);
	pos.z = SnapValue(pos.z);

	pos = ApplyNodeSnap(pos); // 他レールの端点が近ければ吸着（接続をピッタリ）
	return pos;
}

void RailEditor::AppendRailNodeAt(const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];

	line.push_back(ComputePlacement(p));
	// 穴配列も末尾に同期（ズレ防止）
	if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
		data_->railNodeHoles[currentEditRailIndex_].resize(line.size(), 0);
	}
	selectedRailNode_ = ( int ) line.size() - 1; // 追加した点を選択状態に
	multiSelection_.clear();
	multiSelection_.push_back({ currentEditRailIndex_, selectedRailNode_ });
	++railVersion_;
}

// 選択中レールの端を「延長線上」に1個伸ばす（front=true で先頭側）。
//   引き終わったレールへ後からノードを足すためのボタン用。
void RailEditor::ExtendRailNode(bool front){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];

	// 追加位置：端の2点の延長線上に同じ間隔で置く（1点以下なら適当な距離だけ離す）
	Vector3 p;
	if ( line.empty() ) {
		p = { 0.0f, railDrawHeight_, 0.0f };
	} else if ( line.size() == 1 ) {
		p = line.front();
		p.z += front ? -3.0f : 3.0f;
	} else if ( front ) {
		const Vector3& a = line[0];
		const Vector3& b = line[1];
		p = { a.x * 2.0f - b.x, a.y * 2.0f - b.y, a.z * 2.0f - b.z };
	} else {
		const Vector3& a = line[line.size() - 1];
		const Vector3& b = line[line.size() - 2];
		p = { a.x * 2.0f - b.x, a.y * 2.0f - b.y, a.z * 2.0f - b.z };
	}
	p.x = SnapValue(p.x);
	p.y = SnapValue(p.y);
	p.z = SnapValue(p.z);

	// 穴配列を現ノード数に揃えてから、同じ側へ同期挿入（ズレ防止）
	if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
		auto& h = data_->railNodeHoles[currentEditRailIndex_];
		h.resize(line.size(), 0);
		if ( front ) h.insert(h.begin(), 0);
		else         h.push_back(0);
	}

	if ( front ) { line.insert(line.begin(), p); selectedRailNode_ = 0; }
	else         { line.push_back(p);            selectedRailNode_ = ( int ) line.size() - 1; }
	multiSelection_.clear();
	multiSelection_.push_back({ currentEditRailIndex_, selectedRailNode_ });
	++railVersion_;
}

// 方向ボタン用：前ノード（無ければ配置高さの原点）から相対移動して追加
void RailEditor::AppendRailNodeRelative(float dx, float dy, float dz){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];

	Vector3 base = line.empty() ? Vector3{ 0.0f, railDrawHeight_, 0.0f } : line.back();
	Vector3 pos = { base.x + dx, base.y + dy, base.z + dz };

	// デルタは既に軸方向・グリッド倍数なので吸着は保険程度
	pos.x = SnapValue(pos.x);
	pos.y = SnapValue(pos.y);
	pos.z = SnapValue(pos.z);

	pos = ApplyNodeSnap(pos); // 他レールの端点が近ければ吸着

	line.push_back(pos);
	selectedRailNode_ = ( int ) line.size() - 1;
	++railVersion_;
}

// 他レールの近い端点へ吸着（無ければ p をそのまま返す）
Vector3 RailEditor::ApplyNodeSnap(const Vector3& p) const{
	if ( !railNodeSnap_ ) return p;
	float bestSq = railNodeSnapRadius_ * railNodeSnapRadius_;
	Vector3 best = p;
	bool found = false;
	for ( int r = 0; r < ( int ) data_->railLines.size(); ++r ) {
		if ( r == currentEditRailIndex_ ) continue; // 自分のレール内には吸着しない（潰れ防止）
		for ( const auto& n : data_->railLines[r] ) {
			float dx = n.x - p.x, dy = n.y - p.y, dz = n.z - p.z;
			float d2 = dx * dx + dy * dy + dz * dz;
			if ( d2 < bestSq ) { bestSq = d2; best = n; found = true; }
		}
	}
	return found ? best : p;
}

// ============================================================
// 通行判定（プレイヤーがスタートから辿り着けるか）＋ジャンプ予測
//   距離のしきい値・ジャンプ物理は全てゲーム側（Player / RailField）と同じ値。
//   ゲーム側を調整したらここも合わせること（各定数に出典コメントあり）。
// ============================================================
namespace{

// 点p と線分ab の最短距離の2乗（t をクランプした最近点方式）
float SegDistSq(const Vector3& p, const Vector3& a, const Vector3& b){
	Vector3 ab { b.x - a.x, b.y - a.y, b.z - a.z };
	float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
	float t = ( len2 > 1e-6f )
		? ( ( p.x - a.x ) * ab.x + ( p.y - a.y ) * ab.y + ( p.z - a.z ) * ab.z ) / len2
		: 0.0f;
	t = std::clamp(t, 0.0f, 1.0f);
	float dx = a.x + ab.x * t - p.x;
	float dy = a.y + ab.y * t - p.y;
	float dz = a.z + ab.z * t - p.z;
	return dx * dx + dy * dy + dz * dz;
}

// 点p と折れ線line の最短距離の2乗
float PolylineDistSq(const Vector3& p, const std::vector<Vector3>& line){
	float best = 1e30f;
	for ( size_t i = 0; i + 1 < line.size(); ++i ) {
		float d = SegDistSq(p, line[i], line[i + 1]);
		if ( d < best ) best = d;
	}
	return best;
}

// 点p に最も近い折れ線line 上の点。segIdx には最近区間の番号（先頭ノード側）が入る
Vector3 ClosestOnPolyline(const Vector3& p, const std::vector<Vector3>& line, int* segIdx){
	Vector3 best = line.empty() ? Vector3 { 0.0f, 0.0f, 0.0f } : line[0];
	float bestSq = 1e30f;
	if ( segIdx ) *segIdx = 0;
	for ( size_t i = 0; i + 1 < line.size(); ++i ) {
		const Vector3& a = line[i];
		const Vector3& b = line[i + 1];
		Vector3 ab { b.x - a.x, b.y - a.y, b.z - a.z };
		float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
		float t = ( len2 > 1e-6f )
			? ( ( p.x - a.x ) * ab.x + ( p.y - a.y ) * ab.y + ( p.z - a.z ) * ab.z ) / len2
			: 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		Vector3 c { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t };
		float dx = c.x - p.x, dy = c.y - p.y, dz = c.z - p.z;
		float d2 = dx * dx + dy * dy + dz * dz;
		if ( d2 < bestSq ) { bestSq = d2; best = c; if ( segIdx ) *segIdx = ( int ) i; }
	}
	return best;
}

} // namespace

// レール a,b が接続しているか。ゲームでプレイヤーが実際に渡れる距離で判定する。
bool RailEditor::AreRailsLinked(int a, int b) const{
	const float kJoin   = 1.2f; // Player.cpp TryJoinNearbyBody の kJoinReach（溶接0.7mもこの範囲に含む）
	const float kSwitch = 0.9f; // Player.cpp TrySwitchRail の kReach

	const auto& la = data_->railLines[a];
	const auto& lb = data_->railLines[b];
	if ( la.size() < 2 || lb.size() < 2 ) return false;

	// (1) 端から相手の本体へ 1.2m 以内なら合流できる
	if ( PolylineDistSq(la.front(), lb) < kJoin * kJoin ) return true;
	if ( PolylineDistSq(la.back(),  lb) < kJoin * kJoin ) return true;
	if ( PolylineDistSq(lb.front(), la) < kJoin * kJoin ) return true;
	if ( PolylineDistSq(lb.back(),  la) < kJoin * kJoin ) return true;

	// (2) 別タイプ同士なら本体の交差でも乗り換えできる（0.9m）
	if ( GetRailDisplayType(a) != GetRailDisplayType(b) ) {
		for ( const auto& n : la ) {
			if ( PolylineDistSq(n, lb) < kSwitch * kSwitch ) return true;
		}
	}
	return false;
}

// 端が他レールへ接続しているか（1.2m 以内に相手の本体がある＝ゲームで合流できる）
bool RailEditor::IsRailEndConnected(int rail, bool front) const{
	const float kJoin = 1.2f; // Player.cpp TryJoinNearbyBody の kJoinReach
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return false;
	const auto& line = data_->railLines[rail];
	if ( line.size() < 2 ) return false;

	// ループ（先頭と末尾がくっついている）なら端は無い扱い（RailField の isLoop 判定と同じ0.7m）
	if ( line.size() >= 3 ) {
		float dx = line.front().x - line.back().x;
		float dy = line.front().y - line.back().y;
		float dz = line.front().z - line.back().z;
		if ( dx * dx + dy * dy + dz * dz < 0.7f * 0.7f ) return true;
	}

	const Vector3& p = front ? line.front() : line.back();
	for ( int j = 0; j < ( int ) data_->railLines.size(); ++j ) {
		if ( j == rail ) continue;
		const auto& lj = data_->railLines[j];
		if ( lj.size() < 2 ) continue;
		if ( PolylineDistSq(p, lj) < kJoin * kJoin ) return true;
	}
	return false;
}

// レール末端からのジャンプ弾道を予測し、着地できるレール番号を返す（-1=届かない）。
//   「端で必ずジャンプし、頂点からふんばりを全部使う」ベストケースの予測。
int RailEditor::PredictJumpLanding(int rail, bool front, bool useFlutter,
                                   std::vector<Vector3>* outArc) const{
	// --- Player と同じ物理値（出典コメント参照）。調整項目でゲーム側を変えたらここも直す ---
	const float kMoveSpeed   = 5.0f;  // Player.h moveSpeed_
	const float kJumpPower   = 8.0f;  // Player.h jumpPower_
	const float kGravity     = 25.0f; // Player.h gravity_
	const float kLandXZ      = 0.8f;  // Player.cpp UpdateAir の kLandXZ（着地の横許容）
	const float kFloatTime   = 0.6f;  // Player.cpp のふんばり定数3種
	const float kFloatTarget = 1.2f;
	const float kFloatEase   = 6.0f;

	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return -1;
	const auto& line = data_->railLines[rail];
	if ( line.size() < 2 ) return -1;

	// 端から「出て行く」水平方向（front端なら node1→node0 の向き）
	Vector3 a = front ? line.front() : line.back();
	Vector3 b = front ? line[1]      : line[line.size() - 2];
	float dx = a.x - b.x, dz = a.z - b.z;
	float dl = std::sqrt(dx * dx + dz * dz);
	if ( dl < 1e-4f ) return -1;
	dx /= dl; dz /= dl;

	Vector3 pos = a;
	float vy     = kJumpPower;
	float budget = kFloatTime;
	const float dt = 1.0f / 60.0f;

	for ( int step = 0; step < 180; ++step ) { // 最大3秒ぶんシミュレート
		// ふんばり：上昇が尽きたら SPACE 長押しで滞空した想定（Player と同じ式）
		if ( useFlutter && vy < kFloatTarget && budget > 0.0f ) {
			vy += ( kFloatTarget - vy ) * ( std::min )( kFloatEase * dt, 1.0f );
			budget -= dt;
		} else {
			vy -= kGravity * dt;
		}
		float prevY = pos.y;
		pos.x += dx * kMoveSpeed * dt;
		pos.z += dz * kMoveSpeed * dt;
		pos.y += vy * dt;
		if ( outArc ) outArc->push_back(pos);

		if ( vy > 0.0f ) continue; // 上昇中は着地しない（ゲームと同じ）

		for ( int j = 0; j < ( int ) data_->railLines.size(); ++j ) {
			if ( j == rail ) continue;          // 元レールへ戻る着地は「渡り」ではない
			if ( !IsRailVisible(j) ) continue;  // 見えないレールには着地できない（Player と同じ）
			const auto& lj = data_->railLines[j];
			if ( lj.size() < 2 ) continue;

			int seg = 0;
			Vector3 cp = ClosestOnPolyline(pos, lj, &seg);
			if ( IsNodeHole(j, seg) || IsNodeHole(j, seg + 1) ) continue; // 穴区間には着地しない
			float hx = cp.x - pos.x, hz = cp.z - pos.z;
			if ( std::sqrt(hx * hx + hz * hz) > kLandXZ ) continue;
			// レール面のすぐ近くへ降りた or 1フレームで面を上→下へ通過（Player と同条件）
			float above   = pos.y - cp.y;
			bool  reached = ( above <= 0.1f && above >= -0.3f );
			bool  crossed = ( prevY >= cp.y && pos.y <= cp.y );
			if ( reached || crossed ) return j;
		}
		if ( pos.y < a.y - 30.0f ) break; // 落ちすぎ＝どこにも届かない
	}
	return -1;
}

// ============================================================
// 接続情報キャッシュ：どのレールと 溶接/T字/交差 していて座標はどこか。
//   railVersion_ が変わった時だけ全ペア走査で作り直す（表示専用の派生データ）
// ============================================================
void RailEditor::EnsureConnectionCache() const{
	if ( connCacheVersion_ == railVersion_ ) return;
	connCacheVersion_ = railVersion_;

	const auto& lines = data_->railLines;
	const int railCount = ( int ) lines.size();
	connCache_.assign(railCount, {});

	auto dist3 = [](const Vector3& p, const Vector3& q) -> float{
		float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
		};
	// 点 point から線分 segA-segB への最近点
	auto closestOnSegment = [](const Vector3& point, const Vector3& segA, const Vector3& segB) -> Vector3{
		Vector3 seg = { segB.x - segA.x, segB.y - segA.y, segB.z - segA.z };
		float lenSq = seg.x * seg.x + seg.y * seg.y + seg.z * seg.z;
		float t = ( lenSq > 1e-6f )
			? ( ( point.x - segA.x ) * seg.x + ( point.y - segA.y ) * seg.y + ( point.z - segA.z ) * seg.z ) / lenSq
			: 0.0f;
		t = std::clamp(t, 0.0f, 1.0f);
		return { segA.x + seg.x * t, segA.y + seg.y * t, segA.z + seg.z * t };
		};
	// 既に同じ相手との接続が near にあるか（溶接とT字/交差の二重登録防止）
	auto alreadyNear = [&](int rail, int other, const Vector3& pos) -> bool{
		for ( const auto& info : connCache_[rail] ) {
			if ( info.otherRail == other && dist3(info.pos, pos) < 1.5f ) return true;
		}
		return false;
		};
	auto addBoth = [&](int railA, int railB, int type, const Vector3& pos){
		if ( !alreadyNear(railA, railB, pos) ) connCache_[railA].push_back({ type, railB, pos });
		if ( !alreadyNear(railB, railA, pos) ) connCache_[railB].push_back({ type, railA, pos });
		};

	const float kWeldDist  = 0.7f; // 実行時の溶接/連結判定と同じ
	const float kCrossDist = 0.9f; // 実行時の交差乗り換え判定と同じ

	for ( int a = 0; a < railCount; ++a ) {
		if ( lines[a].size() < 2 ) continue;
		for ( int b = a + 1; b < railCount; ++b ) {
			if ( lines[b].size() < 2 ) continue;

			// --- 溶接（端点同士）---
			const Vector3* endsA[2] = { &lines[a].front(), &lines[a].back() };
			const Vector3* endsB[2] = { &lines[b].front(), &lines[b].back() };
			for ( const Vector3* endA : endsA ) {
				for ( const Vector3* endB : endsB ) {
					if ( dist3(*endA, *endB) < kWeldDist ) {
						Vector3 mid = { ( endA->x + endB->x ) * 0.5f, ( endA->y + endB->y ) * 0.5f,
						                ( endA->z + endB->z ) * 0.5f };
						addBoth(a, b, 0, mid);
					}
				}
			}

			// --- T字（片方の端点 → 相手の本体）---
			auto checkTJoin = [&](int endRail, int bodyRail){
				const Vector3 ends[2] = { lines[endRail].front(), lines[endRail].back() };
				for ( const Vector3& endPos : ends ) {
					for ( size_t seg = 0; seg + 1 < lines[bodyRail].size(); ++seg ) {
						Vector3 onBody = closestOnSegment(endPos, lines[bodyRail][seg], lines[bodyRail][seg + 1]);
						if ( dist3(onBody, endPos) < kWeldDist ) {
							addBoth(endRail, bodyRail, 1, onBody);
							break;
						}
					}
				}
				};
			checkTJoin(a, b);
			checkTJoin(b, a);

			// --- 交差（本体×本体。端の近くは溶接/T字の領分なので除外）---
			for ( size_t segA = 0; segA + 1 < lines[a].size(); ++segA ) {
				for ( size_t segB = 0; segB + 1 < lines[b].size(); ++segB ) {
					// 線分Aの中点から線分Bへの最近点で近さを判定（表示用の近似で十分）
					Vector3 midA = { ( lines[a][segA].x + lines[a][segA + 1].x ) * 0.5f,
					                 ( lines[a][segA].y + lines[a][segA + 1].y ) * 0.5f,
					                 ( lines[a][segA].z + lines[a][segA + 1].z ) * 0.5f };
					Vector3 onB = closestOnSegment(midA, lines[b][segB], lines[b][segB + 1]);
					if ( dist3(onB, midA) >= kCrossDist ) continue;
					Vector3 crossPos = { ( midA.x + onB.x ) * 0.5f, ( midA.y + onB.y ) * 0.5f,
					                     ( midA.z + onB.z ) * 0.5f };
					// 端点の近くは溶接/T字として登録済みのはず（alreadyNear が二重登録を防ぐ）
					if ( dist3(crossPos, lines[a].front()) < 1.0f || dist3(crossPos, lines[a].back()) < 1.0f ) continue;
					if ( dist3(crossPos, lines[b].front()) < 1.0f || dist3(crossPos, lines[b].back()) < 1.0f ) continue;
					addBoth(a, b, 2, crossPos);
				}
			}
		}
	}
}

const std::vector<RailEditor::RailConnectionInfo>& RailEditor::GetRailConnections(int rail) const{
	static const std::vector<RailConnectionInfo> kEmpty;
	EnsureConnectionCache();
	if ( rail < 0 || rail >= ( int ) connCache_.size() ) return kEmpty;
	return connCache_[rail];
}

// 到達可否キャッシュ：railVersion_ が変わった時だけ BFS で作り直す
void RailEditor::EnsureReachableCache() const{
	if ( reachCacheVersion_ == railVersion_ ) return;
	reachCacheVersion_ = railVersion_;

	const int n = ( int ) data_->railLines.size();
	reachable_.assign(n, 0);
	if ( n == 0 ) return;

	// 隣接リスト：通常の接続（溶接/合流/乗り換え）は双方向
	std::vector<std::vector<int>> links(n);
	for ( int a = 0; a < n; ++a ) {
		for ( int b = a + 1; b < n; ++b ) {
			if ( AreRailsLinked(a, b) ) {
				links[a].push_back(b);
				links[b].push_back(a);
			}
		}
	}
	// Gap レールの未接続の端からはジャンプで渡れる（一方通行の辺として追加）。
	//   Safe レールの端はゲーム側でクランプされ飛び出せないので辺にしない。
	for ( int g = 0; g < n && g < ( int ) data_->railGroundTypes.size(); ++g ) {
		if ( data_->railGroundTypes[g] != 1 ) continue; // 1 = SplineRail::GroundType::Gap
		for ( int side = 0; side < 2; ++side ) {
			const bool front = ( side == 0 );
			if ( IsRailEndConnected(g, front) ) continue; // 接続済みの端は合流が優先＝飛べない
			int t = PredictJumpLanding(g, front, true);
			if ( t >= 0 ) links[g].push_back(t);
		}
	}

	// スタートレール（未設定なら0番）から辿れるレールに印を付ける
	int start = data_->startRailIndex;
	if ( start < 0 || start >= n ) start = 0;
	std::vector<int> open;
	open.push_back(start);
	reachable_[start] = 1;
	while ( !open.empty() ) {
		int cur = open.back(); open.pop_back();
		for ( int nx : links[cur] ) {
			if ( nx < 0 || nx >= n || reachable_[nx] ) continue;
			reachable_[nx] = 1;
			open.push_back(nx);
		}
	}
}

bool RailEditor::IsRailReachable(int rail) const{
	EnsureReachableCache();
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return true;
	if ( rail >= ( int ) reachable_.size() ) return true;
	if ( data_->railLines[rail].size() < 2 ) return true; // 作りかけの路線は警告しない
	return reachable_[rail] != 0;
}

// ============================================================
// 複数選択（路線まるごと移動・矩形選択）
// ============================================================
void RailEditor::AddToSelection(int rail, int node){
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
	if ( node < 0 || node >= ( int ) data_->railLines[rail].size() ) return;
	for ( const auto& r : multiSelection_ ) {
		if ( r.rail == rail && r.node == node ) return; // 重複登録しない
	}
	multiSelection_.push_back({ rail, node });
}

void RailEditor::SelectSingleNode(int rail, int node){
	SetCurrentRail(rail);
	selectedRailNode_ = node;
	multiSelection_.clear();
	AddToSelection(rail, node);
}

void RailEditor::SelectWholeRail(int railIdx){
	if ( railIdx < 0 || railIdx >= ( int ) data_->railLines.size() ) return;
	SetCurrentRail(railIdx);
	multiSelection_.clear();
	for ( int n = 0; n < ( int ) data_->railLines[railIdx].size(); ++n ) {
		multiSelection_.push_back({ railIdx, n });
	}
}

// 選択中の全ノードの「穴」フラグをまとめて設定する
void RailEditor::SetSelectionHole(bool hole){
	if ( multiSelection_.empty() ) return;
	// 穴配列を railLines と同数・各レールのノード数に整える
	if ( data_->railNodeHoles.size() != data_->railLines.size() ) {
		data_->railNodeHoles.resize(data_->railLines.size());
	}
	for ( const auto& ref : multiSelection_ ) {
		if ( ref.rail < 0 || ref.rail >= ( int ) data_->railNodeHoles.size() ) continue;
		auto& h = data_->railNodeHoles[ref.rail];
		if ( ( int ) h.size() != ( int ) data_->railLines[ref.rail].size() ) {
			h.resize(data_->railLines[ref.rail].size(), 0);
		}
		if ( ref.node >= 0 && ref.node < ( int ) h.size() ) {
			h[ref.node] = hole ? 1 : 0;
		}
	}
	++railVersion_; // ゲーム側へ即反映（マーカーの色も更新される）
}

// 選択ノードに穴が1つでもあるか
bool RailEditor::SelectionHasHole() const{
	for ( const auto& ref : multiSelection_ ) {
		if ( ref.rail < 0 || ref.rail >= ( int ) data_->railNodeHoles.size() ) continue;
		const auto& h = data_->railNodeHoles[ref.rail];
		if ( ref.node >= 0 && ref.node < ( int ) h.size() && h[ref.node] != 0 ) return true;
	}
	return false;
}

Vector3 RailEditor::GetSelectionCenter() const{
	Vector3 c { 0.0f, 0.0f, 0.0f };
	int cnt = 0;
	for ( const auto& r : multiSelection_ ) {
		Vector3 p;
		if ( GetNodePosOf(r.rail, r.node, p) ) { c.x += p.x; c.y += p.y; c.z += p.z; ++cnt; }
	}
	if ( cnt > 0 ) { c.x /= cnt; c.y /= cnt; c.z /= cnt; }
	return c;
}

void RailEditor::TranslateSelection(const Vector3& delta){
	bool moved = false;
	for ( const auto& r : multiSelection_ ) {
		if ( r.rail < 0 || r.rail >= ( int ) data_->railLines.size() ) continue;
		auto& line = data_->railLines[r.rail];
		if ( r.node < 0 || r.node >= ( int ) line.size() ) continue;
		line[r.node].x += delta.x;
		line[r.node].y += delta.y;
		line[r.node].z += delta.z;
		moved = true;
	}
	if ( moved ) { ++railVersion_; } // 緑線・ゲーム側へライブ反映
}

void RailEditor::SetCurrentRail(int idx){
	if ( idx < 0 || idx >= ( int ) data_->railLines.size() ) return;
	if ( currentEditRailIndex_ != idx ) {
		currentEditRailIndex_ = idx;
		selectedRailNode_ = -1;
	}
}

int RailEditor::GetNodeCountOf(int rail) const{
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return 0;
	return ( int ) data_->railLines[rail].size();
}

// 「レールと同数であるべき全設定配列」をレール数に揃える（既定値で埋める）。
//   追加・削除・読込・Undoのどこで数がズレても、ここを通れば安全になる（配列ズレ事故の一元対策）
void RailEditor::SyncRailArraySizes(){
	size_t n = data_->railLines.size();
	data_->railTypes.resize(n, -1);
	data_->railMotions.resize(n, Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	data_->railGroundTypes.resize(n, 1); // 既定は Gap
	data_->railVisible.resize(n, 1);
	data_->railLineModes.resize(n, 0);
	data_->railRoadModes.resize(n, 0);
	data_->railEndPlazas.resize(n, 0);
	data_->railGuideRails.resize(n, -1);
	data_->railGuideStarts.resize(n, 0.0f);
	data_->railGuideEnds.resize(n, -1.0f);
	data_->railGuideModes.resize(n, 0);
	data_->railGuideAligns.resize(n, 0);
	data_->railGuideDwells.resize(n, 0.0f);
	data_->railGroups.resize(n);
	data_->railNodeHoles.resize(n);
	data_->railMotionTypes.resize(n, 0);
	data_->railMotionPhases.resize(n, 0.0f);
	data_->railMotionTriggers.resize(n, 0);
	data_->railAppearTriggers.resize(n, -1);
	data_->railOneWay.resize(n, 0);
	data_->railSpeedMuls.resize(n, 1.0f);

	// レールが短くなって範囲外に取り残されたブロックを末尾セルへ寄せる。
	//   放置すると「見た目は端に重なって出るのに、当たりは元の位置」「消しゴムが届かない」
	//   という孤児ブロックになる。寄せた先が既に埋まっていたら重複ぶんは削除する
	if ( !data_->blocks.empty() ) {
		bool blocksChanged = false;
		// ブロックがあるレールだけ折れ線長を測る（毎フレーム呼ばれるので無駄計算はしない）
		std::unordered_map<int, float> railLengths;
		for ( auto& block : data_->blocks ) {
			if ( block.rail < 0 || block.rail >= ( int ) n ) continue;
			auto found = railLengths.find(block.rail);
			if ( found == railLengths.end() ) {
				const auto& line = data_->railLines[block.rail];
				float length = 0.0f;
				for ( size_t k = 1; k < line.size(); ++k ) {
					float dx = line[k].x - line[k - 1].x;
					float dy = line[k].y - line[k - 1].y;
					float dz = line[k].z - line[k - 1].z;
					length += std::sqrt(dx * dx + dy * dy + dz * dz);
				}
				found = railLengths.emplace(block.rail, length).first;
			}
			// 折れ線とスプラインの長さ差を考慮して少し余裕を持たせる
			if ( block.dist > found->second + 1.0f ) {
				float newDist = ( std::max )( 0.0f, std::floor(found->second) );
				// 寄せ先が埋まっていたら重複になるので消す（-1マーク）
				block.dist = ( FindBlock(block.rail, newDist, block.level, block.side) >= 0 )
					? -1.0f : newDist;
				blocksChanged = true;
			}
		}
		if ( blocksChanged ) {
			std::erase_if(data_->blocks, [](const BlockData& block){ return block.dist < 0.0f; });
			++blockVersion_;
		}
	}
}

// レールを1本追加し、全設定配列を既定値で揃える。追加したレール番号を返す。
//   ※新しい設定配列を増やす時は SyncRailArraySizes と、ここの push を1組足すだけでよい
int RailEditor::AppendRail(std::vector<Vector3> line, const std::string& group){
	SyncRailArraySizes();
	size_t nodeCount = line.size();
	data_->railLines.push_back(std::move(line));
	data_->railTypes.push_back(-1);
	data_->railMotions.push_back(Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	data_->railGroundTypes.push_back(1); // 既定は Gap（端から落ちられる）
	data_->railVisible.push_back(1);
	data_->railLineModes.push_back(0);
	data_->railRoadModes.push_back(0);
	data_->railEndPlazas.push_back(0);
	data_->railGuideRails.push_back(-1);
	data_->railGuideStarts.push_back(0.0f);
	data_->railGuideEnds.push_back(-1.0f);
	data_->railGuideModes.push_back(0);
	data_->railGuideAligns.push_back(0);
	data_->railGuideDwells.push_back(0.0f);
	data_->railGroups.push_back(group);
	data_->railNodeHoles.push_back(std::vector<int>(nodeCount, 0));
	data_->railMotionTypes.push_back(0);
	data_->railMotionPhases.push_back(0.0f);
	data_->railMotionTriggers.push_back(0);
	data_->railAppearTriggers.push_back(-1);
	data_->railOneWay.push_back(0);
	data_->railSpeedMuls.push_back(1.0f);
	return ( int ) data_->railLines.size() - 1;
}

// レール1本と、それに紐づく全設定配列の同じ番号を削除する（配列ズレの一元防止）。
//   選択状態のリセットと RebuildRailPoints は呼び出し側で行う（まとめて消す時に1回で済むように）
void RailEditor::EraseRail(int idx){
	if ( idx < 0 || idx >= ( int ) data_->railLines.size() ) return;
	SyncRailArraySizes(); // 念のため数を揃えてから消す（erase漏れによるズレを防ぐ）
	data_->railLines.erase(data_->railLines.begin() + idx);
	data_->railTypes.erase(data_->railTypes.begin() + idx);
	data_->railMotions.erase(data_->railMotions.begin() + idx);
	data_->railGroundTypes.erase(data_->railGroundTypes.begin() + idx);
	data_->railVisible.erase(data_->railVisible.begin() + idx);
	data_->railLineModes.erase(data_->railLineModes.begin() + idx);
	data_->railRoadModes.erase(data_->railRoadModes.begin() + idx);
	data_->railEndPlazas.erase(data_->railEndPlazas.begin() + idx);
	data_->railGuideRails.erase(data_->railGuideRails.begin() + idx);
	data_->railGuideStarts.erase(data_->railGuideStarts.begin() + idx);
	data_->railGuideEnds.erase(data_->railGuideEnds.begin() + idx);
	data_->railGuideModes.erase(data_->railGuideModes.begin() + idx);
	data_->railGuideAligns.erase(data_->railGuideAligns.begin() + idx);
	data_->railGuideDwells.erase(data_->railGuideDwells.begin() + idx);
	data_->railGroups.erase(data_->railGroups.begin() + idx);
	data_->railNodeHoles.erase(data_->railNodeHoles.begin() + idx);
	data_->railMotionTypes.erase(data_->railMotionTypes.begin() + idx);   // ※従来ここから4本が消えておらず、
	data_->railMotionPhases.erase(data_->railMotionPhases.begin() + idx); //   削除のたびに後続レールの動き設定が
	data_->railMotionTriggers.erase(data_->railMotionTriggers.begin() + idx); // 1つずつズレるバグがあった
	data_->railAppearTriggers.erase(data_->railAppearTriggers.begin() + idx);
	data_->railOneWay.erase(data_->railOneWay.begin() + idx);
	data_->railSpeedMuls.erase(data_->railSpeedMuls.begin() + idx);
	// ガイド参照の付け替え：消した番号より後ろは1つ前へ、消した本人を指していたら解除
	for ( auto& guideRef : data_->railGuideRails ) {
		if ( guideRef == idx ) { guideRef = -1; }
		else if ( guideRef > idx ) { --guideRef; }
	}
	// 出現トリガーの参照も同様に付け替え
	for ( auto& appearRef : data_->railAppearTriggers ) {
		if ( appearRef == idx ) { appearRef = -1; }
		else if ( appearRef > idx ) { --appearRef; }
	}
	// コインの付け替え：消したレール上のコインは削除、後ろのレールのコインは番号を1つ前へ
	std::erase_if(data_->coins, [idx](const CoinData& coin){ return coin.rail == idx; });
	for ( auto& coin : data_->coins ) {
		if ( coin.rail > idx ) { --coin.rail; }
	}
	// ブロックも同様に付け替え
	std::erase_if(data_->blocks, [idx](const BlockData& block){ return block.rail == idx; });
	for ( auto& block : data_->blocks ) {
		if ( block.rail > idx ) { --block.rail; }
	}
	++blockVersion_;
}

// =====================================================================
//  ブロック（乗れる/ぶつかる1m角）の編集。セル＝(レール, 距離1m刻み, 段数)
// =====================================================================
namespace {
	// 型ごとの dist 方向の占有半幅。横長(7)/台座(8)は2mぶん占有する。
	//   斜面(2,3)は従来どおり1セル扱い（ブロックへ立てかけて重ねる使い方を許すため）
	float BlockOccupyHalf(int type){ return ( type == 7 || type == 8 ) ? 1.0f : 0.5f; }
}

// ==== 2D形状エディタ =========================================================================
//   ETOSツール風：レールの経路を2D断面（横見図/上から見た図）で表示し、点をつかんで編集できる。
//   ゲームビューと同じ配列（railLines[railIdx]）を直接書くので3D表示とは常に同期する。
//   作成タブ＝選択中の路線（動かないレールもOK） / 動きタブ＝ガイドレール（motionOverlay=true で
//   区間◆マーカーと「足場の実際の動き」の青丸も表示）の両方から使う共通部品
void RailEditor::DrawRailPathEditor(int railIdx, bool motionOverlay){
	if ( railIdx < 0 || railIdx >= ( int ) data_->railLines.size() ) {
		guidePanelDragNode_ = -1; guidePanelDragMark_ = 0; guidePanelDragging_ = false;
		return;
	}
	// タブ切替等で編集対象が変わったらドラッグ状態を破棄（別レールのノードを書かない安全弁）
	if ( guidePanelDragRail_ != railIdx ) {
		guidePanelDragNode_ = -1; guidePanelDragMark_ = 0; guidePanelDragging_ = false;
	}
	auto& line = data_->railLines[railIdx];
	if ( line.size() < 2 ) {
		ImGui::TextDisabled("ノードが2個未満です（作成タブで点を追加すると編集できます）");
		guidePanelDragNode_ = -1; guidePanelDragMark_ = 0; guidePanelDragging_ = false;
		return;
	}

	ImGui::Separator();
	ImGui::TextDisabled(motionOverlay
		? "リフト経路エディタ: 点をドラッグ=移動 / 線を引っ張る=点を追加 / 右クリック=削除 / ◆=区間の端"
		: "点をドラッグ=移動 / 線を引っ張る=点を追加 / 右クリック=削除");
	ImGui::TextDisabled("ホイール=ズーム / 中ボタン=スクロール / ドラッグ中 Shift=縦だけ・Ctrl=横だけ");
	ImGui::SameLine();
	if ( ImGui::SmallButton("全体表示##pathFit") ) {
		pathEditZoom_ = 1.0f; pathEditPanX_ = 0.0f; pathEditPanY_ = 0.0f;
	}
	// 断面の選択：横見図（坂やリフト用）⇔ 上から（奥行き⇔横向きのカーブ用）を切り替えられる
	ImGui::SetNextItemWidth(170.0f);
	const char* planeLabels[] = { "断面: 自動", "横見図 (X-Y)", "横見図 (Z-Y)", "上から (X-Z)" };
	ImGui::Combo("##pathEditPlane", &pathEditPlane_, planeLabels, 4);
	if ( ImGui::IsItemHovered() ) {
		ImGui::SetTooltip("横見図＝高さ（坂・リフト）を編集 / 上から＝奥行き⇔横向きのカーブを編集\n自動＝広がりが大きい水平軸×高さ");
	}

	// --- 投影軸と表示範囲：ドラッグ中は凍結する ---
	//   ノードを動かすたびに「全体フィット」と「自動断面」を計算し直すと、
	//   マウス→ワールドの換算が毎フレームずれて、ノードがマウスから逃げるように
	//   遠くへ飛んでいく暴走ループになる（特に自動断面がX-Y⇔Z-Yへ切り替わった瞬間）。
	//   ドラッグ開始時点の表示を保持し、離した時に再フィットする
	const bool pathViewFrozen = ( guidePanelDragNode_ >= 0 || guidePanelDragMark_ != 0 || pathEditPullPending_ );
	if ( !pathViewFrozen ) {
		float minX = line[0].x, maxX = minX, minZ = line[0].z, maxZ = minZ;
		float minYw = line[0].y, maxYw = minYw;
		for ( const auto& p : line ) {
			minX = ( std::min )( minX, p.x ); maxX = ( std::max )( maxX, p.x );
			minZ = ( std::min )( minZ, p.z ); maxZ = ( std::max )( maxZ, p.z );
			minYw = ( std::min )( minYw, p.y ); maxYw = ( std::max )( maxYw, p.y );
		}
		int resolvedPlane = pathEditPlane_;
		if ( resolvedPlane == 0 ) { resolvedPlane = ( ( maxX - minX ) >= ( maxZ - minZ ) ) ? 1 : 2; } // 自動＝広い水平軸×Y
		pathViewPlane_ = resolvedPlane;
		pathViewMinH_ = ( ( resolvedPlane == 2 ) ? minZ : minX ) - 2.0f;
		pathViewMaxH_ = ( ( resolvedPlane == 2 ) ? maxZ : maxX ) + 2.0f;
		pathViewMinV_ = ( ( resolvedPlane == 3 ) ? minZ : minYw ) - 2.0f;
		pathViewMaxV_ = ( ( resolvedPlane == 3 ) ? maxZ : maxYw ) + 2.0f;
	}
	const int plane = pathViewPlane_;
	auto axisA = [&](const Vector3& p){ return ( plane == 2 ) ? p.z : p.x; };
	auto axisB = [&](const Vector3& p){ return ( plane == 3 ) ? p.z : p.y; };
	const char* axisAName = ( plane == 2 ) ? "Z" : "X";
	const char* axisBName = ( plane == 3 ) ? "Z" : "Y";
	const float minH = pathViewMinH_, maxH = pathViewMaxH_;
	const float minY = pathViewMinV_, maxY = pathViewMaxV_; // キャンバス縦軸（Y または Z）の範囲

	// 表示中レールが変わったらズーム/パン/選択をリセット（前のレールの拡大位置を引きずらない）
	if ( pathEditLastRail_ != railIdx ) {
		pathEditLastRail_ = railIdx;
		pathEditZoom_ = 1.0f; pathEditPanX_ = 0.0f; pathEditPanY_ = 0.0f;
		pathEditSelNode_ = -1; pathEditPullPending_ = false;
	}

	// --- キャンバス ---
	const float panelWidth  = ( std::max )( ImGui::GetContentRegionAvail().x, 220.0f );
	const float panelHeight = 240.0f;
	ImGui::InvisibleButton("guide_path_canvas", ImVec2(panelWidth, panelHeight));
	const bool canvasHovered = ImGui::IsItemHovered();
	const ImVec2 canvasMin = ImGui::GetItemRectMin();
	const ImVec2 canvasMax { canvasMin.x + panelWidth, canvasMin.y + panelHeight };
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(canvasMin, canvasMax, IM_COL32(24, 30, 40, 255), 4.0f);
	draw->PushClipRect(canvasMin, canvasMax, true); // ズーム中に枠の外へ描かない

	// 写像：中心基準＋ズーム＋パン（toCanvas と逆変換が対で崩れないようにここで一元化）
	const float spanH = ( std::max )( maxH - minH, 0.001f );
	const float spanY = ( std::max )( maxY - minY, 0.001f );
	const float fitScale = ( std::min )( ( panelWidth - 16.0f ) / spanH, ( panelHeight - 16.0f ) / spanY );
	const float scale = fitScale * pathEditZoom_;
	const float centerH = ( minH + maxH ) * 0.5f;
	const float centerV = ( minY + maxY ) * 0.5f;
	const float centerPxX = canvasMin.x + panelWidth * 0.5f + pathEditPanX_;
	const float centerPxY = canvasMin.y + panelHeight * 0.5f + pathEditPanY_;
	auto toCanvas = [&](float h, float y) -> ImVec2 {
		return { centerPxX + ( h - centerH ) * scale, centerPxY + ( centerV - y ) * scale };
	};
	auto nodeCanvas = [&](const Vector3& p){ return toCanvas(axisA(p), axisB(p)); };

	// 1mグリッド（見やすい拡大率の時だけ。5m毎に少し濃く。表示中の範囲だけ引く）
	if ( scale >= 5.0f ) {
		const float viewMinH = centerH + ( canvasMin.x - centerPxX ) / scale;
		const float viewMaxH = centerH + ( canvasMax.x - centerPxX ) / scale;
		const float viewMinV = centerV - ( canvasMax.y - centerPxY ) / scale;
		const float viewMaxV = centerV - ( canvasMin.y - centerPxY ) / scale;
		for ( float h = std::ceil(viewMinH); h <= viewMaxH; h += 1.0f ) {
			ImU32 gridColor = ( std::abs(std::fmod(h, 5.0f)) < 0.01f ) ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 13);
			draw->AddLine(toCanvas(h, viewMinV), toCanvas(h, viewMaxV), gridColor);
		}
		for ( float y = std::ceil(viewMinV); y <= viewMaxV; y += 1.0f ) {
			ImU32 gridColor = ( std::abs(std::fmod(y, 5.0f)) < 0.01f ) ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 13);
			draw->AddLine(toCanvas(viewMinH, y), toCanvas(viewMaxH, y), gridColor);
		}
	}

	// --- 弧長テーブル（3D長。区間マーカー・動きの点の位置決めに使う）---
	std::vector<float> cum(line.size(), 0.0f);
	for ( size_t i = 1; i < line.size(); ++i ) {
		float dx = line[i].x - line[i - 1].x;
		float dy = line[i].y - line[i - 1].y;
		float dz = line[i].z - line[i - 1].z;
		cum[i] = cum[i - 1] + std::sqrt(dx * dx + dy * dy + dz * dz);
	}
	const float totalLen = cum.back();
	auto posAt = [&](float d) -> Vector3 {
		d = std::clamp(d, 0.0f, totalLen);
		for ( size_t i = 1; i < line.size(); ++i ) {
			if ( d <= cum[i] || i == line.size() - 1 ) {
				float segLen = ( std::max )( cum[i] - cum[i - 1], 0.0001f );
				float t = std::clamp(( d - cum[i - 1] ) / segLen, 0.0f, 1.0f);
				return { line[i - 1].x + ( line[i].x - line[i - 1].x ) * t,
				         line[i - 1].y + ( line[i].y - line[i - 1].y ) * t,
				         line[i - 1].z + ( line[i].z - line[i - 1].z ) * t };
			}
		}
		return line.back();
	};

	// 区間 [s0, s1]（RailField::UpdateMotion case3 と同じ解釈。オーバーレイ無しなら全区間）
	float s0 = 0.0f, s1 = totalLen;
	if ( motionOverlay && currentEditRailIndex_ >= 0
		&& currentEditRailIndex_ < ( int ) data_->railGuideEnds.size() ) {
		const float gEnd = data_->railGuideEnds[currentEditRailIndex_];
		s0 = std::clamp(data_->railGuideStarts[currentEditRailIndex_], 0.0f, totalLen);
		s1 = ( gEnd < 0.0f ) ? totalLen : std::clamp(gEnd, 0.0f, totalLen);
		if ( s1 < s0 ) { std::swap(s0, s1); }
		if ( s1 - s0 < 0.01f ) { s0 = 0.0f; s1 = totalLen; }
	}

	// --- 経路の描画 ---
	//   スプライン路線はゲームと同じ Catmull-Rom 曲線（端は鏡映）で描く＝見たまま。
	//   直線同士は薄い「制御ケージ」として重ね、点と曲線の関係が分かるようにする
	const bool smoothLine = ( railIdx < ( int ) data_->railLineModes.size()
		&& data_->railLineModes[railIdx] == 0 );
	auto crPoint = [&](int i, float t) -> Vector3 {
		const Vector3& p1 = line[i];
		const Vector3& p2 = line[i + 1];
		Vector3 p0 = ( i - 1 >= 0 ) ? line[i - 1]
			: Vector3 { 2.0f * p1.x - p2.x, 2.0f * p1.y - p2.y, 2.0f * p1.z - p2.z };
		Vector3 p3 = ( i + 2 < ( int ) line.size() ) ? line[i + 2]
			: Vector3 { 2.0f * p2.x - p1.x, 2.0f * p2.y - p1.y, 2.0f * p2.z - p1.z };
		auto cr = [&](float a0, float a1, float a2, float a3){
			return 0.5f * ( 2.0f * a1 + ( -a0 + a2 ) * t
				+ ( 2.0f * a0 - 5.0f * a1 + 4.0f * a2 - a3 ) * t * t
				+ ( -a0 + 3.0f * a1 - 3.0f * a2 + a3 ) * t * t * t );
		};
		return { cr(p0.x, p1.x, p2.x, p3.x), cr(p0.y, p1.y, p2.y, p3.y), cr(p0.z, p1.z, p2.z, p3.z) };
	};
	if ( smoothLine ) {
		for ( size_t i = 1; i < line.size(); ++i ) { // 制御ケージ（点の並びが分かる薄い直線）
			draw->AddLine(nodeCanvas(line[i - 1]), nodeCanvas(line[i]), IM_COL32(110, 170, 235, 55), 1.0f);
		}
		for ( int i = 0; i < ( int ) line.size() - 1; ++i ) { // 実際に通る曲線
			ImVec2 prev = nodeCanvas(line[i]);
			for ( int k = 1; k <= 8; ++k ) {
				ImVec2 cur = nodeCanvas(crPoint(i, ( float ) k / 8.0f));
				draw->AddLine(prev, cur, IM_COL32(110, 170, 235, 170), 2.0f);
				prev = cur;
			}
		}
	} else {
		for ( size_t i = 1; i < line.size(); ++i ) {
			draw->AddLine(nodeCanvas(line[i - 1]), nodeCanvas(line[i]), IM_COL32(110, 170, 235, 150), 2.0f);
		}
	}
	if ( motionOverlay ) { // 動く区間を白太で強調
		int steps = std::clamp(( int ) ( ( s1 - s0 ) * 2.0f ), 2, 200);
		ImVec2 prev = nodeCanvas(posAt(s0));
		for ( int k = 1; k <= steps; ++k ) {
			ImVec2 cur = nodeCanvas(posAt(s0 + ( s1 - s0 ) * ( float ) k / ( float ) steps));
			draw->AddLine(prev, cur, IM_COL32(220, 240, 255, 220), 3.5f);
			prev = cur;
		}
	}

	// --- マウス入力（ノード優先 → 区間マーカー）---
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	auto distPx = [&](const ImVec2& a) -> float {
		float dx = a.x - mouse.x, dy = a.y - mouse.y;
		return std::sqrt(dx * dx + dy * dy);
	};
	int hoverNode = -1;
	if ( canvasHovered ) {
		float bestNodePx = 10.0f;
		for ( int i = 0; i < ( int ) line.size(); ++i ) {
			float d = distPx(nodeCanvas(line[i]));
			if ( d < bestNodePx ) { bestNodePx = d; hoverNode = i; }
		}
	}
	const ImVec2 markStart = nodeCanvas(posAt(s0));
	const ImVec2 markEnd   = nodeCanvas(posAt(s1));
	int hoverMark = 0;
	if ( motionOverlay && canvasHovered && hoverNode < 0 ) {
		if      ( distPx(markEnd)   < 10.0f ) { hoverMark = 2; }
		else if ( distPx(markStart) < 10.0f ) { hoverMark = 1; }
	}

	// 線分ホバー検出（ノード・◆優先。引っ張り挿入と「＋」ゴーストに使う）
	int hoverSeg = -1; float hoverSegT = 0.0f;
	if ( canvasHovered && hoverNode < 0 && hoverMark == 0
		&& guidePanelDragNode_ < 0 && guidePanelDragMark_ == 0 ) {
		float bestSegPx = 12.0f;
		for ( size_t i = 1; i < line.size(); ++i ) {
			ImVec2 a = nodeCanvas(line[i - 1]), b = nodeCanvas(line[i]);
			float vx = b.x - a.x, vy = b.y - a.y;
			float len2 = vx * vx + vy * vy;
			float t = ( len2 > 1e-5f )
				? std::clamp((( mouse.x - a.x ) * vx + ( mouse.y - a.y ) * vy) / len2, 0.0f, 1.0f)
				: 0.0f;
			ImVec2 c { a.x + vx * t, a.y + vy * t };
			float d = distPx(c);
			if ( d < bestSegPx ) { bestSegPx = d; hoverSeg = ( int ) i; hoverSegT = t; }
		}
		// 「＋」ゴースト：ここを押してそのまま引っ張れば点が増える目印
		if ( hoverSeg >= 1 && !pathEditPullPending_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left) ) {
			ImVec2 a = nodeCanvas(line[hoverSeg - 1]), b = nodeCanvas(line[hoverSeg]);
			ImVec2 hint { a.x + ( b.x - a.x ) * hoverSegT, a.y + ( b.y - a.y ) * hoverSegT };
			draw->AddCircleFilled(hint, 6.0f, IM_COL32(120, 255, 160, 230));
			draw->AddLine({ hint.x - 3.5f, hint.y }, { hint.x + 3.5f, hint.y }, IM_COL32(15, 60, 30, 255), 1.6f);
			draw->AddLine({ hint.x, hint.y - 3.5f }, { hint.x, hint.y + 3.5f }, IM_COL32(15, 60, 30, 255), 1.6f);
		}
	}

	if ( canvasHovered && guidePanelDragNode_ < 0 && guidePanelDragMark_ == 0 ) {
		if ( ImGui::IsMouseClicked(ImGuiMouseButton_Left) ) {
			if ( hoverNode >= 0 ) {
				guidePanelDragNode_ = hoverNode;
				guidePanelDragRail_ = railIdx;
				pathEditSelNode_ = hoverNode; // 数値入力欄の対象にする
				// 自分の路線を編集中なら3D側の選択ノードも同期（ギズモや敵配置の基準に使える）
				if ( railIdx == currentEditRailIndex_ ) {
					selectedRailNode_ = hoverNode;
					multiSelection_.clear();
					multiSelection_.push_back({ railIdx, hoverNode });
				}
			} else if ( hoverMark != 0 ) {
				guidePanelDragMark_ = hoverMark;
				guidePanelDragRail_ = railIdx;
			} else if ( hoverSeg >= 1 && !ImGui::GetIO().KeyCtrl ) {
				// 線を押した：動かせば「点を追加して引っ張る」/ 動かさず離せば何もしない（Ctrl不要）
				pathEditPullPending_ = true;
				pathEditPullSeg_ = hoverSeg;
				pathEditPullT_   = hoverSegT;
				pathEditPullX_ = mouse.x;
				pathEditPullY_ = mouse.y;
			} else if ( ImGui::GetIO().KeyCtrl ) {
				// Ctrl+クリック＝一番近い線分の上にノードを挿入（曲がり角を増やして形を作る）
				float bestPx = 12.0f; int insertAt = -1; float insertT = 0.0f;
				for ( size_t i = 1; i < line.size(); ++i ) {
					ImVec2 a = nodeCanvas(line[i - 1]), b = nodeCanvas(line[i]);
					float vx = b.x - a.x, vy = b.y - a.y;
					float len2 = vx * vx + vy * vy;
					float t = ( len2 > 1e-5f )
						? std::clamp((( mouse.x - a.x ) * vx + ( mouse.y - a.y ) * vy) / len2, 0.0f, 1.0f)
						: 0.0f;
					ImVec2 c { a.x + vx * t, a.y + vy * t };
					float d = distPx(c);
					if ( d < bestPx ) { bestPx = d; insertAt = ( int ) i; insertT = t; }
				}
				if ( insertAt > 0 ) {
					const Vector3& a = line[insertAt - 1];
					const Vector3& b = line[insertAt];
					Vector3 inserted { a.x + ( b.x - a.x ) * insertT,
					                   a.y + ( b.y - a.y ) * insertT,
					                   a.z + ( b.z - a.z ) * insertT };
					line.insert(line.begin() + insertAt, inserted);
					if ( railIdx < ( int ) data_->railNodeHoles.size()
						&& insertAt <= ( int ) data_->railNodeHoles[railIdx].size() ) {
						data_->railNodeHoles[railIdx].insert(
							data_->railNodeHoles[railIdx].begin() + insertAt, 0);
					}
					RebuildRailPoints();
					guidePanelDragging_ = false;
					draw->PopClipRect();
					return; // 弧長テーブル等が古くなるので今フレームはここまで
				}
			}
		}
		// 右クリック＝ノード削除（最低2点は残す）
		if ( hoverNode >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && line.size() > 2 ) {
			line.erase(line.begin() + hoverNode);
			if ( railIdx < ( int ) data_->railNodeHoles.size()
				&& hoverNode < ( int ) data_->railNodeHoles[railIdx].size() ) {
				data_->railNodeHoles[railIdx].erase(data_->railNodeHoles[railIdx].begin() + hoverNode);
			}
			if ( railIdx == currentEditRailIndex_ && selectedRailNode_ >= ( int ) line.size() ) {
				selectedRailNode_ = -1; // 消した分だけ選択が範囲外になったら解除
			}
			pathEditSelNode_ = -1;
			RebuildRailPoints();
			guidePanelDragging_ = false;
			draw->PopClipRect();
			return; // 弧長テーブル等が古くなるので今フレームはここまで（次フレームで描き直し）
		}
	}

	// --- 線を引っ張って点を追加：押したまま4px動かすと挿入＋そのままドラッグ ---
	//   動かさずに離した時は何もしない（誤挿入なし）
	if ( pathEditPullPending_ ) {
		float pullDx = mouse.x - pathEditPullX_, pullDy = mouse.y - pathEditPullY_;
		float movedPx = std::sqrt(pullDx * pullDx + pullDy * pullDy);
		if ( !ImGui::IsMouseDown(ImGuiMouseButton_Left) ) {
			pathEditPullPending_ = false;
		} else if ( movedPx >= 4.0f && pathEditPullSeg_ >= 1 && pathEditPullSeg_ < ( int ) line.size() ) {
			pathEditPullPending_ = false;
			const Vector3& segA = line[pathEditPullSeg_ - 1];
			const Vector3& segB = line[pathEditPullSeg_];
			Vector3 inserted { segA.x + ( segB.x - segA.x ) * pathEditPullT_,
			                   segA.y + ( segB.y - segA.y ) * pathEditPullT_,
			                   segA.z + ( segB.z - segA.z ) * pathEditPullT_ };
			line.insert(line.begin() + pathEditPullSeg_, inserted);
			if ( railIdx < ( int ) data_->railNodeHoles.size()
				&& pathEditPullSeg_ <= ( int ) data_->railNodeHoles[railIdx].size() ) {
				data_->railNodeHoles[railIdx].insert(
					data_->railNodeHoles[railIdx].begin() + pathEditPullSeg_, 0);
			}
			// 挿入した点をそのまま既存のドラッグ処理へ引き渡す（この後すぐマウスに吸い付く）
			guidePanelDragNode_ = pathEditPullSeg_;
			guidePanelDragRail_ = railIdx;
			pathEditSelNode_ = pathEditPullSeg_;
			if ( railIdx == currentEditRailIndex_ ) {
				selectedRailNode_ = pathEditPullSeg_;
				multiSelection_.clear();
				multiSelection_.push_back({ railIdx, pathEditPullSeg_ });
			}
			RebuildRailPoints();
		}
	}

	if ( ImGui::IsMouseDown(ImGuiMouseButton_Left) && ( guidePanelDragNode_ >= 0 || guidePanelDragMark_ != 0 ) ) {
		const float worldH = centerH + ( mouse.x - centerPxX ) / scale;
		const float worldY = centerV - ( mouse.y - centerPxY ) / scale;
		if ( guidePanelDragNode_ >= 0 && guidePanelDragNode_ < ( int ) line.size() ) {
			// ノード移動：投影面の2軸だけ書く（画面に出ていない軸はそのまま）。スナップ設定も効く。
			//   Shift=縦（高さ）だけ / Ctrl=横だけ の軸制限つき（まっすぐ動かしたい時用）
			Vector3& node = line[guidePanelDragNode_];
			ImGuiIO& dragIo = ImGui::GetIO();
			const float snappedH = SnapValue(worldH);
			const float snappedV = SnapValue(worldY);
			if ( !dragIo.KeyShift ) { if ( plane == 2 ) { node.z = snappedH; } else { node.x = snappedH; } }
			if ( !dragIo.KeyCtrl )  { if ( plane == 3 ) { node.z = snappedV; } else { node.y = snappedV; } }
			if      ( dragIo.KeyShift ) { ImGui::SetTooltip("%s=%.1f（縦だけ）", axisBName, snappedV); }
			else if ( dragIo.KeyCtrl )  { ImGui::SetTooltip("%s=%.1f（横だけ）", axisAName, snappedH); }
			else { ImGui::SetTooltip("%s=%.1f  %s=%.1f", axisAName, snappedH, axisBName, snappedV); }
			RebuildRailPoints(); // ライブ同期（ドラッグ中はゲーム側が10Hzの軽量同期に間引く）
		} else if ( guidePanelDragMark_ != 0 ) {
			// 区間マーカー移動：マウスに一番近い経路上の弧長距離へ（0.5m刻み）
			float bestPx = 1e9f, bestDist = 0.0f;
			for ( size_t i = 1; i < line.size(); ++i ) {
				ImVec2 a = nodeCanvas(line[i - 1]), b = nodeCanvas(line[i]);
				float vx = b.x - a.x, vy = b.y - a.y;
				float len2 = vx * vx + vy * vy;
				float t = ( len2 > 1e-5f )
					? std::clamp((( mouse.x - a.x ) * vx + ( mouse.y - a.y ) * vy) / len2, 0.0f, 1.0f)
					: 0.0f;
				ImVec2 c { a.x + vx * t, a.y + vy * t };
				float d = distPx(c);
				if ( d < bestPx ) { bestPx = d; bestDist = cum[i - 1] + ( cum[i] - cum[i - 1] ) * t; }
			}
			float snapped = std::round(bestDist * 2.0f) * 0.5f;
			if ( guidePanelDragMark_ == 1 ) { data_->railGuideStarts[currentEditRailIndex_] = snapped; }
			else { data_->railGuideEnds[currentEditRailIndex_] = snapped; } // 明示指定＝「終点まで」は自動で外れる
			RebuildRailPoints();
		}
	} else if ( guidePanelDragNode_ >= 0 || guidePanelDragMark_ != 0 ) {
		// 離した：確定（Undo履歴はマウスアップ後に自動で1回分として記録される）
		guidePanelDragNode_ = -1;
		guidePanelDragMark_ = 0;
		guidePanelDragRail_ = -1;
		RebuildRailPoints();
	}
	guidePanelDragging_ = ( guidePanelDragNode_ >= 0 || guidePanelDragMark_ != 0 );

	// --- ノード（ホバー/ドラッグ中=白・大きめ。選択中ノードは黄色の輪）---
	for ( int i = 0; i < ( int ) line.size(); ++i ) {
		const bool active = ( i == hoverNode || i == guidePanelDragNode_ );
		ImVec2 c = nodeCanvas(line[i]);
		draw->AddCircleFilled(c, active ? 6.0f : 4.5f,
			active ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 200, 255, 255));
		draw->AddCircle(c, active ? 6.0f : 4.5f, IM_COL32(25, 45, 75, 255), 0, 1.5f);
		if ( railIdx == currentEditRailIndex_ && i == selectedRailNode_ ) {
			draw->AddCircle(c, 9.0f, IM_COL32(255, 220, 80, 220), 0, 2.0f); // 3D側と同じ選択ノード
		}
	}

	if ( motionOverlay ) {
		// --- 区間マーカー（緑◆=ここから / 橙◆=ここまで）---
		auto diamond = [&](const ImVec2& c, ImU32 col, float r){
			draw->AddQuadFilled({ c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y }, col);
		};
		diamond(markStart, IM_COL32(90, 255, 120, 255),  ( hoverMark == 1 || guidePanelDragMark_ == 1 ) ? 8.0f : 6.0f);
		diamond(markEnd,   IM_COL32(255, 170, 60, 255),  ( hoverMark == 2 || guidePanelDragMark_ == 2 ) ? 8.0f : 6.0f);

		// --- 足場の実際の動きを常時アニメ表示（周期・位相・動き方を反映。ETOSの同期表示と同じ狙い）---
		float period = 2.0f;
		if ( currentEditRailIndex_ < ( int ) data_->railMotions.size() ) {
			period = ( std::max )( data_->railMotions[currentEditRailIndex_].w, 0.1f );
		}
		const float phase = ( currentEditRailIndex_ < ( int ) data_->railMotionPhases.size() )
			? data_->railMotionPhases[currentEditRailIndex_] : 0.0f;
		const int guideMode = ( currentEditRailIndex_ < ( int ) data_->railGuideModes.size() )
			? data_->railGuideModes[currentEditRailIndex_] : 0;
		const float u = ( float ) std::fmod(ImGui::GetTime() / period + phase, 1.0);
		float w;
		if ( guideMode == 1 ) {
			w = 0.5f - 0.5f * std::cos(u * 6.2831853f); // 往復
		} else if ( guideMode == 2 ) {
			// 片道：到着→少し停車→最初から（実際のゲームでは到着後そのまま停まる。ここは確認用の繰り返し）
			float cycle = ( float ) std::fmod(ImGui::GetTime() / period + phase, 1.3);
			w = 0.5f - 0.5f * std::cos(( std::min )( cycle, 1.0f ) * 3.14159265f);
		} else {
			w = u; // 一周ループ
		}
		ImVec2 dot = nodeCanvas(posAt(s0 + ( s1 - s0 ) * w));
		draw->AddCircleFilled(dot, 6.0f, IM_COL32(120, 220, 255, 255));
		draw->AddCircle(dot, 9.0f, IM_COL32(120, 220, 255, 110), 0, 2.0f);
	}

	draw->PopClipRect();

	// --- ズーム＆パン（ホイール=カーソル位置基準のズーム / 中ボタンドラッグ=スクロール）---
	//   ノードのドラッグ中は写像が変わらないようズームも受け付けない（飛び対策）
	if ( canvasHovered && !pathViewFrozen && ImGui::GetIO().MouseWheel != 0.0f ) {
		float worldAtH = centerH + ( mouse.x - centerPxX ) / scale;
		float worldAtV = centerV - ( mouse.y - centerPxY ) / scale;
		pathEditZoom_ = std::clamp(pathEditZoom_ * std::exp(ImGui::GetIO().MouseWheel * 0.15f), 0.5f, 12.0f);
		float newScale = fitScale * pathEditZoom_;
		pathEditPanX_ = mouse.x - ( canvasMin.x + panelWidth * 0.5f ) - ( worldAtH - centerH ) * newScale;
		pathEditPanY_ = mouse.y - ( canvasMin.y + panelHeight * 0.5f ) + ( worldAtV - centerV ) * newScale;
	}
	if ( canvasHovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle) ) {
		pathEditPanX_ += ImGui::GetIO().MouseDelta.x;
		pathEditPanY_ += ImGui::GetIO().MouseDelta.y;
	}

	// --- 操作ボタン ---
	if ( ImGui::SmallButton("末尾にノード追加##guidePath") ) {
		Vector3 last = line.back(), prev = line[line.size() - 2];
		Vector3 dir { last.x - prev.x, last.y - prev.y, last.z - prev.z };
		float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if ( len < 0.01f ) { dir = { 0.0f, 3.0f, 0.0f }; }
		else { dir = { dir.x / len * 3.0f, dir.y / len * 3.0f, dir.z / len * 3.0f }; }
		line.push_back({ last.x + dir.x, last.y + dir.y, last.z + dir.z });
		if ( railIdx < ( int ) data_->railNodeHoles.size() ) {
			data_->railNodeHoles[railIdx].resize(line.size(), 0);
		}
		RebuildRailPoints();
	}
	ImGui::SameLine();
	if ( motionOverlay ) {
		ImGui::TextDisabled("%s-%s断面 / ガイド全長 %.1fm / 青丸=足場の今の位置", axisAName, axisBName, totalLen);
	} else {
		ImGui::TextDisabled("%s-%s断面 / 全長 %.1fm", axisAName, axisBName, totalLen);
	}

	// --- 選択ノードの数値入力（触った点を正確な値に合わせる）---
	if ( pathEditSelNode_ >= 0 && pathEditSelNode_ < ( int ) line.size() ) {
		Vector3& selNode = line[pathEditSelNode_];
		bool coordChanged = false;
		ImGui::TextDisabled("ノード%d:", pathEditSelNode_);
		ImGui::SameLine(); ImGui::SetNextItemWidth(72.0f);
		coordChanged |= ImGui::DragFloat("X##pathNodeX", &selNode.x, 0.1f, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine(); ImGui::SetNextItemWidth(72.0f);
		coordChanged |= ImGui::DragFloat("Y##pathNodeY", &selNode.y, 0.1f, 0.0f, 0.0f, "%.1f");
		ImGui::SameLine(); ImGui::SetNextItemWidth(72.0f);
		coordChanged |= ImGui::DragFloat("Z##pathNodeZ", &selNode.z, 0.1f, 0.0f, 0.0f, "%.1f");
		if ( coordChanged ) { RebuildRailPoints(); }
	}
}

int RailEditor::FindBlock(int rail, float dist, int level, float side) const{
	for ( int i = 0; i < ( int ) data_->blocks.size(); ++i ) {
		const BlockData& block = data_->blocks[i];
		// dist はブロックの占有幅でカバー判定（2m級は端のセルでも「ここにある」扱い＝端クリックで消せる）
		if ( block.rail == rail && block.level == level
			&& std::abs(block.dist - dist) < BlockOccupyHalf(block.type) + 0.49f
			&& std::abs(block.side - side) < 0.51f ) {
			return i;
		}
	}
	return -1;
}

void RailEditor::AddBlock(int rail, float dist, int level, float side, int type){
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return;
	if ( level < 0 ) return;
	// 占有幅同士の重なりチェック（2m級を既存ブロックへ食い込ませない）
	float newHalf = BlockOccupyHalf(type);
	for ( const BlockData& block : data_->blocks ) {
		if ( block.rail == rail && block.level == level
			&& std::abs(block.dist - dist) < BlockOccupyHalf(block.type) + newHalf - 0.01f
			&& std::abs(block.side - side) < 0.51f ) {
			return; // 重なる配置は無視
		}
	}
	data_->blocks.push_back({ rail, dist, level, side, type });
	++blockVersion_;
}

bool RailEditor::RemoveBlock(int rail, float dist, int level, float side){
	int found = FindBlock(rail, dist, level, side);
	if ( found < 0 ) return false;
	data_->blocks.erase(data_->blocks.begin() + found);
	++blockVersion_;
	return true;
}

// ブロックのノード錨の維持。dist（始点からの弧長）は手前のノードを編集すると変わってしまうので、
// 「錨ノード＋そこからの相対距離」を正として dist を追従させる（レール編集でブロックが滑らない）
void RailEditor::UpdateBlockAnchors(const std::vector<SplineRail>& splineRails){
	if ( !data_ || data_->blocks.empty() ) return;
	// レールごとのノード弧長表（必要になったレールだけ作る）
	std::vector<std::vector<float>> nodeArcTable(splineRails.size());
	auto arcsOf = [&](int railIdx) -> const std::vector<float>& {
		auto& arcs = nodeArcTable[railIdx];
		if ( arcs.empty() ) {
			const SplineRail& rail = splineRails[railIdx];
			arcs.reserve(rail.nodes.size());
			for ( const Vector3& node : rail.nodes ) { arcs.push_back(rail.GetClosestDistance(node)); }
		}
		return arcs;
	};
	bool moved = false;
	for ( auto& block : data_->blocks ) {
		if ( block.rail < 0 || block.rail >= ( int ) splineRails.size() ) continue;
		if ( splineRails[block.rail].nodes.size() < 2 ) continue;
		const auto& arcs = arcsOf(block.rail);
		// ノード削除などで番号が範囲外になったら錨を引き直す（現在位置は維持）
		if ( block.anchorNode >= ( int ) arcs.size() ) { block.anchorNode = -1; }
		if ( block.anchorNode < 0 ) {
			// 現在位置の手前にある最後のノードを錨として記録
			int anchor = 0;
			for ( int i = 0; i < ( int ) arcs.size(); ++i ) {
				if ( arcs[i] <= block.dist + 0.01f ) { anchor = i; } else { break; }
			}
			block.anchorNode   = anchor;
			block.anchorOffset = block.dist - arcs[anchor];
		} else {
			// レール編集で弧長が変わっていたら、錨からの相対位置を保って dist を追従
			float newDist = arcs[block.anchorNode] + block.anchorOffset;
			newDist = std::clamp(newDist, 0.0f, splineRails[block.rail].GetLength());
			if ( std::abs(newDist - block.dist) > 0.001f ) { block.dist = newDist; moved = true; }
		}
	}
	if ( moved ) { ++blockVersion_; } // シーン側のブロック再同期を促す
}

// スタンプ（配置待ちシェイプ）を at を原点として新しい路線として設置する。
//   複数レールテンプレート（登り足場など）が待機中なら、全ポリラインを一括で路線化する
void RailEditor::PlaceStamp(const Vector3& at){
	if ( pendingStamp_.empty() && pendingMultiStamp_.empty() ) return;

	std::vector<std::vector<Vector3>> shapes;
	if ( !pendingMultiStamp_.empty() ) { shapes = pendingMultiStamp_; }
	else                               { shapes.push_back(pendingStamp_); }

	int firstNewRail = ( int ) data_->railLines.size();
	// テンプレート（複数レール）は自動で同じグループにまとめる → リスト絞り込み/一括操作しやすい
	std::string autoGroup = ( shapes.size() > 1 ) ? ( "テンプレ" + std::to_string(firstNewRail) ) : "";
	for ( const auto& shape : shapes ) {
		std::vector<Vector3> line = shape;
		for ( auto& n : line ) { n.x += at.x; n.y += at.y; n.z += at.z; }
		AppendRail(std::move(line), autoGroup);
	}
	// 動くテンプレートのメタ設定を適用（可視/道/波形/親子/位相を一括セット。
	// ガイドの相対番号は今置いたレール群の実番号へ変換する）
	if ( !pendingMultiMeta_.empty() && pendingMultiMeta_.size() == shapes.size() ) {
		for ( int i = 0; i < ( int ) shapes.size(); ++i ) {
			int r = firstNewRail + i;
			const StampMeta& meta = pendingMultiMeta_[i];
			data_->railVisible[r]      = meta.visible;
			data_->railRoadModes[r]    = meta.roadMode;
			data_->railLineModes[r]    = meta.lineMode;
			data_->railMotionTypes[r]  = meta.motionType;
			data_->railMotionPhases[r] = meta.phase;
			data_->railMotions[r].w    = meta.period;
			data_->railGuideRails[r]   = ( meta.guideRel >= 0 ) ? ( firstNewRail + meta.guideRel ) : -1;
		}
	}

	SelectWholeRail(firstNewRail); // 置いた直後にギズモで微調整できる（テンプレートは1本目を選択）
	RebuildRailPoints();

	pendingStamp_.clear();
	pendingMultiStamp_.clear();
	pendingMultiMeta_.clear();
}

// 複数選択中のノードを一括削除
void RailEditor::DeleteSelectedNodes(){
	if ( multiSelection_.empty() ) return;

	// 消すたびに後ろのノード番号がズレないよう、rail降順→node降順で消す
	std::vector<NodeRef> refs = multiSelection_;
	std::sort(refs.begin(), refs.end(), [](const NodeRef& a, const NodeRef& b){
		if ( a.rail != b.rail ) return a.rail > b.rail;
		return a.node > b.node;
		});

	for ( const auto& r : refs ) {
		if ( r.rail < 0 || r.rail >= ( int ) data_->railLines.size() ) continue;
		auto& line = data_->railLines[r.rail];
		if ( r.node < 0 || r.node >= ( int ) line.size() ) continue;
		line.erase(line.begin() + r.node);
	}
	multiSelection_.clear();
	selectedRailNode_ = -1;
	++railVersion_;
}

// 路線を複製して選択（少し奥にずらしたコピー）
void RailEditor::DuplicateRail(int railIdx){
	if ( railIdx < 0 || railIdx >= ( int ) data_->railLines.size() ) return;

	std::vector<Vector3> copy = data_->railLines[railIdx];
	for ( auto& n : copy ) { n.z += 2.0f; }
	data_->railLines.push_back(std::move(copy));
	data_->railTypes.push_back(data_->railTypes[railIdx]);
	data_->railMotions.push_back(data_->railMotions[railIdx]);
	data_->railGroundTypes.push_back(( railIdx < ( int ) data_->railGroundTypes.size() ) ? data_->railGroundTypes[railIdx] : 0);
	data_->railVisible.push_back(( railIdx < ( int ) data_->railVisible.size() ) ? data_->railVisible[railIdx] : 1);
	data_->railLineModes.push_back(( railIdx < ( int ) data_->railLineModes.size() ) ? data_->railLineModes[railIdx] : 0);
	data_->railRoadModes.push_back(( railIdx < ( int ) data_->railRoadModes.size() ) ? data_->railRoadModes[railIdx] : 0);
	data_->railEndPlazas.push_back(( railIdx < ( int ) data_->railEndPlazas.size() ) ? data_->railEndPlazas[railIdx] : 0);
	data_->railGuideRails.push_back(( railIdx < ( int ) data_->railGuideRails.size() ) ? data_->railGuideRails[railIdx] : -1);
	data_->railGuideStarts.push_back(( railIdx < ( int ) data_->railGuideStarts.size() ) ? data_->railGuideStarts[railIdx] : 0.0f);
	data_->railGuideEnds.push_back(( railIdx < ( int ) data_->railGuideEnds.size() ) ? data_->railGuideEnds[railIdx] : -1.0f);
	data_->railGuideModes.push_back(( railIdx < ( int ) data_->railGuideModes.size() ) ? data_->railGuideModes[railIdx] : 0);
	data_->railGuideAligns.push_back(( railIdx < ( int ) data_->railGuideAligns.size() ) ? data_->railGuideAligns[railIdx] : 0);
	data_->railGuideDwells.push_back(( railIdx < ( int ) data_->railGuideDwells.size() ) ? data_->railGuideDwells[railIdx] : 0.0f);
	data_->railGroups.push_back(( railIdx < ( int ) data_->railGroups.size() ) ? data_->railGroups[railIdx] : "");
	if ( railIdx < ( int ) data_->railNodeHoles.size() ) data_->railNodeHoles.push_back(data_->railNodeHoles[railIdx]);
	else                                                 data_->railNodeHoles.push_back(std::vector<int>(data_->railLines.back().size(), 0));
	data_->railMotionTypes.push_back(( railIdx < ( int ) data_->railMotionTypes.size() ) ? data_->railMotionTypes[railIdx] : 0);
	data_->railMotionPhases.push_back(( railIdx < ( int ) data_->railMotionPhases.size() ) ? data_->railMotionPhases[railIdx] : 0.0f);
	data_->railMotionTriggers.push_back(( railIdx < ( int ) data_->railMotionTriggers.size() ) ? data_->railMotionTriggers[railIdx] : 0);
	data_->railAppearTriggers.push_back(( railIdx < ( int ) data_->railAppearTriggers.size() ) ? data_->railAppearTriggers[railIdx] : -1);
	data_->railOneWay.push_back(( railIdx < ( int ) data_->railOneWay.size() ) ? data_->railOneWay[railIdx] : 0);
	data_->railSpeedMuls.push_back(( railIdx < ( int ) data_->railSpeedMuls.size() ) ? data_->railSpeedMuls[railIdx] : 1.0f);
	SelectWholeRail(( int ) data_->railLines.size() - 1);
	RebuildRailPoints();
}

// 近いレール同士を連結する（端点 → 他レール本体の最近点へ寄せて共有ノードを挿入）。
void RailEditor::ConnectNearbyLines(){
	const float kReach = 1.2f;  // この距離以内なら連結（ランタイムの自動合流と同じ）
	const float kSnapNode = 0.25f; // 最近点が既存ノードにこれだけ近ければ新ノードを足さず共有
	auto& lines = data_->railLines;

	auto dist3 = [](const Vector3& p, const Vector3& q) -> float{
		float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
		};

	bool changed = false;
	for ( size_t a = 0; a < lines.size(); ++a ) {
		if ( lines[a].size() < 2 ) continue;
		for ( int endSel = 0; endSel < 2; ++endSel ) {
			Vector3 ep = ( endSel == 0 ) ? lines[a].front() : lines[a].back();

			int   bRail = -1, bSeg = -1;
			float bDist = kReach;
			Vector3 bPt { 0.0f, 0.0f, 0.0f };

			for ( size_t b = 0; b < lines.size(); ++b ) {
				if ( b == a ) continue;
				if ( lines[b].size() < 2 ) continue;
				for ( size_t s = 0; s + 1 < lines[b].size(); ++s ) {
					const Vector3& p0 = lines[b][s];
					const Vector3& p1 = lines[b][s + 1];
					Vector3 d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
					float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
					float t = ( len2 > 1e-6f )
						? ( ( ep.x - p0.x ) * d.x + ( ep.y - p0.y ) * d.y + ( ep.z - p0.z ) * d.z ) / len2
						: 0.0f;
					t = std::clamp(t, 0.0f, 1.0f);
					Vector3 cp = { p0.x + d.x * t, p0.y + d.y * t, p0.z + d.z * t };
					float dd = dist3(cp, ep);
					if ( dd < bDist ) { bDist = dd; bRail = ( int ) b; bSeg = ( int ) s; bPt = cp; }
				}
			}
			if ( bRail < 0 ) continue;
			if ( bDist < 1e-4f ) continue; // 既にピッタリ重なっている

			// 相手レールの最近点が既存ノードに近ければそれを共有点に、なければ挿入する
			auto& tline = lines[bRail];
			Vector3 shared = bPt;
			float dToS  = dist3(bPt, tline[bSeg]);
			float dToS1 = dist3(bPt, tline[bSeg + 1]);
			if ( dToS <= kSnapNode ) {
				shared = tline[bSeg];
			} else if ( dToS1 <= kSnapNode ) {
				shared = tline[bSeg + 1];
			} else {
				tline.insert(tline.begin() + bSeg + 1, bPt); // 線の途中に共有ノードを足す
				// 穴配列も同じ位置へ挿入（同期しないと既存の穴指定が後ろへズレる）
				if ( bRail < ( int ) data_->railNodeHoles.size() ) {
					auto& holes = data_->railNodeHoles[bRail];
					holes.resize(tline.size() - 1, 0);
					int at = std::clamp(bSeg + 1, 0, ( int ) holes.size());
					holes.insert(holes.begin() + at, 0);
				}
			}

			// 自分の端点をその共有点へ寄せる
			if ( endSel == 0 ) lines[a].front() = shared;
			else               lines[a].back()  = shared;
			changed = true;
		}
	}

	if ( changed ) {
		multiSelection_.clear();
		selectedRailNode_ = -1;
		RebuildRailPoints();
	}
}

// ============================================================
// 自動スナップ接続（仕様書_自動レール接続 §1）
// ============================================================

// rail の端点(front/back)から radius 以内の接続候補を探す（変更はしない）。
//   (a) 他レールの端点（→溶接） (b) 他レール本体の最近点（→T字連結）。両方あれば近い方を優先
RailEditor::SnapTarget RailEditor::FindSnapTarget(int rail, bool front, float radius) const{
	SnapTarget best;
	const auto& lines = data_->railLines;
	if ( rail < 0 || rail >= ( int ) lines.size() || lines[rail].size() < 2 ) return best;
	Vector3 ep = front ? lines[rail].front() : lines[rail].back();

	auto dist3 = [](const Vector3& p, const Vector3& q) -> float{
		float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
		};

	float bestDist = radius;
	for ( size_t b = 0; b < lines.size(); ++b ) {
		if ( ( int ) b == rail ) continue;
		if ( lines[b].size() < 2 ) continue;

		// (a) 端点同士（溶接候補）
		const int endNodes[2] = { 0, ( int ) lines[b].size() - 1 };
		for ( int en : endNodes ) {
			float dd = dist3(lines[b][en], ep);
			if ( dd < bestDist ) {
				bestDist = dd;
				best.valid = true;
				best.isEndpoint = true;
				best.rail = ( int ) b;
				best.node = en;
				best.seg = -1;
				best.pos = lines[b][en];
			}
		}

		// (b) 本体の最近点（T字連結候補。ConnectNearbyLines と同じ線分投影）
		for ( size_t s = 0; s + 1 < lines[b].size(); ++s ) {
			const Vector3& p0 = lines[b][s];
			const Vector3& p1 = lines[b][s + 1];
			Vector3 d = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
			float len2 = d.x * d.x + d.y * d.y + d.z * d.z;
			float t = ( len2 > 1e-6f )
				? ( ( ep.x - p0.x ) * d.x + ( ep.y - p0.y ) * d.y + ( ep.z - p0.z ) * d.z ) / len2
				: 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			Vector3 cp = { p0.x + d.x * t, p0.y + d.y * t, p0.z + d.z * t };
			float dd = dist3(cp, ep);
			if ( dd < bestDist ) {
				bestDist = dd;
				best.valid = true;
				best.isEndpoint = false;
				best.rail = ( int ) b;
				best.node = -1;
				best.seg = ( int ) s;
				best.pos = cp;
			}
		}
	}
	return best;
}

// FindSnapTarget の XZ 版：真上から見た距離だけで候補を探す。
//   3D距離の FindSnapTarget では「位置は重ねたのに高さだけ 1.2m 以上ズレている」相手を
//   拾えず黙って失敗する。こちらは高さ差を maxYDiff まで許容して拾い、
//   返す pos は相手側の点なので ConnectToTarget へ渡せば端点のYも相手に一致する。
RailEditor::SnapTarget RailEditor::FindSnapTargetXZ(int rail, bool front, float radiusXZ, float maxYDiff) const{
	SnapTarget best;
	const auto& lines = data_->railLines;
	if ( rail < 0 || rail >= ( int ) lines.size() || lines[rail].size() < 2 ) return best;
	Vector3 ep = front ? lines[rail].front() : lines[rail].back();

	auto distXZ = [](const Vector3& p, const Vector3& q) -> float{
		float dx = p.x - q.x, dz = p.z - q.z;
		return std::sqrt(dx * dx + dz * dz);
		};

	// 採用条件は「XZ距離が radiusXZ 未満」。順位は「XZ距離＋高さ差ペナルティ」で付ける。
	//   同じくらい近い相手が上下に複数いる時、XZ距離だけだと 0.2m 先の 4.5m 下を
	//   1.0m 先の 1.3m 下より優先してしまう＝立体交差エリアで意図しない相手に繋がるため
	const float kHeightPenalty = 0.3f;
	float bestScore = 3.4e38f;
	for ( size_t b = 0; b < lines.size(); ++b ) {
		if ( ( int ) b == rail ) continue;
		if ( lines[b].size() < 2 ) continue;

		// (a) 端点同士（溶接候補）
		const int endNodes[2] = { 0, ( int ) lines[b].size() - 1 };
		for ( int en : endNodes ) {
			float dyAbs = std::abs(lines[b][en].y - ep.y);
			if ( dyAbs > maxYDiff ) continue;
			float dd = distXZ(lines[b][en], ep);
			if ( dd >= radiusXZ ) continue;
			float score = dd + dyAbs * kHeightPenalty;
			if ( score < bestScore ) {
				bestScore = score;
				best.valid = true;
				best.isEndpoint = true;
				best.rail = ( int ) b;
				best.node = en;
				best.seg = -1;
				best.pos = lines[b][en];
			}
		}

		// (b) 本体の最近点（T字連結候補）：XZ 平面で線分へ投影し、Yは線分上の高さを使う
		for ( size_t s = 0; s + 1 < lines[b].size(); ++s ) {
			const Vector3& p0 = lines[b][s];
			const Vector3& p1 = lines[b][s + 1];
			float dx = p1.x - p0.x, dz = p1.z - p0.z;
			float len2 = dx * dx + dz * dz;
			float t = ( len2 > 1e-6f )
				? ( ( ep.x - p0.x ) * dx + ( ep.z - p0.z ) * dz ) / len2
				: 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			Vector3 cp = { p0.x + ( p1.x - p0.x ) * t,
			               p0.y + ( p1.y - p0.y ) * t,
			               p0.z + ( p1.z - p0.z ) * t };
			float dyAbs = std::abs(cp.y - ep.y);
			if ( dyAbs > maxYDiff ) continue;
			float dd = distXZ(cp, ep);
			if ( dd >= radiusXZ ) continue;
			float score = dd + dyAbs * kHeightPenalty;
			if ( score < bestScore ) {
				bestScore = score;
				best.valid = true;
				best.isEndpoint = false;
				best.rail = ( int ) b;
				best.node = -1;
				best.seg = ( int ) s;
				best.pos = cp;
			}
		}
	}
	return best;
}

// スナップ候補へ実際に接続する（ドロップ確定）。
//   端点同士→ドラッグ中の端点を相手の位置へ吸着（相手は動かさない＝片側スナップ）
//   本体の途中→相手レールへ共有ノードを挿入して自分の端点を寄せる（ConnectNearbyLines と同じ）
void RailEditor::ConnectToTarget(int rail, bool front, const SnapTarget& target){
	if ( !target.valid ) return;
	auto& lines = data_->railLines;
	if ( rail < 0 || rail >= ( int ) lines.size() || lines[rail].size() < 2 ) return;
	if ( target.rail < 0 || target.rail >= ( int ) lines.size() ) return;

	Vector3 shared = target.pos;

	if ( !target.isEndpoint ) {
		// 相手レールの最近点が既存ノードに近ければそれを共有点に、なければ挿入する
		const float kSnapNode = 0.25f; // ConnectNearbyLines と同じ
		auto& tline = lines[target.rail];
		int seg = std::clamp(target.seg, 0, ( int ) tline.size() - 2);
		auto dist3 = [](const Vector3& p, const Vector3& q) -> float{
			float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
			};
		if ( dist3(target.pos, tline[seg]) <= kSnapNode ) {
			shared = tline[seg];
		} else if ( dist3(target.pos, tline[seg + 1]) <= kSnapNode ) {
			shared = tline[seg + 1];
		} else {
			tline.insert(tline.begin() + seg + 1, target.pos);
			// 穴配列も同じ位置へ挿入（同期しないと既存の穴指定が後ろへズレる）
			if ( target.rail < ( int ) data_->railNodeHoles.size() ) {
				auto& holes = data_->railNodeHoles[target.rail];
				holes.resize(tline.size() - 1, 0); // 念のため挿入前サイズへ整える
				int at = std::clamp(seg + 1, 0, ( int ) holes.size());
				holes.insert(holes.begin() + at, 0);
			}
		}
	}

	// 自分の端点を共有点へ吸着
	if ( front ) lines[rail].front() = shared;
	else         lines[rail].back()  = shared;

	RebuildRailPoints(); // 世代を進めて道・マーカー・接続の再構築を促す
}

bool RailEditor::GetNodePosOf(int rail, int node, Vector3& out) const{
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return false;
	const auto& line = data_->railLines[rail];
	if ( node < 0 || node >= ( int ) line.size() ) return false;
	out = line[node];
	return true;
}

bool RailEditor::IsNodeHole(int rail, int node) const{
	if ( rail < 0 || rail >= ( int ) data_->railNodeHoles.size() ) return false;
	const auto& h = data_->railNodeHoles[rail];
	return node >= 0 && node < ( int ) h.size() && h[node] != 0;
}

bool RailEditor::IsRailVisible(int rail) const{
	if ( rail < 0 || rail >= ( int ) data_->railVisible.size() ) return true; // 未設定は表示扱い
	return data_->railVisible[rail] != 0;
}

// 表示用：横(0)/縦(1)。railTypes が -1(自動)なら front→back の主軸で判定（実装と同じ1.5バイアス）
int RailEditor::GetRailDisplayType(int rail) const{
	if ( rail < 0 || rail >= ( int ) data_->railLines.size() ) return 0;
	if ( rail < ( int ) data_->railTypes.size() ) {
		int t = data_->railTypes[rail];
		if ( t == 0 ) return 0;
		if ( t == 1 ) return 1;
	}
	const auto& nodes = data_->railLines[rail];
	if ( nodes.size() < 2 ) return 0;
	float dx = std::abs(nodes.back().x - nodes.front().x);
	float dz = std::abs(nodes.back().z - nodes.front().z);
	return ( dz > dx * 1.5f ) ? 1 : 0;
}

// afterIndex の直後にノードを挿入
void RailEditor::InsertRailNode(int afterIndex, const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];
	int insertAt = afterIndex + 1;
	if ( insertAt < 0 ) insertAt = 0;
	if ( insertAt > ( int ) line.size() ) insertAt = ( int ) line.size();
	// 穴配列も同じ位置へ挿入（同期しないと既存の穴指定が後ろへズレるバグになる）
	if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
		auto& h = data_->railNodeHoles[currentEditRailIndex_];
		h.resize(line.size(), 0);
		h.insert(h.begin() + insertAt, 0);
	}
	line.insert(line.begin() + insertAt, p);
	selectedRailNode_ = insertAt;
	// ノード番号がずれるので選択を挿入ノードだけに引き直す
	multiSelection_.clear();
	multiSelection_.push_back({ currentEditRailIndex_, insertAt });
	++railVersion_;
}

// 指定ノードを削除
void RailEditor::DeleteRailNode(int idx){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) data_->railLines.size() ) return;
	auto& line = data_->railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return;
	line.erase(line.begin() + idx);
	selectedRailNode_ = -1;
	multiSelection_.clear(); // ノード番号がずれるので選択を解除
	++railVersion_;
}

// ============================================================
// Undo / Redo
// ============================================================
void RailEditor::RestoreSnapshot(const RailSnapshot& s){
	data_->railLines = s.lines;
	data_->railTypes = s.types;
	if ( !s.motions.empty() ) { data_->railMotions = s.motions; } // 動くレール設定も復元
	data_->coins = s.coins; // コイン配置も復元（レール削除のUndoでrail番号がズレないように）
	data_->blocks = s.blocks; // ブロック配置も復元
	++blockVersion_;          // ゲーム側にブロック作り直しを通知
	multiSelection_.clear(); // ノード構成が変わるので選択を解除
	if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
	}
	if ( currentEditRailIndex_ < 0 ) currentEditRailIndex_ = 0;
	selectedRailNode_ = -1;
	committed_.lines   = data_->railLines; // 復元直後を基準に
	committed_.types   = data_->railTypes;
	committed_.motions = data_->railMotions;
	committed_.coins   = data_->coins;
	committed_.blocks  = data_->blocks;
	++railVersion_;
}

// マウス非操作の瞬間に、前回チェックポイントとの差分があれば履歴へ積む
void RailEditor::CommitIfStable(){
	Input* input = Input::GetInstance();
	// ドラッグ中(左/右ボタン押下中)はまだ確定させない（一連の操作を1ステップにまとめる）
	if ( input->PushMouseButton(0) || input->PushMouseButton(1) ) return;

	if ( !committedInit_ ) {
		committed_.lines   = data_->railLines;
		committed_.types   = data_->railTypes;
		committed_.motions = data_->railMotions;
		committed_.coins   = data_->coins;
		committed_.blocks  = data_->blocks;
		committedInit_ = true;
		return;
	}

	// 変化していなければ何もしない（配置・タイプ・動くレール設定・コイン・ブロックのいずれかが変わったら記録）
	if ( RailLinesEqual(committed_.lines, data_->railLines)
		&& committed_.types == data_->railTypes
		&& MotionsEqual(committed_.motions, data_->railMotions)
		&& committed_.coins == data_->coins
		&& committed_.blocks == data_->blocks ) return;

	// 直前の安定状態を undo へ積み、現在を新しいチェックポイントに
	undoStack_.push_back(committed_);
	if ( undoStack_.size() > 100 ) undoStack_.erase(undoStack_.begin());
	redoStack_.clear();
	committed_.lines   = data_->railLines;
	committed_.types   = data_->railTypes;
	committed_.motions = data_->railMotions;
	committed_.coins   = data_->coins;
	committed_.blocks  = data_->blocks;
}

void RailEditor::Undo(){
	if ( undoStack_.empty() ) return;
	// 現在をredoへ
	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions; cur.coins = data_->coins; cur.blocks = data_->blocks;
	redoStack_.push_back(cur);
	RailSnapshot prev = undoStack_.back();
	undoStack_.pop_back();
	RestoreSnapshot(prev);
}

void RailEditor::Redo(){
	if ( redoStack_.empty() ) return;
	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions; cur.coins = data_->coins; cur.blocks = data_->blocks;
	undoStack_.push_back(cur);
	RailSnapshot next = redoStack_.back();
	redoStack_.pop_back();
	RestoreSnapshot(next);
}

// 「最初期に戻す」が意味を持つか（初期状態と今が違う時だけ true）
bool RailEditor::CanResetToInitial() const{
	if ( !hasInitial_ ) return false;
	return !( RailLinesEqual(initialLines_, data_->railLines)
		&& initialTypes_ == data_->railTypes
		&& MotionsEqual(initialMotions_, data_->railMotions)
		&& initialCoins_ == data_->coins
		&& initialBlocks_ == data_->blocks );
}

// 編集開始時（マップ読込直後）の状態へ一発で戻す。
//   現在の状態を undo に積んでから戻すので、Ctrl+Z で元の作業に復帰できる。
void RailEditor::ResetToInitial(){
	if ( !CanResetToInitial() ) return;

	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions; cur.coins = data_->coins; cur.blocks = data_->blocks;
	undoStack_.push_back(cur);
	if ( undoStack_.size() > 100 ) undoStack_.erase(undoStack_.begin());
	redoStack_.clear();

	data_->railLines   = initialLines_;
	data_->railTypes   = initialTypes_;
	data_->railMotions = initialMotions_; // 動くレール設定も初期へ戻す
	data_->coins       = initialCoins_;   // コイン配置も初期へ戻す
	data_->blocks      = initialBlocks_;  // ブロック配置も初期へ戻す
	++blockVersion_;
	multiSelection_.clear();
	selectedRailNode_ = -1;
	if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
	}
	if ( currentEditRailIndex_ < 0 ) currentEditRailIndex_ = 0;

	committed_.lines   = data_->railLines; // 戻した直後を基準に
	committed_.types   = data_->railTypes;
	committed_.motions = data_->railMotions; // これを忘れると直後に偽のUndo履歴が1つ積まれる
	committed_.coins   = data_->coins;
	committed_.blocks  = data_->blocks;
	++railVersion_;
}

// =====================================================================
//  レール編集ウィンドウ（管理 / 作成 タブ）
// =====================================================================
// スタート→ゴールの到達チェック：
//   辺 = 接続（溶接/T字/交差）＋「ジャンプ想定」（Gapレールの未接続の端から跳んで他レールに着地）。
//   到達できれば経路（ジャンプ弧を含む）をワールドポリライン化する（表示とゴースト走行に使う）
void RailEditor::BuildRouteCheck(){
	routePoints_.clear();
	routeJumpFlags_.clear();
	routeCum_.clear();
	routeChecked_ = true;
	routeReachable_ = false;
	routeViaCount_ = 0;
	ghostDist_ = 0.0f;
	lastRouteVersion_ = railVersion_;
	int n = ( int ) data_->railLines.size();
	int startRail = data_->startRailIndex;
	int goalRail  = data_->goalRailIndex;
	if ( startRail < 0 || startRail >= n || goalRail < 0 || goalRail >= n ) return;
	if ( data_->railLines[startRail].empty() || data_->railLines[goalRail].empty() ) return;

	// --- ジャンプ辺の事前計算：Gapレールの未接続の端から弾道を飛ばし、着地できる相手を探す ---
	//   物理はプレイヤーと同じ想定（移動5m/s・ジャンプ初速8m/s・重力25m/s²。Player.h と揃える）
	struct JumpEdge { int from = -1, to = -1; bool flutter = false; std::vector<Vector3> arc; };
	std::vector<JumpEdge> jumpEdges;
	{
		const float kRunSpeed = 5.0f, kJumpV = 8.0f, kGravity = 25.0f;
		const float kFloatTarget = 1.2f, kFloatTime = 0.6f, kFloatEase = 6.0f; // ふんばり（Player と同じ値）
		auto segDistSq = [](const Vector3& p, const Vector3& a, const Vector3& b) -> float{
			Vector3 ab = { b.x - a.x, b.y - a.y, b.z - a.z };
			float len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
			float t = ( len2 > 1e-6f )
				? ( ( p.x - a.x ) * ab.x + ( p.y - a.y ) * ab.y + ( p.z - a.z ) * ab.z ) / len2 : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			Vector3 c = { a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t };
			float dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
			return dx * dx + dy * dy + dz * dz;
		};
		for ( int r = 0; r < n; ++r ) {
			if ( r >= ( int ) data_->railGroundTypes.size() || data_->railGroundTypes[r] != 1 ) continue; // Gapだけ飛び出せる
			const auto& line = data_->railLines[r];
			if ( line.size() < 2 ) continue;
			for ( int side = 0; side < 2; ++side ) {
				bool front = ( side == 0 );
				if ( IsRailEndConnected(r, front) ) continue;
				Vector3 endP  = front ? line.front() : line.back();
				Vector3 inner = front ? line[1] : line[line.size() - 2];
				Vector3 dir = { endP.x - inner.x, 0.0f, endP.z - inner.z };
				float dirLen = std::sqrt(dir.x * dir.x + dir.z * dir.z);
				if ( dirLen < 1e-4f ) continue;
				dir = { dir.x / dirLen, 0.0f, dir.z / dirLen };
				// 弾道シミュレーション：まず通常ジャンプ、それで届かなければ「ふんばり込み」で再挑戦
				auto simulateJump = [&](bool useFlutter) -> JumpEdge{
					JumpEdge edge; edge.from = r; edge.flutter = useFlutter;
					const float dt = 0.05f;
					float posY = endP.y, dist = 0.0f, vy = kJumpV, flutterBudget = kFloatTime;
					for ( float t = 0.0f; t <= ( useFlutter ? 3.5f : 2.5f ); t += dt ) {
						// ふんばり：上昇が弱まったら弱い浮力で粘る（ゲームの kFloatTarget/kFloatEase と同じ式）
						if ( useFlutter && vy < kFloatTarget && flutterBudget > 0.0f ) {
							vy += ( kFloatTarget - vy ) * ( std::min )( kFloatEase * dt, 1.0f );
							flutterBudget -= dt;
						} else {
							vy -= kGravity * dt;
						}
						posY += vy * dt;
						dist += kRunSpeed * dt;
						Vector3 p = { endP.x + dir.x * dist, posY, endP.z + dir.z * dist };
						edge.arc.push_back(p);
						if ( vy >= 0.0f ) continue; // 上昇/滞空中は着地判定しない
						for ( int o = 0; o < n && edge.to < 0; ++o ) {
							if ( o == r || data_->railLines[o].size() < 2 ) continue;
							const auto& otherLine = data_->railLines[o];
							for ( size_t s2 = 0; s2 + 1 < otherLine.size(); ++s2 ) {
								if ( segDistSq(p, otherLine[s2], otherLine[s2 + 1]) < 0.8f * 0.8f ) { edge.to = o; break; }
							}
						}
						if ( edge.to >= 0 ) break;
						if ( posY < endP.y - 15.0f ) break; // 深く落ちたら届かない
					}
					return edge;
				};
				JumpEdge edge = simulateJump(false);
				if ( edge.to < 0 ) { edge = simulateJump(true); } // 通常で届かない→ふんばり込みで再挑戦
				if ( edge.to >= 0 && edge.to != r ) { jumpEdges.push_back(std::move(edge)); }
			}
		}
	}

	// --- BFS（接続＋ジャンプ辺）---
	std::vector<int> parent(n, -2);
	std::vector<int> parentJump(n, -1); // そのレールへ「どのジャンプ辺」で来たか（-1=接続で来た）
	parent[startRail] = -1;
	std::vector<int> bfsQueue { startRail };
	for ( size_t qi = 0; qi < bfsQueue.size(); ++qi ) {
		int cur = bfsQueue[qi];
		if ( cur == goalRail ) break;
		for ( const auto& conn : GetRailConnections(cur) ) {
			int next = conn.otherRail;
			if ( next < 0 || next >= n || parent[next] != -2 ) continue;
			parent[next] = cur;
			bfsQueue.push_back(next);
		}
		for ( int je = 0; je < ( int ) jumpEdges.size(); ++je ) {
			if ( jumpEdges[je].from != cur ) continue;
			int next = jumpEdges[je].to;
			if ( parent[next] != -2 ) continue;
			parent[next] = cur;
			parentJump[next] = je;
			bfsQueue.push_back(next);
		}
	}
	if ( parent[goalRail] == -2 ) return; // 到達不可

	routeReachable_ = true;
	std::vector<int> path;
	for ( int r = goalRail; r != -1; r = parent[r] ) { path.push_back(r); }
	std::reverse(path.begin(), path.end());
	routeViaCount_ = ( int ) path.size();

	// レール列 → ワールドポリライン（レール上はノードを辿り、ジャンプ区間は弾道の弧を挿入）
	auto nearestNode = [&](int rail, const Vector3& p) -> int{
		int best = 0; float bestD = 1e30f;
		for ( int k = 0; k < ( int ) data_->railLines[rail].size(); ++k ) {
			const Vector3& q = data_->railLines[rail][k];
			float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
			float d = dx * dx + dy * dy + dz * dz;
			if ( d < bestD ) { bestD = d; best = k; }
		}
		return best;
	};
	auto connPosWith = [&](int rail, int other, Vector3& out) -> bool{
		for ( const auto& conn : GetRailConnections(rail) ) {
			if ( conn.otherRail == other ) { out = conn.pos; return true; }
		}
		return false;
	};
	Vector3 startPos = data_->railLines[startRail][std::clamp(data_->startNodeIndex, 0, ( int ) data_->railLines[startRail].size() - 1)];
	Vector3 goalPos  = data_->railLines[goalRail][std::clamp(data_->goalNodeIndex, 0, ( int ) data_->railLines[goalRail].size() - 1)];
	for ( int pi = 0; pi < ( int ) path.size(); ++pi ) {
		int rail = path[pi];
		// 入口：ジャンプで来たなら着地点、接続なら接続点、先頭ならスタートノード
		Vector3 entryPos = startPos;
		if ( pi > 0 ) {
			int cameByJump = parentJump[rail];
			if ( cameByJump >= 0 ) { entryPos = jumpEdges[cameByJump].arc.back(); }
			else { connPosWith(rail, path[pi - 1], entryPos); }
		}
		// 出口：次へジャンプで行くなら跳び出しの端、接続なら接続点、最後ならゴールノード
		Vector3 exitPos = goalPos;
		int exitJump = -1;
		if ( pi + 1 < ( int ) path.size() ) {
			int nextJump = parentJump[path[pi + 1]];
			if ( nextJump >= 0 && jumpEdges[nextJump].from == rail ) {
				exitJump = nextJump;
				exitPos = jumpEdges[nextJump].arc.front();
			} else {
				connPosWith(rail, path[pi + 1], exitPos);
			}
		}
		int i0 = nearestNode(rail, entryPos);
		int i1 = nearestNode(rail, exitPos);
		int step = ( i1 >= i0 ) ? 1 : -1;
		for ( int k = i0; ; k += step ) {
			routePoints_.push_back(data_->railLines[rail][k]);
			routeJumpFlags_.push_back(0);
			if ( k == i1 ) break;
		}
		if ( exitJump >= 0 ) { // ジャンプ弧（シアン=通常 / マゼンタ=ふんばり必要。ゴーストも跳ぶ）
			char jumpFlag = jumpEdges[exitJump].flutter ? 2 : 1;
			for ( const auto& arcPoint : jumpEdges[exitJump].arc ) {
				routePoints_.push_back(arcPoint);
				routeJumpFlags_.push_back(jumpFlag);
			}
		}
	}
	routeCum_.resize(routePoints_.size(), 0.0f);
	for ( size_t k = 1; k < routePoints_.size(); ++k ) {
		Vector3 d = { routePoints_[k].x - routePoints_[k - 1].x,
		              routePoints_[k].y - routePoints_[k - 1].y,
		              routePoints_[k].z - routePoints_[k - 1].z };
		routeCum_[k] = routeCum_[k - 1] + std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	}
}

// ゴースト走行の現在位置（経路の累積距離からセグメント内を線形補間）
bool RailEditor::GetGhostPos(Vector3& out) const{
	if ( !ghostRun_ || routePoints_.size() < 2 || routeCum_.empty() ) return false;
	float total = routeCum_.back();
	if ( total <= 0.01f ) return false;
	float d = std::fmod(ghostDist_, total);
	for ( size_t k = 1; k < routePoints_.size(); ++k ) {
		if ( d <= routeCum_[k] ) {
			float seg = routeCum_[k] - routeCum_[k - 1];
			float t = ( seg > 1e-4f ) ? ( d - routeCum_[k - 1] ) / seg : 0.0f;
			out = { routePoints_[k - 1].x + ( routePoints_[k].x - routePoints_[k - 1].x ) * t,
			        routePoints_[k - 1].y + ( routePoints_[k].y - routePoints_[k - 1].y ) * t,
			        routePoints_[k - 1].z + ( routePoints_[k].z - routePoints_[k - 1].z ) * t };
			return true;
		}
	}
	out = routePoints_.back();
	return true;
}

// レール編集の毎フレーム処理（配列同期・Undo確定・Ctrl+Z/Y・ゴースト時間など）。
//   ウィンドウの表示とは独立して毎フレーム呼ぶこと（アイコンモードでレールエディタを
//   閉じていても Undo やゴーストが止まらないように、UI描画から分離した）
void RailEditor::TickEditing(){
	// 全設定配列をレール数へ一元同期（各UIブロックに散らばっていた resize の置き換え。
	// これ以降のコードは「配列はレール数と同じ」を前提にしてよい）
	SyncRailArraySizes();
	// ゴースト走行の時間を進める（エディタ中のプレイヤー挙動プレビュー）
	if ( ghostRun_ ) { ghostDist_ += ghostSpeed_ * ImGui::GetIO().DeltaTime; }
	// 自動再チェック：レールを編集したら経路を作り直す（毎回ボタンを押す手間をなくす）
	if ( routeChecked_ && routeAutoRecheck_ && lastRouteVersion_ != railVersion_ ) { BuildRouteCheck(); }
	hoveredListRail_ = -1; // リストのホバーは毎フレーム取り直す
	hoveredListRailB_ = -1;
	hoveredConnValid_ = false;
#ifdef USE_IMGUI
    // Undo/Redo：マウス非操作の瞬間に自動チェックポイント＋ Ctrl+Z / Ctrl+Y
    CommitIfStable();
    {
        Input* in = Input::GetInstance();
        bool ctrl = in->Pushkey(DIK_LCONTROL) || in->Pushkey(DIK_RCONTROL);
        if ( ctrl && in->Triggerkey(DIK_Z) ) Undo();
        if ( ctrl && in->Triggerkey(DIK_Y) ) Redo();
    }
#endif
}

void RailEditor::DrawWindow(){
#ifdef USE_IMGUI
	// =========================================================
	//  5. レールエディタ ウィンドウ（1つのウィンドウ内をタブで分割）
	//     「管理」タブ … 既存レールの一覧・プロパティ・座標
	//     「作成」タブ … 新しいレールを作る（シェイプ生成・マウス描画）
	// =========================================================
	ImGui::Begin("レールエディタ");
	if ( ImGui::BeginTabBar("RailEditorTabs") ) {
	if ( ImGui::BeginTabItem("管理 (Rails)") ) {

	// railTypes / railMotions / railGroundTypes を railLines と必ず同数に保つ
	if ( data_->railMotions.size() != data_->railLines.size() ) {
		data_->railMotions.resize(data_->railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	}
	if ( data_->railGroundTypes.size() != data_->railLines.size() ) {
		data_->railGroundTypes.resize(data_->railLines.size(), 1); // 既定は Gap
	}
	if ( data_->railVisible.size() != data_->railLines.size() ) {
		data_->railVisible.resize(data_->railLines.size(), 1);
	}
	if ( data_->railLineModes.size() != data_->railLines.size() ) {
		data_->railLineModes.resize(data_->railLines.size(), 0);
	}
	if ( data_->railRoadModes.size() != data_->railLines.size() ) {
		data_->railRoadModes.resize(data_->railLines.size(), 0);
	}
	if ( data_->railEndPlazas.size() != data_->railLines.size() ) {
		data_->railEndPlazas.resize(data_->railLines.size(), 0);
	}
	if ( data_->railGuideRails.size() != data_->railLines.size() ) {
		data_->railGuideRails.resize(data_->railLines.size(), -1);
	}
	if ( data_->railTypes.size() != data_->railLines.size() ) {
		data_->railTypes.resize(data_->railLines.size(), -1);
	}
	if ( data_->railNodeHoles.size() != data_->railLines.size() ) {
		data_->railNodeHoles.resize(data_->railLines.size());
	}

	// --- 検証（到達チェック・ゴースト走行・表示トグル）。よく使うので管理タブの先頭に置く ---
	if ( ImGui::CollapsingHeader("検証（到達チェック・プレビュー）", ImGuiTreeNodeFlags_DefaultOpen) ) {
		ImGui::Checkbox("ジャンプ予測線を表示", &railReachLines_);
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("プレイヤーのジャンプ物理の弾道線（Gapの端から出る点線）。\n多くて見づらい時はOFF、飛び移り距離を確認したい時だけON");
		}
		ImGui::SameLine();
		ImGui::Checkbox("動きをプレビュー", &railMotionPreview_);
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("動くレール（親子付けリフト含む）をエディタ中も再生する。\nPlayを押さなくても、リフトの組み方が合っているかその場で確認できる");
		}
		if ( ImGui::Button("到達チェック＆経路表示") ) { BuildRouteCheck(); }
		ImGui::SameLine();
		if ( ImGui::Button("経路をクリア") ) {
			routePoints_.clear(); routeCum_.clear();
			routeChecked_ = false; ghostRun_ = false;
		}
		if ( routeChecked_ ) {
			if ( data_->goalRailIndex < 0 ) {
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ゴールが未設定です（下の「選択ノードをゴールに」で設定）");
			} else if ( routeReachable_ ) {
				float routeLength = routeCum_.empty() ? 0.0f : routeCum_.back();
				bool hasJump = false, hasFlutter = false;
				for ( char f : routeJumpFlags_ ) {
					if ( f == 1 ) { hasJump = true; }
					else if ( f == 2 ) { hasFlutter = true; }
				}
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f),
					"OK: ゴールへ到達できます（経由 %d 本 / 約%.0fm / 走って約%.1f秒%s%s）",
					routeViaCount_, routeLength, routeLength / ( std::max )( ghostSpeed_, 0.1f ),
					hasJump ? " / ジャンプあり" : "",
					hasFlutter ? " / ふんばり必要(マゼンタ区間)" : "");
				ImGui::Checkbox("ゴースト走行（プレイヤー速度で経路を走らせる）", &ghostRun_);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90.0f);
				ImGui::DragFloat("速度##ghost", &ghostSpeed_, 0.5f, 1.0f, 15.0f, "%.1fm/s");
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "NG: ゴールへ到達できません（接続タブで未接続の端を確認）");
			}
			ImGui::Checkbox("編集したら自動で再チェック", &routeAutoRecheck_);
		}
		ImGui::Separator();
	}

	// --- 路線リスト（クリック＝路線まるごと選択 / 複製 / 削除）---
	ImGui::Text("路線リスト:");
	ImGui::SameLine();
	ImGui::TextDisabled("(行を右クリック＝グループへ分類)");
	// グループ絞り込み：レールが増えてもリストを管理できるように、グループ単位で表示を絞れる
	data_->railGroups.resize(data_->railLines.size());
	static std::string listGroupFilter; // 空=すべて表示
	{
		std::vector<std::string> groupNames;
		for ( const auto& groupName : data_->railGroups ) {
			if ( groupName.empty() ) continue;
			if ( std::find(groupNames.begin(), groupNames.end(), groupName) == groupNames.end() ) {
				groupNames.push_back(groupName);
			}
		}
		// Undo等で絞り込み中のグループが消えたら「すべて表示」へ自動で戻す
		//   （戻さないとリストが空に見えて、レールが消えたように錯覚してしまう）
		if ( !listGroupFilter.empty()
		  && std::find(groupNames.begin(), groupNames.end(), listGroupFilter) == groupNames.end() ) {
			listGroupFilter.clear();
		}
		std::string preview = listGroupFilter.empty() ? "すべて表示" : listGroupFilter;
		ImGui::SetNextItemWidth(180.0f);
		if ( ImGui::BeginCombo("グループ絞り込み", preview.c_str()) ) {
			if ( ImGui::Selectable("すべて表示", listGroupFilter.empty()) ) { listGroupFilter.clear(); }
			for ( const auto& groupName : groupNames ) {
				if ( ImGui::Selectable(groupName.c_str(), listGroupFilter == groupName) ) {
					listGroupFilter = groupName;
				}
			}
			ImGui::EndCombo();
		}
		// 絞り込み中：選択レールをこのグループへ入れるショートカット（リスト側からもグループ操作できる）
		if ( !listGroupFilter.empty() && currentEditRailIndex_ >= 0
		  && currentEditRailIndex_ < ( int ) data_->railGroups.size() ) {
			ImGui::SameLine();
			if ( ImGui::SmallButton("選択レールをここへ") ) {
				data_->railGroups[currentEditRailIndex_] = listGroupFilter;
			}
		}
	}
	{
		int duplicateRail = -1;
		int deleteRail = -1;
		std::string deleteGroupTarget; // グループ見出しの「削除」で選ばれたグループ名

		// 既存グループ一覧（右クリックの分類メニューで使う）
		std::vector<std::string> allGroups;
		for ( const auto& existingGroup : data_->railGroups ) {
			if ( existingGroup.empty() ) continue;
			if ( std::find(allGroups.begin(), allGroups.end(), existingGroup) == allGroups.end() ) {
				allGroups.push_back(existingGroup);
			}
		}

		// 1行分の描画（ラベル/複製/削除/道コンボ/接続要約）
		auto drawRailRow = [&](int i){
			ImGui::PushID(i);
			bool isSelected = ( currentEditRailIndex_ == i );

			int t = data_->railTypes[i];
			const char* typeStr = ( t == 0 ) ? "横" : ( t == 1 ) ? "縦" : "自動";
			char label[64];
			snprintf(label, sizeof(label), "路線 %d  (%dノード, %s)",
				i, ( int ) data_->railLines[i].size(), typeStr);

			if ( ImGui::Selectable(label, isSelected, 0, ImVec2(190.0f, 0.0f)) ) {
				SelectWholeRail(i); // クリックで路線まるごと選択（ギズモで移動できる）
			}
			if ( ImGui::IsItemHovered() ) { hoveredListRail_ = i; } // Game View 側で黄色ハイライト
			// 右クリック＝分類メニュー：好きなグループへ移動 / 新しいグループを作って移動
			if ( ImGui::BeginPopupContextItem("railClassify") ) {
				ImGui::TextDisabled("路線 %d をグループへ分類:", i);
				if ( ImGui::MenuItem("未分類へ戻す") ) { data_->railGroups[i] = ""; }
				for ( const auto& targetGroup : allGroups ) {
					bool inThisGroup = ( data_->railGroups[i] == targetGroup );
					if ( ImGui::MenuItem(targetGroup.c_str(), nullptr, inThisGroup) ) {
						data_->railGroups[i] = targetGroup;
					}
				}
				ImGui::Separator();
				static char newGroupBuf[64] {};
				ImGui::SetNextItemWidth(140.0f);
				ImGui::InputTextWithHint("##newGroupName", "新しいグループ名", newGroupBuf, sizeof(newGroupBuf));
				ImGui::SameLine();
				if ( ImGui::SmallButton("作成して移動") && newGroupBuf[0] != '\0' ) {
					data_->railGroups[i] = newGroupBuf;
					newGroupBuf[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton("複製") ) { duplicateRail = i; }
			ImGui::SameLine();
			if ( ImGui::SmallButton("削除") ) { deleteRail = i; }
			// 道の生成トグル（§4：0=自動で道を敷く / 1=道なし）
			ImGui::SameLine();
			{
				int roadModeValue = ( i < ( int ) data_->railRoadModes.size() ) ? data_->railRoadModes[i] : 0;
				const char* roadModeLabels[] = { "道", "なし", "溶け道" };
				ImGui::SetNextItemWidth(74.0f);
				if ( ImGui::Combo("##roadMode", &roadModeValue, roadModeLabels, 3) ) {
					if ( i < ( int ) data_->railRoadModes.size() ) {
						data_->railRoadModes[i] = roadModeValue;
						++railVersion_; // 道メッシュ・ジョイントの即時再生成トリガー
					}
				}
				if ( ImGui::IsItemHovered() ) {
					ImGui::SetTooltip("道の種類：\n 道＝通常の道メッシュ\n なし＝レールだけ（カメラ/敵/演出用）\n 溶け道＝SDFの道。普段は消えていて、近づくと現れ、離れると溶けて消える");
				}
			}
			if ( !IsRailReachable(i) ) {
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f), "×通れない");
			}
			// 接続の要約（溶接/T字/交差の件数）。詳細は選択して下の「接続情報」で見る
			{
				const auto& conns = GetRailConnections(i);
				if ( !conns.empty() ) {
					int weldCount = 0, tJoinCount = 0, crossCount = 0;
					for ( const auto& c : conns ) {
						if ( c.type == 0 ) ++weldCount;
						else if ( c.type == 1 ) ++tJoinCount;
						else ++crossCount;
					}
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "溶%d T%d 交%d", weldCount, tJoinCount, crossCount);
				}
			}
			ImGui::PopID();
		};

		if ( !listGroupFilter.empty() ) {
			// 絞り込み中：対象グループだけフラット表示
			for ( int i = 0; i < ( int ) data_->railLines.size(); ++i ) {
				if ( data_->railGroups[i] == listGroupFilter ) { drawRailRow(i); }
			}
		} else {
			// すべて表示：グループごとの折りたたみ表示。
			//   未分類（手で引いた本線など）は開いた状態、テンプレ/リフト群は畳まれてスッキリ見える
			std::vector<std::string> groupOrder;
			groupOrder.push_back(""); // 未分類が先頭
			for ( const auto& groupName : data_->railGroups ) {
				if ( groupName.empty() ) continue;
				if ( std::find(groupOrder.begin(), groupOrder.end(), groupName) == groupOrder.end() ) {
					groupOrder.push_back(groupName);
				}
			}
			for ( const auto& groupName : groupOrder ) {
				int memberCount = 0;
				for ( const auto& railGroup : data_->railGroups ) { if ( railGroup == groupName ) ++memberCount; }
				if ( memberCount == 0 ) continue;
				char headerLabel[96];
				snprintf(headerLabel, sizeof(headerLabel), "%s (%d)###grp_%s",
					groupName.empty() ? "未分類" : groupName.c_str(), memberCount, groupName.c_str());
				ImGuiTreeNodeFlags headerFlags = groupName.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0;
				bool headerOpen = ImGui::CollapsingHeader(headerLabel, headerFlags);
				// 見出しから直接グループ操作（開かなくても表示切替・まとめて削除ができる）
				if ( !groupName.empty() ) {
					ImGui::PushID(headerLabel);
					ImGui::SameLine();
					if ( ImGui::SmallButton("表示") ) {
						int newVisible = -1; // 先頭メンバーの反転値に全員合わせる
						for ( int i = 0; i < ( int ) data_->railLines.size(); ++i ) {
							if ( data_->railGroups[i] != groupName ) continue;
							if ( newVisible < 0 ) { newVisible = data_->railVisible[i] ? 0 : 1; }
							data_->railVisible[i] = newVisible;
						}
						++railVersion_;
					}
					ImGui::SameLine();
					if ( ImGui::SmallButton("削除") ) { deleteGroupTarget = groupName; }
					ImGui::PopID();
				}
				if ( headerOpen ) {
					for ( int i = 0; i < ( int ) data_->railLines.size(); ++i ) {
						if ( data_->railGroups[i] == groupName ) { drawRailRow(i); }
					}
				}
			}
		}

		// 複製：少し奥にずらしたコピーを作り、すぐ動かせるよう選択しておく（Ctrl+Dと共通処理）
		if ( duplicateRail >= 0 ) {
			DuplicateRail(duplicateRail);
		}
		// 削除（最後の1本は消さない）
		if ( deleteRail >= 0 && data_->railLines.size() > 1 ) {
			EraseRail(deleteRail);
			if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
				currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
			}
			selectedRailNode_ = -1;
			ClearMultiSelection();
			RebuildRailPoints();
		}
		// グループまとめて削除（見出しの「削除」ボタン。最後の1本は残す）
		if ( !deleteGroupTarget.empty() ) {
			for ( int i = ( int ) data_->railLines.size() - 1; i >= 0; --i ) {
				if ( data_->railGroups[i] == deleteGroupTarget && data_->railLines.size() > 1 ) { EraseRail(i); }
			}
			if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
				currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
			}
			selectedRailNode_ = -1;
			ClearMultiSelection();
			RebuildRailPoints();
		}
	}
	ImGui::Separator();

	// --- このレールの「移動操作タイプ」（横=A/D移動 / 縦=W/S移動）---
	if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) data_->railLines.size() ) {
		int& t = data_->railTypes[currentEditRailIndex_];

		// 自動判定の結果（表示用）：縦判定には Z が X の1.5倍以上必要（実装と同じ式）
		const auto& nodes = data_->railLines[currentEditRailIndex_];
		int autoType = 0; // 0=横 / 1=縦
		if ( nodes.size() >= 2 ) {
			float dx = nodes.back().x - nodes.front().x; if ( dx < 0 ) dx = -dx;
			float dz = nodes.back().z - nodes.front().z; if ( dz < 0 ) dz = -dz;
			autoType = ( dz > dx * 1.5f ) ? 1 : 0;
		}

		const char* label =
			( t == 0 ) ? "タイプ: 横 (A/D移動) [固定]" :
			( t == 1 ) ? "タイプ: 縦 (W/S移動) [固定]" :
			( autoType == 0 ? "タイプ: 自動 → 横 (A/D移動)" : "タイプ: 自動 → 縦 (W/S移動)" );

		ImGui::Text("移動操作タイプ:");
		if ( ImGui::Button(label) ) {
			t = ( t == -1 ) ? 0 : ( t == 0 ? 1 : -1 ); // 自動→横→縦→自動 と循環
			++railVersion_;                            // ゲーム側へ即反映
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(押すと 自動→横→縦)");

		ImGui::TextDisabled("※動き（波形・リフト・親子付け）は「動き」タブへ移動");
		ImGui::Separator();

		// --- 地面タイプ（落下は「穴」で指定する。NoGround は廃止）---
		data_->railGroundTypes.resize(data_->railLines.size(), 1); // 既定は Gap
		int& gt = data_->railGroundTypes[currentEditRailIndex_];
		if ( gt > 1 ) gt = 0; // 旧 NoGround(2) は Safe 扱いに直す
		const char* groundLabels[] = { "Safe (安全)", "Gap (端で落下)" };
		if ( ImGui::Combo("地面タイプ", &gt, groundLabels, 2) ) { ++railVersion_; }

		// --- 表示／非表示（連結用の見えないレール）---
		data_->railVisible.resize(data_->railLines.size(), 1);
		bool vis = ( data_->railVisible[currentEditRailIndex_] != 0 );
		if ( ImGui::Checkbox("ゲームに表示する", &vis) ) {
			data_->railVisible[currentEditRailIndex_] = vis ? 1 : 0;
			++railVersion_;
		}
		ImGui::SameLine();
		ImGui::TextDisabled(vis ? "(表示)" : "(非表示=連結用の見えないレール)");

		// --- 接続情報：このレールがどのレールと繋がる/交差するか（座標付き）---
		//   「通れない」の理由調査や、意図しない接続・交差の発見に使う
		{
			const auto& conns = GetRailConnections(currentEditRailIndex_);
			ImGui::TextDisabled("接続情報（%d件）:", ( int ) conns.size());
			if ( conns.empty() ) {
				ImGui::TextColored(ImVec4(0.85f, 0.45f, 1.0f, 1.0f),
					"  どのレールとも繋がっていません（未接続）");
			}
			const char* typeNames[] = { "溶接", "T字", "交差" };
			for ( const auto& conn : conns ) {
				ImGui::BulletText("%s: 路線%d と @ (%.1f, %.1f, %.1f)",
					typeNames[std::clamp(conn.type, 0, 2)], conn.otherRail,
					conn.pos.x, conn.pos.y, conn.pos.z);
			}

			// --- ワンクリック修復：未接続の端に「真上から見れば近い」相手がいれば、
			//     高さを相手に合わせて接続するボタンを出す（ドラッグのやり直し不要）---
			//   検索半径はスナップ距離の2倍（明示操作なのでドラッグ吸着より少し広めに拾う）
			const auto& line = data_->railLines[currentEditRailIndex_];
			if ( line.size() >= 2 ) {
				const char* endNames[] = { "先頭", "末尾" };
				for ( int endSide = 0; endSide < 2; ++endSide ) {
					bool front = ( endSide == 0 );
					if ( IsRailEndConnected(currentEditRailIndex_, front) ) continue;
					SnapTarget tgt = FindSnapTargetXZ(currentEditRailIndex_, front, railSnapDistance_ * 2.0f);
					if ( !tgt.valid ) continue;
					Vector3 endPos = front ? line.front() : line.back();
					float heightDiff = tgt.pos.y - endPos.y;
					char repairLabel[160];
					snprintf(repairLabel, sizeof(repairLabel),
						"%s側: 路線%d へ高さを合わせて接続（高さ差 %+.1fm）",
						endNames[endSide], tgt.rail, heightDiff);
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.10f, 0.75f));
					if ( ImGui::Button(repairLabel) ) {
						ConnectToTarget(currentEditRailIndex_, front, tgt);
					}
					ImGui::PopStyleColor();
				}
			}
		}

		// --- 線のつなぎ方（ノードの位置は同じで「つなぎ方」だけが変わる）---
		//   スプライン=置いた点を全部通るなめらかな曲線 / 直線=点をそのまま直線でつなぐ（カクカク）
		data_->railLineModes.resize(data_->railLines.size(), 0);
		int& lm = data_->railLineModes[currentEditRailIndex_];
		const char* lineModeLabels[] = { "スプライン (なめらか)", "直線 (カクカク)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("線のつなぎ方", &lm, lineModeLabels, 2) ) { ++railVersion_; }


		// --- グループ（リストの絞り込み/一括操作の単位）---
		data_->railGroups.resize(data_->railLines.size());
		{
			std::string& currentGroup = data_->railGroups[currentEditRailIndex_];
			char groupBuf[64] {};
			snprintf(groupBuf, sizeof(groupBuf), "%s", currentGroup.c_str());
			ImGui::SetNextItemWidth(160.0f);
			if ( ImGui::InputText("グループ名", groupBuf, sizeof(groupBuf)) ) {
				currentGroup = groupBuf;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("同じ名前を付けたレールはグループとして絞り込み・一括操作できる\n（テンプレート一括設置は自動で同じグループになる）");
			}
			if ( !currentGroup.empty() ) {
				// 一括操作：このレールの設定をグループ全員へコピー（位相は数珠つなぎ用に対象外）
				if ( ImGui::Button("このレールの設定をグループ全体へコピー") ) {
					// 先に全配列をレール数まで揃える（短い配列に書き込むとヒープ破壊→後でクラッシュするため）
					int railTotal = ( int ) data_->railLines.size();
					data_->railTypes.resize(railTotal, -1);
					data_->railMotions.resize(railTotal, Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
					data_->railGroundTypes.resize(railTotal, 1);
					data_->railVisible.resize(railTotal, 1);
					data_->railLineModes.resize(railTotal, 0);
					data_->railRoadModes.resize(railTotal, 0);
					data_->railMotionTypes.resize(railTotal, 0);
					data_->railMotionTriggers.resize(railTotal, 0);
					data_->railAppearTriggers.resize(railTotal, -1);
					data_->railOneWay.resize(railTotal, 0);
					data_->railSpeedMuls.resize(railTotal, 1.0f);
					data_->railGuideRails.resize(railTotal, -1);
					data_->railGuideStarts.resize(railTotal, 0.0f);
					data_->railGuideEnds.resize(railTotal, -1.0f);
					data_->railGuideModes.resize(railTotal, 0);
					data_->railGuideAligns.resize(railTotal, 0);
					data_->railGuideDwells.resize(railTotal, 0.0f);
					for ( int g = 0; g < ( int ) data_->railLines.size(); ++g ) {
						if ( g == currentEditRailIndex_ || data_->railGroups[g] != currentGroup ) continue;
						data_->railTypes[g]       = data_->railTypes[currentEditRailIndex_];
						data_->railMotions[g]     = data_->railMotions[currentEditRailIndex_];
						data_->railGroundTypes[g] = data_->railGroundTypes[currentEditRailIndex_];
						data_->railVisible[g]     = data_->railVisible[currentEditRailIndex_];
						data_->railLineModes[g]   = data_->railLineModes[currentEditRailIndex_];
						data_->railRoadModes[g]   = data_->railRoadModes[currentEditRailIndex_];
						data_->railMotionTypes[g] = data_->railMotionTypes[currentEditRailIndex_];
						data_->railMotionTriggers[g] = data_->railMotionTriggers[currentEditRailIndex_];
						data_->railAppearTriggers[g] = data_->railAppearTriggers[currentEditRailIndex_];
						data_->railOneWay[g]      = data_->railOneWay[currentEditRailIndex_];
						data_->railSpeedMuls[g]   = data_->railSpeedMuls[currentEditRailIndex_];
						data_->railGuideRails[g]  = data_->railGuideRails[currentEditRailIndex_];
						data_->railGuideStarts[g] = data_->railGuideStarts[currentEditRailIndex_];
						data_->railGuideEnds[g]   = data_->railGuideEnds[currentEditRailIndex_];
						data_->railGuideModes[g]  = data_->railGuideModes[currentEditRailIndex_];
						data_->railGuideAligns[g] = data_->railGuideAligns[currentEditRailIndex_];
						data_->railGuideDwells[g] = data_->railGuideDwells[currentEditRailIndex_];
					}
					++railVersion_;
				}
				if ( ImGui::IsItemHovered() ) {
					ImGui::SetTooltip("波形・ガイド番号・周期・道/表示設定などを一括反映。\n位相だけはコピーしない（足場の数珠つなぎは位相をずらして使うため）");
				}

				// 一括移動：グループ全レールの全ノードを平行移動
				static Vector3 groupMove { 0.0f, 0.0f, 0.0f };
				ImGui::SetNextItemWidth(200.0f);
				ImGui::DragFloat3("グループ移動量(m)", &groupMove.x, 0.1f);
				ImGui::SameLine();
				if ( ImGui::Button("移動##group") ) {
					for ( int g = 0; g < ( int ) data_->railLines.size(); ++g ) {
						if ( data_->railGroups[g] != currentGroup ) continue;
						for ( auto& node : data_->railLines[g] ) {
							node.x += groupMove.x; node.y += groupMove.y; node.z += groupMove.z;
						}
					}
					groupMove = { 0.0f, 0.0f, 0.0f };
					RebuildRailPoints();
				}
			}
		}

		// --- 端の丸広場（始点/終点に円形の広場を敷く。行き止まりやゴールの見た目用）---
		data_->railEndPlazas.resize(data_->railLines.size(), 0);
		{
			int& endPlaza = data_->railEndPlazas[currentEditRailIndex_];
			bool plazaFront = ( endPlaza & 1 ) != 0;
			bool plazaBack  = ( endPlaza & 2 ) != 0;
			if ( ImGui::Checkbox("始点に丸広場", &plazaFront) ) {
				endPlaza = ( plazaFront ? 1 : 0 ) | ( plazaBack ? 2 : 0 );
				++railVersion_; // 道メッシュの即時再生成トリガー
			}
			ImGui::SameLine();
			if ( ImGui::Checkbox("終点に丸広場", &plazaBack) ) {
				endPlaza = ( plazaFront ? 1 : 0 ) | ( plazaBack ? 2 : 0 );
				++railVersion_;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("レールの端に円形の広場を敷く（曲がり角の丸広場と同じ見た目。行き止まり/ゴール向け）");
			}
		}

		// --- 片方向レール（逆走禁止。ジェットコースター区間など）---
		data_->railOneWay.resize(data_->railLines.size(), 0);
		int& ow = data_->railOneWay[currentEditRailIndex_];
		const char* oneWayLabels[] = { "両方向 (通常)", "正方向のみ (始点→終点)", "逆方向のみ (終点→始点)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("片方向", &ow, oneWayLabels, 3) ) { ++railVersion_; }

		// --- 速度倍率（加速/減速レール）---
		data_->railSpeedMuls.resize(data_->railLines.size(), 1.0f);
		float& sm = data_->railSpeedMuls[currentEditRailIndex_];
		ImGui::SetNextItemWidth(140.0f);
		if ( ImGui::SliderFloat("速度倍率", &sm, 0.25f, 3.0f, "%.2fx") ) { ++railVersion_; }
		ImGui::Separator();

		// --- スタート／ゴール地点（選択中のノードをマップに記録）---
		ImGui::Text("スタート/ゴール:");
		bool nodeSelected = ( selectedRailNode_ >= 0
			&& selectedRailNode_ < ( int ) data_->railLines[currentEditRailIndex_].size() );
		ImGui::BeginDisabled(!nodeSelected);
		if ( ImGui::Button("選択ノードをスタート地点に") ) {
			data_->startRailIndex = currentEditRailIndex_;
			data_->startNodeIndex = selectedRailNode_;
			++railVersion_;
		}
		ImGui::SameLine();
		if ( ImGui::Button("選択ノードをゴールに") ) {
			data_->goalRailIndex = currentEditRailIndex_;
			data_->goalNodeIndex = selectedRailNode_;
			++railVersion_;
		}
		ImGui::EndDisabled();
		if ( !nodeSelected ) { ImGui::TextDisabled("(Game View でノードをクリックして選択してから押す)"); }
		ImGui::Text("スタート: レール%d ノード%d", data_->startRailIndex, data_->startNodeIndex);
		ImGui::SameLine();
		if ( data_->goalRailIndex >= 0 ) {
			ImGui::Text(" / ゴール: レール%d ノード%d", data_->goalRailIndex, data_->goalNodeIndex);
			ImGui::SameLine();
			if ( ImGui::SmallButton("ゴール解除") ) { data_->goalRailIndex = -1; ++railVersion_; }
		} else {
			ImGui::TextDisabled(" / ゴール: 未設定");
		}
		ImGui::Separator();

	}

	// Undo / Redo
	// --- 操作履歴（元に戻す / やり直す / 最初期に戻す）---
	ImGui::BeginDisabled(undoStack_.empty());
	if ( ImGui::Button("元に戻す (Ctrl+Z)") ) { Undo(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(redoStack_.empty());
	if ( ImGui::Button("やり直す (Ctrl+Y)") ) { Redo(); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!CanResetToInitial());
	if ( ImGui::Button("最初期に戻す") ) { ResetToInitial(); }
	ImGui::EndDisabled();
	ImGui::TextDisabled("履歴:%d手 / やり直し:%d手  ※最初期=このマップを開いた直後の状態",
		static_cast< int >( undoStack_.size() ), static_cast< int >( redoStack_.size() ));

	if ( ImGui::CollapsingHeader("操作ヘルプ（クリックで開閉）") ) {
		ImGui::TextDisabled("【Game View操作】");
		ImGui::TextDisabled("線クリック→路線まるごと選択(ギズモで移動) / ノードクリック→1点選択");
		ImGui::TextDisabled("空白をドラッグ→矩形選択(まとめて移動) / Shift+クリック→追加選択");
		ImGui::TextDisabled("Ctrl+線クリック→ノード挿入 / 右クリック→ノード削除");
		ImGui::TextDisabled("端点をドラッグ→近くの線に自動スナップ(緑=接続/オレンジ=高さ差表示)");
		ImGui::TextDisabled("【キーボード】矢印=1マス移動 / Q,E=下,上 / Delete=削除 / Ctrl+D=路線複製");
		ImGui::TextDisabled("【数値欄】クリック=キーボード入力 / ドラッグ=増減");
		ImGui::TextDisabled("【リスト】行を右クリック=グループへ分類 / ホバー=画面で黄色に光る");
		ImGui::TextDisabled("【リフトの作り方】作成タブ「動くリフト一式」→クリック設置→「検証」の動きプレビューON");
		ImGui::TextDisabled("  既存の足場を動かす→「動き」タブの「自動でリフト化」/ 親レール番号を入れて追従");
		ImGui::TextDisabled("【仕上げ】「検証」ウィンドウの「到達チェック」→OK(緑)ならクリア可能 / ゴースト走行で流れ確認");
		ImGui::TextDisabled("  経路色: オレンジ=走る / シアン=ジャンプ / マゼンタ=ふんばり必要");
	}









	// --- ノード一覧・編集（追加/挿入/削除。座標はクリックでキーボード入力可）---
	if ( ImGui::CollapsingHeader("ノード一覧・編集", ImGuiTreeNodeFlags_DefaultOpen) ) {
	ImGui::Text("ノード追加 (レール%d):", currentEditRailIndex_);
	ImGui::SameLine();
	if ( ImGui::Button("先頭に追加") ) { ExtendRailNode(true); }
	ImGui::SameLine();
	if ( ImGui::Button("末尾に追加") ) { ExtendRailNode(false); }
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if ( ImGui::IsItemHovered() ) {
		ImGui::SetTooltip("選択中レールの端の延長線上にノードを1個足します。\n追加後はギズモ/数値で位置を調整。途中への挿入は Ctrl+線クリック か「↓ 下に挿入」。");
	}

	ImGui::BeginChild("RailNodeList", ImVec2(0, 300), true);
	for ( size_t i = 0; i < data_->railLines[currentEditRailIndex_].size(); ++i ) {
		ImGui::PushID(static_cast< int >(i));
		std::string label = "Node " + std::to_string(i);

		if ( ImGui::Selectable(label.c_str(), selectedRailNode_ == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(60, 0)) ) {
			selectedRailNode_ = static_cast< int >(i);
		}
		ImGui::SameLine();

		// 穴チェックボックス
		if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size()
			&& i < data_->railNodeHoles[currentEditRailIndex_].size() ) {
			bool hole = ( data_->railNodeHoles[currentEditRailIndex_][i] != 0 );
			if ( ImGui::Checkbox("穴", &hole) ) {
				data_->railNodeHoles[currentEditRailIndex_][i] = hole ? 1 : 0;
				++railVersion_;
			}
			ImGui::SameLine();
		}

		ImGui::PushItemWidth(135.0f);
		if ( ImGui::DragFloat3(( "##" + label ).c_str(), &data_->railLines[currentEditRailIndex_][i].x, 0.5f) ) {
			++railVersion_; // 数値編集でも緑線をライブ更新
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();

		if ( ImGui::Button("↓ 下に挿入") ) {
			Vector3 newPos = data_->railLines[currentEditRailIndex_][i];
			if ( i + 1 < data_->railLines[currentEditRailIndex_].size() ) {
				newPos.x = ( data_->railLines[currentEditRailIndex_][i].x + data_->railLines[currentEditRailIndex_][i + 1].x ) * 0.5f;
				newPos.y = ( data_->railLines[currentEditRailIndex_][i].y + data_->railLines[currentEditRailIndex_][i + 1].y ) * 0.5f;
				newPos.z = ( data_->railLines[currentEditRailIndex_][i].z + data_->railLines[currentEditRailIndex_][i + 1].z ) * 0.5f;
			} else {
				newPos.z += 5.0f;
			}
			data_->railLines[currentEditRailIndex_].insert(data_->railLines[currentEditRailIndex_].begin() + i + 1, newPos);
			// 穴配列も同じ位置に挿入（穴なし=0）
			if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
				auto& h = data_->railNodeHoles[currentEditRailIndex_];
				if ( i + 1 <= h.size() ) h.insert(h.begin() + i + 1, 0);
			}

			RebuildRailPoints(); // 配列が変わったので一括再構築！
			ImGui::PopID();
			break;
		}

		ImGui::SameLine();
		if ( ImGui::Button("削除") ) {
			data_->railLines[currentEditRailIndex_].erase(data_->railLines[currentEditRailIndex_].begin() + i);
			// 穴配列も同じ位置を削除
			if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
				auto& h = data_->railNodeHoles[currentEditRailIndex_];
				if ( i < h.size() ) h.erase(h.begin() + i);
			}

			RebuildRailPoints(); // 一括再構築！
			ImGui::PopID();
			break;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	}
	ImGui::EndTabItem();
	} // 管理タブ

	// =========================================================
	//  作成 (Shape) タブ — 新しいレールを「作る」専用。
	//  生成直後は路線まるごと選択済みなので、ギズモですぐ配置できる。
	// =========================================================
	if ( ImGui::BeginTabItem("作成 (Shape)") ) {

	// --- 作り方ガイド（モードが多いので、おすすめ順を一目で）---
	ImGui::TextDisabled("作り方（上から手軽な順）:");
	ImGui::BulletText("形から: 「形」を選び生成 → Game Viewでクリック設置");
	ImGui::BulletText("手描き: キャンバスに引いて生成 → クリック設置");
	ImGui::BulletText("細かく: 空の路線→方向ボタン/マウス追加で1点ずつ");
	ImGui::BulletText("図で調整: 下の「2D形状エディタ」で点をつかんで整える");
	ImGui::Separator();

	// --- 2D形状エディタ：選択中の路線を図で編集（動かない普通のレールもOK）---
	//   横見図＝坂・登り下りを滑らかに / 上から＝奥行き⇔横向きのカーブを滑らかに。
	//   スプライン路線は実際に通る曲線（Catmull-Rom）がそのまま表示される
	if ( ImGui::CollapsingHeader("2D形状エディタ（選択路線を図で編集）", ImGuiTreeNodeFlags_DefaultOpen) ) {
		if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) data_->railLines.size() ) {
			ImGui::Text("編集対象: 路線%d", currentEditRailIndex_);
			ImGui::SameLine();
			ImGui::Checkbox("固定##lockEditTargetShape", &lockEditTarget_);
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("ONの間、Game View のクリックで編集対象が切り替わらない\n（この2Dエディタで作業中に他のレールへ移ってしまう事故防止）");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(選択は管理タブ / Game View クリック)");
			DrawRailPathEditor(currentEditRailIndex_, false);
		} else {
			ImGui::TextDisabled("路線が選択されていません（管理タブ / Game View クリックで選択）");
		}
	}
	ImGui::Separator();

	// --- パラメータ式シェイプ生成（スタンプ配置：生成→マウスに追従→クリックで設置）---
	{
		static int   shapeType   = 0;
		static float shapeLen    = 10.0f;
		static int   shapeDiv    = 4;
		static float shapeRadius = 3.0f;
		static float shapeStepH  = 1.0f;
		static float shapeYawDeg = 0.0f;              // 生成する形の向き（0°=+X。90°=+Z奥）
		static std::vector<Vector3> shapeBase;        // 回転前（+X向き）の形。向き変更の再適用に使う

		// 形の各点を Y軸まわりに回す（0°=+X向き=従来どおり / 90°=+Z(奥) / ±180°=-X / -90°=-Z(手前)）
		auto rotateShape = [](const std::vector<Vector3>& src, float yawDeg) -> std::vector<Vector3>{
			float rad = yawDeg * 3.14159265f / 180.0f;
			float c = std::cos(rad), s = std::sin(rad);
			std::vector<Vector3> out;
			out.reserve(src.size());
			for ( const auto& p : src ) {
				out.push_back({ p.x * c - p.z * s, p.y, p.x * s + p.z * c });
			}
			return out;
			};

		const char* shapeNames[] = { "直線", "L字", "円 (ループ)", "階段", "S字カーブ" };
		ImGui::Text("形を選んでパラメータを決めて「生成」:");
		ImGui::SetNextItemWidth(150.0f);
		ImGui::Combo("形", &shapeType, shapeNames, IM_ARRAYSIZE(shapeNames));

		// --- 向き：ドラッグで自由な角度＋ワンタッチの90°ボタン。配置待ち中の変更も即反映 ---
		bool yawChanged = false;
		ImGui::SetNextItemWidth(140.0f);
		yawChanged |= ImGui::DragFloat("向き (°)", &shapeYawDeg, 1.0f, -180.0f, 180.0f, "%.0f");
		ImGui::SameLine();
		if ( ImGui::SmallButton("右+X") )   { shapeYawDeg = 0.0f;    yawChanged = true; }
		ImGui::SameLine();
		if ( ImGui::SmallButton("奥+Z") )   { shapeYawDeg = 90.0f;   yawChanged = true; }
		ImGui::SameLine();
		if ( ImGui::SmallButton("左-X") )   { shapeYawDeg = 180.0f;  yawChanged = true; }
		ImGui::SameLine();
		if ( ImGui::SmallButton("手前-Z") ) { shapeYawDeg = -90.0f;  yawChanged = true; }
		// 配置待ちの形にも向きの変更をその場で反映（もう一度「生成」を押し直さなくてよい）
		if ( yawChanged && HasPendingStamp() && !shapeBase.empty() ) {
			pendingStamp_ = rotateShape(shapeBase, shapeYawDeg);
		}

		// 形ごとのパラメータ（必要なものだけ表示）
		ImGui::PushItemWidth(140.0f);
		if ( shapeType == 0 || shapeType == 1 || shapeType == 3 || shapeType == 4 ) {
			ImGui::DragFloat("長さ (m)", &shapeLen, 0.5f, 1.0f, 100.0f);
		}
		if ( shapeType == 0 || shapeType == 3 || shapeType == 4 ) {
			ImGui::DragInt("分割数", &shapeDiv, 1, 1, 32);
		}
		if ( shapeType == 2 ) {
			ImGui::DragFloat("半径 (m)", &shapeRadius, 0.1f, 0.5f, 30.0f);
			ImGui::DragInt("分割数", &shapeDiv, 1, 6, 32);
		}
		if ( shapeType == 4 ) {
			ImGui::DragFloat("振れ幅 (m)", &shapeRadius, 0.1f, 0.5f, 30.0f);
		}
		if ( shapeType == 3 ) {
			ImGui::DragFloat("1段の高さ (m)", &shapeStepH, 0.1f, 0.1f, 10.0f);
		}
		ImGui::PopItemWidth();

		if ( ImGui::Button("生成 (Game Viewでクリックして設置)", ImVec2(250.0f, 0.0f)) ) {
			std::vector<Vector3> line;
			const Vector3 base = { 0.0f, 0.0f, 0.0f }; // 原点基準で作り、設置時にクリック位置へ平行移動
			int div = ( shapeDiv < 1 ) ? 1 : shapeDiv;

			switch ( shapeType ) {
			case 0: // 直線（+X方向）
				for ( int k = 0; k <= div; ++k ) {
					float t = static_cast< float >( k ) / static_cast< float >( div );
					line.push_back({ base.x + shapeLen * t, base.y, base.z });
				}
				break;
			case 1: // L字（+Xに半分 → +Zに半分）
				line.push_back(base);
				line.push_back({ base.x + shapeLen * 0.5f, base.y, base.z });
				line.push_back({ base.x + shapeLen * 0.5f, base.y, base.z + shapeLen * 0.5f });
				break;
			case 2: // 円（最初と最後が同じ位置 → ゲーム側が自動でループ認識）
			{
				int seg = ( div < 6 ) ? 6 : div;
				for ( int k = 0; k <= seg; ++k ) {
					float ang = 6.2831853f * static_cast< float >( k ) / static_cast< float >( seg );
					line.push_back({ base.x + std::cos(ang) * shapeRadius,
					                 base.y,
					                 base.z + std::sin(ang) * shapeRadius });
				}
			}
			break;
			case 3: // 階段（+Xに進んで +Yに上がるを繰り返す）
			{
				float run = shapeLen / static_cast< float >( div );
				Vector3 p = base;
				line.push_back(p);
				for ( int k = 0; k < div; ++k ) {
					p.x += run;        line.push_back(p); // 水平に進む
					p.y += shapeStepH; line.push_back(p); // 1段上がる
				}
			}
			break;
			case 4: // S字カーブ（+Xに進みながらZへsin波で振れる）
			{
				int seg = ( div < 2 ) ? 4 : div * 2;
				for ( int k = 0; k <= seg; ++k ) {
					float t = static_cast< float >( k ) / static_cast< float >( seg );
					line.push_back({ base.x + shapeLen * t,
					                 base.y,
					                 base.z + std::sin(t * 6.2831853f) * shapeRadius });
				}
			}
			break;
			}

			if ( !line.empty() ) {
				// すぐ路線にせず「配置待ち（スタンプ）」にする → Game Viewでクリックした場所に設置。
				// 回転前の形も控えておき、配置待ち中の「向き」変更に即反映できるようにする
				shapeBase = line;
				pendingStamp_ = rotateShape(shapeBase, shapeYawDeg);
			}
		}
		if ( HasPendingStamp() ) {
			ImGui::SameLine();
			if ( ImGui::Button("配置をやめる") ) { CancelStamp(); }
			ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f),
				"Game Viewでクリック→設置 / 右クリックかEscで中止");
		} else {
			ImGui::TextDisabled("「生成」を押すと形がマウスに付いてくる → クリックで設置");
		}
	}
	ImGui::Separator();

	// =========================================================
	//  テンプレート（複数レール一括生成）：ヨッシー風の「登り」構成をワンクリックで用意する。
	//   長い本線＋小さい足場の組み合わせは、本線を置いた後にこのテンプレートを重ねて作る
	if ( ImGui::CollapsingHeader("テンプレート（登り足場・らせん）") ) {
		static int   climbCount  = 6;    // 足場の数
		static float climbRise   = 1.4f; // 1段ごとの高さ(m)
		static float climbPadLen = 2.0f; // 足場1枚の長さ(m)
		static float climbShift  = 1.8f; // 交互の横ずれ(m)
		static float climbShiftZ = 0.0f; // 1段ごとの奥ずれ(m)。入れると斜め奥へ登る階段になる
		ImGui::TextDisabled("小さい足場を下から上へ積む（ふんばりジャンプで登る）:");
		ImGui::PushItemWidth(130.0f);
		ImGui::DragInt("足場の数", &climbCount, 1, 2, 60);
		ImGui::DragFloat("1段の高さ(m)", &climbRise, 0.1f, 0.2f, 6.0f);
		ImGui::DragFloat("足場の長さ(m)", &climbPadLen, 0.1f, 0.5f, 12.0f);
		ImGui::DragFloat("横ずれ(m)", &climbShift, 0.1f, 0.0f, 8.0f);
		ImGui::DragFloat("奥ずれ(m)", &climbShiftZ, 0.1f, -8.0f, 8.0f);
		ImGui::PopItemWidth();
		static int templateActive = -1; // ライブ反映用：どのテンプレが配置待ちか（-1=なし）
		bool templateSelectedNow = false;
		auto buildLadder = [&](bool alternate){
			pendingMultiStamp_.clear();
			pendingMultiMeta_.clear();
			for ( int i = 0; i < climbCount; ++i ) {
				float offsetX = ( alternate && ( i % 2 == 1 ) ) ? climbShift : 0.0f;
				float padY = climbRise * i;
				float padZ = climbShiftZ * i;
				pendingMultiStamp_.push_back({ { offsetX - climbPadLen * 0.5f, padY, padZ },
				                               { offsetX + climbPadLen * 0.5f, padY, padZ } });
			}
			pendingStamp_ = pendingMultiStamp_.front(); // 配置プレビュー用（1枚目＝クリック位置が最下段）
		};
		if ( ImGui::Button("のぼり足場（交互）を生成") ) { templateActive = 0; templateSelectedNow = true; }
		ImGui::SameLine();
		if ( ImGui::Button("のぼり足場（直線）を生成") ) { templateActive = 1; templateSelectedNow = true; }

		static float spiralRadius = 3.0f; // らせんの半径(m)
		static float spiralTurns  = 1.5f; // 周回数
		static float spiralRise   = 6.0f; // 登る高さ(m)
		ImGui::TextDisabled("らせん（円で回りながら登る1本のスプライン）:");
		ImGui::PushItemWidth(130.0f);
		ImGui::DragFloat("半径(m)", &spiralRadius, 0.1f, 1.0f, 10.0f);
		ImGui::DragFloat("周回数", &spiralTurns, 0.1f, 0.5f, 4.0f);
		ImGui::DragFloat("登る高さ(m)", &spiralRise, 0.5f, 1.0f, 30.0f);
		ImGui::PopItemWidth();
		auto buildSpiralStatic = [&](){
			pendingStamp_.clear();
			pendingMultiStamp_.clear();
			pendingMultiMeta_.clear();
			int totalSteps = ( std::max )( 8, ( int ) ( spiralTurns * 16.0f ) );
			for ( int k = 0; k <= totalSteps; ++k ) {
				float t = ( float ) k / totalSteps;
				float ang = spiralTurns * 2.0f * 3.14159265f * t;
				pendingStamp_.push_back({ std::cos(ang) * spiralRadius,
				                          spiralRise * t,
				                          std::sin(ang) * spiralRadius });
			}
		};
		if ( ImGui::Button("らせんのぼりを生成") ) { templateActive = 2; templateSelectedNow = true; }
		ImGui::TextDisabled("生成 → Game Viewでクリックした場所に一括設置（クリック位置＝最下段）。\n"
			"高い場所に置けば「上から降りる」用にもなる。足場は両方向に通行可");

		// ==== 動くリフト一式（設置した瞬間から全自動で動く）====
		//   1本目=見えないガイド + 残り=追従する足場（位相等間隔）をワンクリックで丸ごと設置する
		ImGui::Separator();
		ImGui::TextDisabled("動くリフト一式（クリック設置した瞬間から自動で動く。専用パラメータで自由に調整）:");
		static int   moveCount    = 4;     // 足場の数
		static float movePadLen   = 2.0f;  // 足場の長さ(m)
		static float movePeriod   = 12.0f; // 1周にかかる秒数
		static float moveInterval = 0.0f;  // 足場の間隔(秒)。0=経路全体に等間隔
		static float moveHeight   = 6.0f;  // 登る高さ(m)（縦/らせん/コンベアの長径）
		static float moveRadius   = 3.0f;  // 半径(m)（らせん/観覧車/コンベアの短径）
		static float moveTurns    = 1.5f;  // らせんの周回数
		ImGui::PushItemWidth(120.0f);
		ImGui::DragInt("足場の数##move", &moveCount, 1, 1, 24);
		ImGui::DragFloat("足場の長さ(m)##move", &movePadLen, 0.1f, 0.5f, 12.0f);
		ImGui::DragFloat("1周(秒)##move", &movePeriod, 0.5f, 2.0f, 120.0f, "%.1f");
		ImGui::DragFloat("足場の間隔(秒)##move", &moveInterval, 0.1f, 0.0f, 60.0f, "%.1f");
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("前の足場が出てから次が出るまでの秒数。0=経路全体に等間隔で並べる。\n"
				"小さくすると数珠つなぎが密に、大きくすると間が空く");
		}
		ImGui::DragFloat("高さ(m)##move", &moveHeight, 0.5f, 1.0f, 40.0f);
		ImGui::DragFloat("半径(m)##move", &moveRadius, 0.1f, 1.0f, 15.0f);
		ImGui::DragFloat("周回数##move", &moveTurns, 0.1f, 0.5f, 5.0f);
		ImGui::PopItemWidth();
		auto buildMovingLift = [&](std::vector<Vector3> guideLine){
			pendingMultiStamp_.clear();
			pendingMultiMeta_.clear();
			pendingMultiStamp_.push_back(guideLine);
			pendingMultiMeta_.push_back({ 0, 1, 0, 0, -1, 0.0f, movePeriod }); // ガイド：非表示・道なし
			Vector3 start = guideLine.front();
			for ( int i = 0; i < moveCount; ++i ) {
				// 間隔指定あり→秒間隔を位相へ換算 / なし→経路全体へ等間隔
				float phase = ( moveInterval > 0.01f )
					? ( moveInterval * i ) / movePeriod
					: ( float ) i / ( float ) moveCount;
				phase -= std::floor(phase); // 0〜1へ折り返し
				pendingMultiStamp_.push_back({ { start.x - movePadLen * 0.5f, start.y, start.z },
				                               { start.x + movePadLen * 0.5f, start.y, start.z } });
				pendingMultiMeta_.push_back({ 1, 0, 1, 3, 0, phase, movePeriod });
			}
			pendingStamp_ = pendingMultiStamp_[1]; // 配置プレビュー用
		};
		// サイズプリセット：数字をいじらなくてもワンクリックで雰囲気を選べる
		if ( ImGui::SmallButton("小##movePreset") ) { moveCount = 3; movePadLen = 1.5f; movePeriod = 8.0f;  moveHeight = 4.0f;  moveRadius = 2.0f; }
		ImGui::SameLine();
		if ( ImGui::SmallButton("中##movePreset") ) { moveCount = 4; movePadLen = 2.0f; movePeriod = 12.0f; moveHeight = 6.0f;  moveRadius = 3.0f; }
		ImGui::SameLine();
		if ( ImGui::SmallButton("大##movePreset") ) { moveCount = 6; movePadLen = 2.5f; movePeriod = 18.0f; moveHeight = 10.0f; moveRadius = 4.5f; }
		ImGui::SameLine();
		ImGui::TextDisabled("← サイズプリセット（配置待ち中でも即反映）");

		auto buildMoving = [&](int type){
			std::vector<Vector3> guideLine;
			switch ( type ) {
			case 0: // 縦リフト
				guideLine = { { 0.0f, 0.0f, 0.0f }, { 0.0f, moveHeight, 0.0f } };
				break;
			case 1: { // らせんリフト
				int totalSteps = ( std::max )( 8, ( int ) ( moveTurns * 16.0f ) );
				for ( int k = 0; k <= totalSteps; ++k ) {
					float t = ( float ) k / totalSteps;
					float ang = moveTurns * 2.0f * 3.14159265f * t;
					guideLine.push_back({ std::cos(ang) * moveRadius, moveHeight * t, std::sin(ang) * moveRadius });
				}
				break;
			}
			case 2: // 観覧車（縦の円ループ。最下点スタート、先頭=末尾でループ扱い）
				for ( int k = 0; k <= 16; ++k ) {
					float ang = 2.0f * 3.14159265f * ( float ) k / 16.0f;
					guideLine.push_back({ std::sin(ang) * moveRadius, moveRadius - std::cos(ang) * moveRadius, 0.0f });
				}
				break;
			default: // ベルトコンベア（水平の楕円ループ。長径=高さ(m)の値 / 短径=半径）
				for ( int k = 0; k <= 16; ++k ) {
					float ang = 2.0f * 3.14159265f * ( float ) k / 16.0f;
					guideLine.push_back({ std::cos(ang) * moveHeight * 0.5f, 0.0f, std::sin(ang) * moveRadius });
				}
				break;
			}
			buildMovingLift(guideLine);
		};
		if ( ImGui::Button("縦リフト") )       { templateActive = 10; templateSelectedNow = true; }
		ImGui::SameLine();
		if ( ImGui::Button("らせんリフト") )   { templateActive = 11; templateSelectedNow = true; }
		ImGui::SameLine();
		if ( ImGui::Button("観覧車") )         { templateActive = 12; templateSelectedNow = true; }
		ImGui::SameLine();
		if ( ImGui::Button("ベルトコンベア") ) { templateActive = 13; templateSelectedNow = true; }
		ImGui::TextDisabled("置いたら「動きをプレビュー」ONでその場で動く（Play不要）。\n"
			"設置後もガイドをギズモで曲げれば経路ごと変わる／各値はグループ一括コピーで後から変更可");

		// ライブ反映：配置待ちの間はテンプレートを毎フレーム作り直す。
		//   数字をドラッグすると Game View のゴーストが即変わる＝数字を読まなくても見た目で決められる
		if ( templateActive >= 0 ) {
			if ( !templateSelectedNow && !HasPendingStamp() ) {
				templateActive = -1; // 設置 or キャンセルされたら追従終了
			} else {
				switch ( templateActive ) {
				case 0:  buildLadder(true);   break;
				case 1:  buildLadder(false);  break;
				case 2:  buildSpiralStatic(); break;
				case 10: buildMoving(0); break;
				case 11: buildMoving(1); break;
				case 12: buildMoving(2); break;
				default: buildMoving(3); break;
				}
			}
		}
	}
	ImGui::Separator();

	// =========================================================
	//  手描きキャンバス：ImGui内で線を「仮」で引いて → 生成 → Game Viewで設置
	//   ・キャンバス内クリックで点を足す（仮の線がライブ表示される）
	//   ・右クリック / 「1つ戻す」で直前の点を取り消し
	//   ・「生成」でその形がマウスに付いてくる（既存スタンプ機構を再利用）
	// =========================================================
	if ( ImGui::CollapsingHeader("手描きで作る（キャンバスに引いて生成）") ) {
		static std::vector<ImVec2> sketch;     // キャンバス正規化座標(0..1)
		static float sketchSpan   = 20.0f;     // キャンバス全体が表す世界の幅(m)
		static bool  sketchSnap   = true;      // グリッド吸着
		static int   sketchDiv    = 20;        // グリッド分割数（吸着マス）
		static bool  sketchLoop   = false;     // 始点と終点を繋いでループに
		static bool  sketchSmooth = true;      // Catmull-Rom でなめらかに

		ImGui::SetNextItemWidth(110.0f);
		ImGui::DragFloat("範囲(m)", &sketchSpan, 0.5f, 2.0f, 200.0f);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		ImGui::DragFloat("高さY", &railDrawHeight_, 0.05f);
		ImGui::Checkbox("吸着", &sketchSnap);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::SliderInt("マス", &sketchDiv, 4, 40);
		ImGui::SameLine();
		ImGui::Checkbox("ループ", &sketchLoop);
		ImGui::SameLine();
		ImGui::Checkbox("なめらか", &sketchSmooth);

		// --- キャンバス本体（上=奥+Z / 右=+X の真上から見た図）---
		const ImVec2 cpos  = ImGui::GetCursorScreenPos();
		const ImVec2 csize = ImVec2(280.0f, 280.0f);
		ImGui::InvisibleButton("##railSketch", csize);
		const bool  chov = ImGui::IsItemHovered();
		ImDrawList* cdl  = ImGui::GetWindowDrawList();
		const ImVec2 cmax = ImVec2(cpos.x + csize.x, cpos.y + csize.y);

		cdl->AddRectFilled(cpos, cmax, IM_COL32(24, 27, 33, 255));
		for ( int i = 0; i <= sketchDiv; ++i ) {
			float f = static_cast< float >( i ) / static_cast< float >( sketchDiv );
			ImU32 gcol = ( i == sketchDiv / 2 ) ? IM_COL32(90, 100, 120, 255) : IM_COL32(48, 54, 64, 255);
			cdl->AddLine({ cpos.x + f * csize.x, cpos.y }, { cpos.x + f * csize.x, cmax.y }, gcol);
			cdl->AddLine({ cpos.x, cpos.y + f * csize.y }, { cmax.x, cpos.y + f * csize.y }, gcol);
		}
		cdl->AddRect(cpos, cmax, IM_COL32(110, 120, 140, 255));

		auto toScr = [&]( const ImVec2& n ) -> ImVec2{
			return ImVec2(cpos.x + n.x * csize.x, cpos.y + n.y * csize.y);
			};

		// クリックで点追加 / 右クリックで1つ戻す
		if ( chov && ImGui::IsMouseClicked(0) ) {
			ImVec2 m = ImGui::GetMousePos();
			float nx = ( m.x - cpos.x ) / csize.x;
			float ny = ( m.y - cpos.y ) / csize.y;
			if ( sketchSnap ) {
				nx = std::round(nx * sketchDiv) / static_cast< float >( sketchDiv );
				ny = std::round(ny * sketchDiv) / static_cast< float >( sketchDiv );
			}
			sketch.push_back({ std::clamp(nx, 0.0f, 1.0f), std::clamp(ny, 0.0f, 1.0f) });
		}
		if ( chov && ImGui::IsMouseClicked(1) && !sketch.empty() ) { sketch.pop_back(); }

		// 仮の線（プレビュー）。なめらかON時は補間して表示する
		std::vector<ImVec2> preview = sketch;
		if ( sketchLoop && sketch.size() >= 2 ) preview.push_back(sketch.front());
		if ( sketchSmooth && preview.size() >= 3 ) {
			std::vector<Vector3> tmp; tmp.reserve(preview.size());
			for ( const auto& p : preview ) tmp.push_back({ p.x, p.y, 0.0f });
			std::vector<Vector3> sm = SmoothPolylineCR(tmp, 10);
			preview.clear();
			for ( const auto& q : sm ) preview.push_back({ q.x, q.y });
		}
		for ( size_t i = 0; i + 1 < preview.size(); ++i ) {
			cdl->AddLine(toScr(preview[i]), toScr(preview[i + 1]), IM_COL32(90, 220, 255, 230), 2.0f);
		}
		for ( size_t i = 0; i < sketch.size(); ++i ) {
			cdl->AddCircleFilled(toScr(sketch[i]), 3.5f,
				( i == 0 ) ? IM_COL32(120, 255, 140, 255) : IM_COL32(235, 235, 235, 255));
		}
		if ( chov && !sketch.empty() ) {
			cdl->AddLine(toScr(sketch.back()), ImGui::GetMousePos(), IM_COL32(255, 255, 255, 90), 1.0f);
		}

		ImGui::TextDisabled("クリック=点追加 / 右クリック=1つ戻す（点 %d）", static_cast< int >( sketch.size() ));
		if ( ImGui::Button("1つ戻す") && !sketch.empty() ) { sketch.pop_back(); }
		ImGui::SameLine();
		if ( ImGui::Button("全消し") ) { sketch.clear(); }
		ImGui::SameLine();
		if ( ImGui::Button("生成（Game Viewで設置）", ImVec2(200.0f, 0.0f)) && sketch.size() >= 2 ) {
			std::vector<Vector3> raw; raw.reserve(sketch.size());
			for ( const auto& n : sketch ) {
				float x = ( n.x - 0.5f ) * sketchSpan;   // 右が +X
				float z = ( 0.5f - n.y ) * sketchSpan;   // 上が +Z（奥）
				raw.push_back({ x, 0.0f, z });
			}
			if ( sketchLoop ) raw.push_back(raw.front());
			pendingStamp_ = sketchSmooth ? SmoothPolylineCR(raw, 10) : raw;
		}
		if ( HasPendingStamp() ) {
			ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Game Viewでクリック→設置 / 右クリックかEscで中止");
		}
	}
	ImGui::Separator();

	// --- 空の路線・押し出し ---
	if ( ImGui::Button("空の路線を追加") ) {
		data_->railLines.push_back(std::vector<Vector3>());
		data_->railTypes.push_back(-1);
		data_->railMotions.push_back(Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
		data_->railGroundTypes.push_back(1); // 既定は Gap（端から落ちられる）
		data_->railVisible.push_back(1);
		data_->railLineModes.push_back(0);
		data_->railNodeHoles.push_back(std::vector<int>());
		currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
		selectedRailNode_ = -1;
		ClearMultiSelection();
		RebuildRailPoints();
	}
	ImGui::SameLine();
	if ( ImGui::Button("末尾にノードを追加 (押し出し)") ) {
		if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) data_->railLines.size() ) {
			Vector3 newPos = { 0.0f, railDrawHeight_, 0.0f };
			if ( !data_->railLines[currentEditRailIndex_].empty() ) {
				newPos = data_->railLines[currentEditRailIndex_].back();
				newPos.z += 5.0f; // 奥へ
			}
			data_->railLines[currentEditRailIndex_].push_back(newPos);
			RebuildRailPoints();
		}
	}
	ImGui::Separator();

	// --- マウスで描く設定 ---
	if ( ImGui::CollapsingHeader("マウスで描く（追加モード・吸着設定）") ) {
		ImGui::Checkbox("マウス追加モード（地面クリックで末尾に追加）", &railDrawMode_);
		if ( railDrawMode_ ) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "対象: レール%d", currentEditRailIndex_);
		}
		ImGui::DragFloat("追加する高さ Y", &railDrawHeight_, 0.05f);
		ImGui::Checkbox("グリッド吸着", &railSnap_);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90.0f);
		ImGui::DragFloat("間隔(m)", &railGridSize_, 0.05f, 0.1f, 10.0f);
		ImGui::Checkbox("直角モード（X/Z軸に固定）", &railAxisLock_);
		ImGui::Checkbox("ノード吸着（他レール端点へ）", &railNodeSnap_);
		ImGui::SameLine();
		ImGui::Checkbox("フリーハンド", &railFreehand_);
		ImGui::TextDisabled("※斜め視点だと奥行き(Z)がずれやすい→「斜め視点に戻す/トップビュー」推奨。\n   地平線方向の遠すぎる位置には置かないように制限済み。");
	}

	// --- 方向ボタンで1マスずつ伸ばす（確実に直角・正確サイズ）---
	if ( ImGui::CollapsingHeader("方向ボタンで伸ばす（確実に直角）") ) {
		static int railStep = 1;
		ImGui::SetNextItemWidth(110.0f);
		ImGui::InputInt("マス数", &railStep);
		if ( railStep < 1 ) railStep = 1;
		const float d = railGridSize_ * static_cast< float >( railStep );
		if ( ImGui::Button(" 左 -X ") ) { AppendRailNodeRelative(-d, 0.0f, 0.0f); RebuildRailPoints(); } ImGui::SameLine();
		if ( ImGui::Button(" 右 +X ") ) { AppendRailNodeRelative(+d, 0.0f, 0.0f); RebuildRailPoints(); }
		if ( ImGui::Button("手前 -Z") ) { AppendRailNodeRelative(0.0f, 0.0f, -d); RebuildRailPoints(); } ImGui::SameLine();
		if ( ImGui::Button(" 奥 +Z ") ) { AppendRailNodeRelative(0.0f, 0.0f, +d); RebuildRailPoints(); }
		if ( ImGui::Button(" 下 -Y ") ) { AppendRailNodeRelative(0.0f, -d, 0.0f); RebuildRailPoints(); } ImGui::SameLine();
		if ( ImGui::Button(" 上 +Y ") ) { AppendRailNodeRelative(0.0f, +d, 0.0f); RebuildRailPoints(); }
	}

	// --- 路線全体を移動（数値での微調整用。ふだんはギズモで動かせる）---
	if ( ImGui::CollapsingHeader("微調整：路線全体を移動") ) {
		static float wholeMove[3] = { 0.0f, 0.0f, 0.0f };
		ImGui::Text("路線全体を移動:");
		ImGui::SetNextItemWidth(180.0f);
		ImGui::DragFloat3("##WholeMove", wholeMove, 0.1f);
		ImGui::SameLine();
		if ( ImGui::Button("適用##WholeMove") ) {
			auto& line = data_->railLines[currentEditRailIndex_];
			for ( auto& n : line ) { n.x += wholeMove[0]; n.y += wholeMove[1]; n.z += wholeMove[2]; }
			wholeMove[0] = wholeMove[1] = wholeMove[2] = 0.0f;
			RebuildRailPoints();
		}
	}
	// --- 形を整えるツール（ワンクリックで 直線 / カーブ / なめらか化）---
	if ( ImGui::CollapsingHeader("形を整える（直線/カーブ/なめらか化）") ) {
		auto& line = data_->railLines[currentEditRailIndex_];
		static float curveAmount = 4.0f;
		static int   curveAxis   = 1; // 0=X(横), 1=Y(上), 2=Z(奥)

		ImGui::Text("形を整える:");

		// 直線：両端を結ぶ直線上に、中間ノードを等間隔で並べ直す
		if ( ImGui::Button("直線にする") ) {
			if ( line.size() >= 2 ) {
				Vector3 a = line.front();
				Vector3 b = line.back();
				int n = static_cast< int >( line.size() );
				for ( int k = 1; k < n - 1; ++k ) {
					float t = static_cast< float >( k ) / static_cast< float >( n - 1 );
					line[k] = { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t };
				}
				RebuildRailPoints();
			}
		}
		ImGui::SameLine();

		// カーブ：直線基準に対して中央が最大の弧(sin)を加える
		if ( ImGui::Button("カーブにする") ) {
			// 2ノードしか無い時は中間ノードを補完して曲げられるようにする
			if ( line.size() == 2 ) {
				Vector3 a = line.front();
				Vector3 b = line.back();
				std::vector<Vector3> filled;
				for ( int k = 0; k < 5; ++k ) {
					float t = static_cast< float >( k ) / 4.0f;
					filled.push_back({ a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t });
				}
				line = filled;
			}
			if ( line.size() >= 3 ) {
				Vector3 a = line.front();
				Vector3 b = line.back();
				int n = static_cast< int >( line.size() );
				for ( int k = 1; k < n - 1; ++k ) {
					float t = static_cast< float >( k ) / static_cast< float >( n - 1 );
					Vector3 base = { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t, a.z + ( b.z - a.z ) * t };
					float bump = std::sin(t * 3.14159265f) * curveAmount; // 中央が最大、両端0
					if ( curveAxis == 0 )      base.x += bump;
					else if ( curveAxis == 1 ) base.y += bump;
					else                       base.z += bump;
					line[k] = base;
				}
				RebuildRailPoints();
			}
		}

		// なめらか化：折れ線を Catmull-Rom 補間でカーブに（手描き・粗い線をきれいに整える）
		if ( ImGui::Button("なめらか化（補間で点を増やす）") ) {
			if ( line.size() >= 3 ) {
				line = SmoothPolylineCR(line, 6);
				RebuildRailPoints();
			}
		}

		ImGui::PushItemWidth(140.0f);
		ImGui::SliderFloat("カーブの強さ", &curveAmount, -10.0f, 10.0f);
		const char* axisNames[] = { "X (横)", "Y (上)", "Z (奥)" };
		ImGui::Combo("カーブの向き", &curveAxis, axisNames, 3);
		ImGui::PopItemWidth();
		ImGui::TextDisabled("※カーブは中間ノードを曲げます。保存→Playでゲームに反映");
		ImGui::Separator();
	}
	ImGui::EndTabItem();
	} // 作成タブ

	// =====================================================================
	//  動きタブ：動くレール（波形・親子付け・リフト生成）を1か所に集約
	// =====================================================================
	if ( ImGui::BeginTabItem("動き (Motion)") ) {
	if ( data_->railLines.empty() || currentEditRailIndex_ < 0
		|| currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		ImGui::TextDisabled("レールがありません（作成タブで作成／管理タブで選択）");
	} else {
		ImGui::Text("編集対象: 路線%d", currentEditRailIndex_);
		ImGui::SameLine();
		ImGui::Checkbox("固定##lockEditTarget", &lockEditTarget_);
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("ONの間、Game View のクリックで編集対象が切り替わらない。\n"
				"リフトのガイドを画面上で調整している時に、うっかり他のレールを触って\n"
				"このタブの内容が消えてしまう事故を防ぐ（切り替えは管理タブから）");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(選択は管理タブ / Game View クリック)");
		// 今のレールが他レールの「ガイド（骨組み）」なら案内を出す（クリックで迷い込んだ時に足場へ戻れる）
		{
			data_->railGuideRails.resize(data_->railLines.size(), -1);
			data_->railMotionTypes.resize(data_->railLines.size(), 0);
			for ( int p = 0; p < ( int ) data_->railLines.size(); ++p ) {
				if ( data_->railGuideRails[p] != currentEditRailIndex_ ) continue;
				if ( data_->railMotionTypes[p] != 3 ) continue;
				ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
					"このレールは 路線%d の足場が走るガイド（骨組み）です", p);
				ImGui::SameLine();
				std::string backLabel = "路線" + std::to_string(p) + "を編集##backToPad" + std::to_string(p);
				if ( ImGui::SmallButton(backLabel.c_str()) ) { SetCurrentRail(p); }
				ImGui::TextDisabled("経路の形は足場側（上のボタンで戻る）の「リフト経路エディタ」で調整できる");
			}
		}
		ImGui::Checkbox("動きをプレビュー##motionTab", &railMotionPreview_);
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("動くレール（親子付けリフト含む）をエディタ中も再生する。\nPlayを押さなくても、リフトの組み方が合っているかその場で確認できる");
		}
		ImGui::Separator();
		// --- このレールの「動き」（ムービングプラットフォーム）。畳んで整理（親子付けは下の欄）---
		if ( ImGui::CollapsingHeader("動くレール（揺れ/波形/ガイド追従）", ImGuiTreeNodeFlags_DefaultOpen) ) {
		Vector4& motion = data_->railMotions[currentEditRailIndex_];
		bool motionChanged = false;
		ImGui::TextDisabled("全て0で停止:");
		motionChanged |= ImGui::DragFloat3("振幅 XYZ (m)", &motion.x, 0.05f);
		ImGui::SetNextItemWidth(110.0f);
		motionChanged |= ImGui::DragFloat("周期 (秒)", &motion.w, 0.05f, 0.1f, 60.0f);
		if ( motion.w < 0.1f ) motion.w = 0.1f;

		// 波形（sin往復 / 端で一時停止つき往復 / 円運動）と位相（複数レールの動きをずらす）
		data_->railMotionTypes.resize(data_->railLines.size(), 0);
		data_->railMotionPhases.resize(data_->railLines.size(), 0.0f);
		int& mtype = data_->railMotionTypes[currentEditRailIndex_];
		const char* waveLabels[] = { "サイン往復", "停止つき往復 (端で一瞬止まる)", "円運動 (X,Z振幅で円)",
		                             "ガイドレール追従 (経路に沿って移動)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("波形", &mtype, waveLabels, 4) ) { motionChanged = true; }
		if ( mtype == 3 ) {
			// ガイド追従：このレール全体が、指定したガイドレールの経路に沿って動く。
			//   らせんガイド＋小さい足場数枚（位相をずらす）＝自動で上へ運ばれる乗り継ぎ足場
			data_->railGuideRails.resize(data_->railLines.size(), -1);
			data_->railGuideStarts.resize(data_->railLines.size(), 0.0f);
			data_->railGuideEnds.resize(data_->railLines.size(), -1.0f);
			data_->railGuideModes.resize(data_->railLines.size(), 0);
			// ガイドは番号入力ではなく一覧から選ぶ（番号を覚えなくてよい）
			int guideSel = data_->railGuideRails[currentEditRailIndex_];
			auto railLabel = [&](int r) -> std::string {
				std::string label = "路線" + std::to_string(r);
				if ( r >= 0 && r < ( int ) data_->railGroups.size() && !data_->railGroups[r].empty() ) {
					label += " [" + data_->railGroups[r] + "]";
				}
				if ( r >= 0 && r < ( int ) data_->railVisible.size() && data_->railVisible[r] == 0 ) {
					label += " (非表示=骨組み)";
				}
				return label;
			};
			ImGui::SetNextItemWidth(220.0f);
			if ( ImGui::BeginCombo("ガイドレール", guideSel < 0 ? "なし" : railLabel(guideSel).c_str()) ) {
				if ( ImGui::Selectable("なし", guideSel < 0) ) {
					data_->railGuideRails[currentEditRailIndex_] = -1;
					guideSel = -1;
					motionChanged = true;
				}
				for ( int r = 0; r < ( int ) data_->railLines.size(); ++r ) {
					if ( r == currentEditRailIndex_ ) continue; // 自分自身は不可
					if ( data_->railLines[r].size() < 2 ) continue;
					if ( ImGui::Selectable(railLabel(r).c_str(), guideSel == r) ) {
						data_->railGuideRails[currentEditRailIndex_] = r;
						guideSel = r;
						motionChanged = true;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			// ワンクリックでガイドを用意：足場の位置から縦6mの骨組みレールを作って割り当てる。
			//   形はすぐ下の「リフト経路エディタ」で点をつかんで自由に変えられる
			if ( ImGui::Button("新規作成##newGuide") ) {
				int platformIdx = currentEditRailIndex_;
				Vector3 base = data_->railLines[platformIdx].empty()
					? Vector3 { 0.0f, 0.0f, 0.0f } : data_->railLines[platformIdx].front();
				std::string guideGroup = ( platformIdx < ( int ) data_->railGroups.size()
					&& !data_->railGroups[platformIdx].empty() )
					? data_->railGroups[platformIdx] : ( "リフト" + std::to_string(platformIdx) );
				// ※AppendRail は全配列を push_back するため、これ以前に取った参照はここで無効になる
				int newGuide = AppendRail({ base, { base.x, base.y + 6.0f, base.z } }, guideGroup);
				data_->railVisible[newGuide]   = 0; // 見えない骨組み
				data_->railRoadModes[newGuide] = 1; // 道は敷かない
				data_->railGuideRails[platformIdx] = newGuide;
				guideSel = newGuide;
				motionChanged = true;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("足場の位置から縦6mのガイド（見えない骨組み）を作って割り当てる。\n形は下の経路エディタでドラッグして自由に変えられる");
			}
			int& guideMode = data_->railGuideModes[currentEditRailIndex_];
			const char* guideModeLabels[] = { "一周ループ (終点まで行くと始点へ戻る)",
			                                  "往復 (端で滑らかに折り返す)",
			                                  "片道 (到着したら止まる)" };
			ImGui::SetNextItemWidth(220.0f);
			if ( ImGui::Combo("動き方##guideMode", &guideMode, guideModeLabels, 3) ) { motionChanged = true; }
			if ( guideMode == 2 && ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("終点に着いたらその場で停車する（出発・到着は緩急つきで滑らか）。\n"
					"「乗ったら動き出す」と組み合わせると、乗って出発→目的地で停車する列車になる。\n"
					"落下でミスすると列車は駅へ戻ってやり直せる");
			}
			// 停車時間：往復=両端で停まる秒数 / 片道=出発までの待ち秒数
			data_->railGuideDwells.resize(data_->railLines.size(), 0.0f);
			float& guideDwell = data_->railGuideDwells[currentEditRailIndex_];
			ImGui::SetNextItemWidth(100.0f);
			if ( ImGui::DragFloat("停車時間 (秒)", &guideDwell, 0.1f, 0.0f, 30.0f, "%.1f") ) {
				if ( guideDwell < 0.0f ) { guideDwell = 0.0f; }
				motionChanged = true;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("往復：両端に着くたびこの秒数だけ停まってから折り返す（乗り降りしやすくなる）\n"
					"片道：出発までこの秒数だけ待つ（「乗ったら動き出す」なら乗ってから発車までの間）\n"
					"一周ループでは使わない（0のままでOK）");
			}
			// 列車式：足場の向きを経路のカーブに合わせて回す（縦向き⇔横向きへ転回できる）
			data_->railGuideAligns.resize(data_->railLines.size(), 0);
			int& guideAlignFlag = data_->railGuideAligns[currentEditRailIndex_];
			bool guideAlignOn = ( guideAlignFlag == 1 );
			if ( ImGui::Checkbox("列車式：向きも経路に合わせて回す##guideAlign", &guideAlignOn) ) {
				guideAlignFlag = guideAlignOn ? 1 : 0;
				motionChanged = true;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("ONにすると、足場がガイドのカーブに合わせて向きを変えながら走る（列車の転回）。\n"
					"奥向きの道→カーブ→横向きの道のようなガイドを引けば、乗せたまま曲がっていく。\n"
					"OFF=向きを保ったまま平行移動（従来どおり。真上に運ぶだけのリフト向け）");
			}
			float& guideStart = data_->railGuideStarts[currentEditRailIndex_];
			float& guideEnd   = data_->railGuideEnds[currentEditRailIndex_];
			ImGui::SetNextItemWidth(100.0f);
			if ( ImGui::DragFloat("区間 ここから(m)", &guideStart, 0.5f, 0.0f, 100000.0f, "%.1f") ) {
				if ( guideStart < 0.0f ) guideStart = 0.0f;
				motionChanged = true;
			}
			ImGui::SameLine();
			// 「終点まで」はチェックボックスで切り替える（-1をドラッグで抜けようとすると
			// 毎フレーム-1へ戻されて操作できないため。外すと具体的な距離を指定できる）
			bool guideToEnd = ( guideEnd < 0.0f );
			if ( ImGui::Checkbox("終点まで##guideToEnd", &guideToEnd) ) {
				guideEnd = guideToEnd ? -1.0f : guideStart + 10.0f;
				motionChanged = true;
			}
			if ( !guideToEnd ) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(100.0f);
				if ( ImGui::DragFloat("ここまで(m)##guideEnd", &guideEnd, 0.5f, 0.0f, 100000.0f, "%.1f") ) {
					if ( guideEnd < 0.0f ) guideEnd = 0.0f;
					motionChanged = true;
				}
			}
			ImGui::TextDisabled("周期=1周期の秒数（一周ループ=1周 / 往復=行って帰る1往復）\n"
				"位相=スタート位置(0〜1) / 振幅は使わない\n"
				"区間: ガイドのどこからどこまでを動くか（「終点まで」を外すと距離指定）\n"
				"「往復」ならヨッシー1-1の列車のように縦⇔横も緩急つきで滑らかに折り返す\n"
				"ゲームビューの水色の線＝実際に通る経路（プレビューで実際に動かして確認可）");

			// --- リフト経路エディタ（ETOS風：2D図で点をつかんで編集・実際の動きを常時表示）---
			DrawRailPathEditor(guideSel, true);
		}
		float& mphase = data_->railMotionPhases[currentEditRailIndex_];
		ImGui::SetNextItemWidth(140.0f);
		if ( ImGui::SliderFloat("位相 (0〜1)", &mphase, 0.0f, 1.0f, "%.2f") ) { motionChanged = true; }

		// 動き出しのタイミング：最初から動く ⇔ プレイヤーが乗ったら動き出す（ヨッシー式）
		data_->railMotionTriggers.resize(data_->railLines.size(), 0);
		int& mtrigger = data_->railMotionTriggers[currentEditRailIndex_];
		const char* triggerLabels[] = { "最初から動く", "乗ったら動き出す (ヨッシー式)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("動き出し", &mtrigger, triggerLabels, 2) ) { motionChanged = true; }
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("「乗ったら動き出す」＝プレイヤーがこのレールに乗るまで待機し、\n"
				"乗った瞬間から動き出す（以後は降りても動き続ける）。\n"
				"ガイド追従リフトに設定すると、乗ってから出発する列車になる");
		}

		if ( motionChanged ) { ++railVersion_; } // ゲーム側へ即反映
		ImGui::TextDisabled("例: 振幅(0,0,3) 周期2 → 奥行き±3mを2秒で往復 / 位相0.5=半周期ずれ");
		}
		ImGui::Separator();
		// --- 後から出現する道（ヨッシー式：指定レールに乗るとこの道が現れる）---
		if ( ImGui::CollapsingHeader("出現する道（後から現れる）") ) {
			data_->railAppearTriggers.resize(data_->railLines.size(), -1);
			int appearSel = data_->railAppearTriggers[currentEditRailIndex_];
			bool appearOn = ( appearSel >= 0 );
			if ( ImGui::Checkbox("後から出現する道にする##appear", &appearOn) ) {
				int defaultTrigger = ( currentEditRailIndex_ == 0 && ( int ) data_->railLines.size() > 1 ) ? 1 : 0;
				data_->railAppearTriggers[currentEditRailIndex_] = appearOn ? defaultTrigger : -1;
				appearSel = data_->railAppearTriggers[currentEditRailIndex_];
				++railVersion_;
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("プレイ開始時はこの道が消えていて、乗ることも着地することもできない。\n"
					"下で選んだ「発動レール」にプレイヤーが乗った瞬間、下からせり上がって出現する");
			}
			if ( appearOn ) {
				auto appearLabel = [&](int r) -> std::string {
					std::string label = "路線" + std::to_string(r);
					if ( r >= 0 && r < ( int ) data_->railGroups.size() && !data_->railGroups[r].empty() ) {
						label += " [" + data_->railGroups[r] + "]";
					}
					return label;
				};
				ImGui::SetNextItemWidth(220.0f);
				if ( ImGui::BeginCombo("発動レール（ここに乗ると出現）",
					appearSel >= 0 ? appearLabel(appearSel).c_str() : "未設定") ) {
					for ( int r = 0; r < ( int ) data_->railLines.size(); ++r ) {
						if ( r == currentEditRailIndex_ ) continue; // 自分自身では発動できない（乗れないので）
						if ( data_->railLines[r].size() < 2 ) continue;
						if ( ImGui::Selectable(appearLabel(r).c_str(), appearSel == r) ) {
							data_->railAppearTriggers[currentEditRailIndex_] = r;
							++railVersion_;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::TextDisabled("エディタ中は普通に表示される（プレイで消える）\n"
					"道の種類が「SDF溶け道」なら溶け演出つきで出現する\n"
					"※出現前に見えるつなぎ目を避けるため、他の道と溶接しない独立レール推奨");
			}
		}
		ImGui::Separator();
		// --- 親子付け：このレールを「親レール」の経路に沿って動かす（リフト/エスカレーター）---
		//   波形コンボを探さなくても、ここに番号を入れるだけで自動的にガイド追従になる
		data_->railGuideRails.resize(data_->railLines.size(), -1);
		data_->railMotionTypes.resize(data_->railLines.size(), 0);
		{
			int& parentRail = data_->railGuideRails[currentEditRailIndex_];
			int prevParent = parentRail;
			ImGui::SetNextItemWidth(110.0f);
			if ( ImGui::InputInt("親レール（経路に沿って動く）", &parentRail) ) {
				if ( parentRail < -1 ) parentRail = -1;
				if ( parentRail >= ( int ) data_->railLines.size() ) parentRail = ( int ) data_->railLines.size() - 1;
				if ( parentRail == currentEditRailIndex_ ) parentRail = -1; // 自分自身は不可
				if ( parentRail >= 0 ) { data_->railMotionTypes[currentEditRailIndex_] = 3; } // 自動でガイド追従へ
				else if ( prevParent >= 0 && data_->railMotionTypes[currentEditRailIndex_] == 3 ) {
					data_->railMotionTypes[currentEditRailIndex_] = 0; // 外したら通常の波形へ戻す
				}
				++railVersion_;
			}
			if ( parentRail >= 0 ) {
				ImGui::TextDisabled("親=路線%d の経路を一周し続ける（周期=1周の秒数 / 位相=スタート位置0〜1）\n"
					"同じ親の足場を複数用意して位相をずらすと数珠つなぎのリフトになる", parentRail);
			} else if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("番号を入れるとこのレール全体が親レールの経路に沿って動き続ける（-1=なし）\n"
					"親レール側は「道=なし」「ゲームに表示OFF」にすると見えない骨組みになる");
			}

			// ワンボタン簡易化：選択レールを親（骨組み）にして、追従する足場一式を自動生成。
			//   位相は等間隔＝数珠つなぎ。グループも自動で付くので後から一括調整できる
			static int   liftCount  = 4;
			static float liftPeriod = 12.0f;
			static float liftPadLen = 2.0f;
			ImGui::SetNextItemWidth(80.0f);
			ImGui::DragInt("足場数##lift", &liftCount, 1, 1, 12);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.0f);
			ImGui::DragFloat("1周(秒)##lift", &liftPeriod, 0.5f, 2.0f, 60.0f, "%.1f");
			if ( ImGui::Button("このレールを親にしてリフト足場を一括生成") ) {
				int parentIdx = currentEditRailIndex_;
				std::string liftGroup = "リフト" + std::to_string(parentIdx);
				Vector3 base = data_->railLines[parentIdx].front();
				for ( int i = 0; i < liftCount; ++i ) {
					int padIdx = AppendRail({ { base.x - liftPadLen * 0.5f, base.y, base.z },
					                          { base.x + liftPadLen * 0.5f, base.y, base.z } }, liftGroup);
					data_->railLineModes[padIdx]    = 1;         // 足場は直線
					data_->railGuideRails[padIdx]   = parentIdx; // 親に追従
					data_->railMotionTypes[padIdx]  = 3;         // ガイドレール追従
					data_->railMotionPhases[padIdx] = ( float ) i / ( float ) liftCount; // 等間隔
					data_->railMotions[padIdx].w    = liftPeriod;
				}
				RebuildRailPoints();
			}
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("選択中のレールを骨組みとして、追従する足場を位相等間隔で一括生成。\n"
					"これ1回で「動く乗り継ぎリフト」が完成する（親は道=なし・表示OFF推奨）");
			}
		}

		// --- グループ一括の動き（同じグループ名を付けた足場をまとめてリフト化）---
		{
			std::string& currentGroup = data_->railGroups[currentEditRailIndex_];
			if ( currentGroup.empty() ) {
				ImGui::TextDisabled("グループ一括の操作（親追従/自動リフト化）は、管理タブでグループ名を付けると使える");
			} else {
				// グループ一括で親レールへ追従させる：既に置いてある足場群をまとめて動かす最短ルート。
				//   位相はグループ内で自動的に等間隔（数珠つなぎ）に割り振る
				static int   groupParentRail   = -1;
				static float groupParentPeriod = 12.0f;
				ImGui::SetNextItemWidth(70.0f);
				ImGui::InputInt("親##groupParent", &groupParentRail);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				ImGui::DragFloat("1周(秒)##groupParent", &groupParentPeriod, 0.5f, 2.0f, 60.0f, "%.1f");
				if ( ImGui::Button("グループ全体を親レールに追従させる") ) {
					int railTotal = ( int ) data_->railLines.size();
					if ( groupParentRail >= 0 && groupParentRail < railTotal ) {
						data_->railGuideRails.resize(railTotal, -1);
						data_->railMotionTypes.resize(railTotal, 0);
						data_->railMotionPhases.resize(railTotal, 0.0f);
						data_->railMotions.resize(railTotal, Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
						// グループの所属メンバー（親自身は除く）を集めて位相を等間隔に割り振る
						std::vector<int> members;
						for ( int g = 0; g < railTotal; ++g ) {
							if ( data_->railGroups[g] == currentGroup && g != groupParentRail ) { members.push_back(g); }
						}
						for ( int m = 0; m < ( int ) members.size(); ++m ) {
							int g = members[m];
							data_->railGuideRails[g]   = groupParentRail;
							data_->railMotionTypes[g]  = 3; // ガイドレール追従
							data_->railMotionPhases[g] = ( float ) m / ( float ) members.size();
							data_->railMotions[g].w    = groupParentPeriod;
						}
						++railVersion_;
					}
				}
				if ( ImGui::IsItemHovered() ) {
					ImGui::SetTooltip("グループ内の全レールを指定した親レールの経路に追従させる。\n位相は自動で等間隔＝数珠つなぎのリフトになる。\n「動きをプレビュー」ONでその場で動きを確認できる");
				}

				// 全自動リフト化：ガイドの用意も番号入力も不要のワンボタン。
				//   1) 足場の並び（低い→高い）からガイドのスプラインを自動生成（道なし・非表示の骨組み）
				//   2) 足場を出発点に揃え、全員をガイド追従＋位相等間隔にする ＝ その場でリフト完成
				if ( ImGui::Button("このグループを自動でリフト化（ガイドも自動生成）") ) {
					int railTotal = ( int ) data_->railLines.size();
					std::vector<std::pair<float, int>> sortedMembers; // (中心Y, レール番号)
					std::vector<Vector3> memberCenters(railTotal);
					for ( int g = 0; g < railTotal; ++g ) {
						if ( data_->railGroups[g] != currentGroup ) continue;
						if ( data_->railLines[g].empty() ) continue;
						Vector3 center { 0.0f, 0.0f, 0.0f };
						for ( const auto& node : data_->railLines[g] ) {
							center.x += node.x; center.y += node.y; center.z += node.z;
						}
						float inv = 1.0f / ( float ) data_->railLines[g].size();
						center = { center.x * inv, center.y * inv, center.z * inv };
						memberCenters[g] = center;
						sortedMembers.push_back({ center.y, g });
					}
					if ( sortedMembers.size() >= 2 ) {
						std::sort(sortedMembers.begin(), sortedMembers.end());
						// 1) ガイドレール：足場中心を低い順に結ぶスプライン（道なし・非表示の骨組み）
						std::vector<Vector3> guideLine;
						for ( const auto& member : sortedMembers ) { guideLine.push_back(memberCenters[member.second]); }
						// AppendRail が配列を再確保しても安全なよう、グループ名は先にコピーしておく
						std::string groupNameCopy = currentGroup;
						int guideIdx = AppendRail(guideLine, groupNameCopy + "_ガイド");
						data_->railVisible[guideIdx]   = 0; // 見えない骨組み
						data_->railRoadModes[guideIdx] = 1; // 道なし

						// 2) 足場を出発点（最下段の中心）へ揃えてから、全員をガイド追従にする。
						//    追従はオフセット式なので、出発点を揃えないと元の位置分だけズレて回ってしまう
						Vector3 base = memberCenters[sortedMembers[0].second];
						for ( int m = 0; m < ( int ) sortedMembers.size(); ++m ) {
							int g = sortedMembers[m].second;
							Vector3 diff = { base.x - memberCenters[g].x,
							                 base.y - memberCenters[g].y,
							                 base.z - memberCenters[g].z };
							for ( auto& node : data_->railLines[g] ) {
								node.x += diff.x; node.y += diff.y; node.z += diff.z;
							}
							data_->railGuideRails[g]   = guideIdx;
							data_->railMotionTypes[g]  = 3; // ガイドレール追従
							data_->railMotionPhases[g] = ( float ) m / ( float ) sortedMembers.size();
							data_->railMotions[g].w    = groupParentPeriod;
						}
						RebuildRailPoints();
					}
				}
				if ( ImGui::IsItemHovered() ) {
					ImGui::SetTooltip("足場の今の並び（低い→高い）をそのまま経路にして、グループ全体を自動で動くリフトにする。\n"
						"「動きをプレビュー」ONですぐ動きを確認できる（1周の秒数は上の欄で変更可）");
				}
			}
		}
	}
	ImGui::EndTabItem();
	} // 動きタブ

	// =====================================================================
	//  配置物タブ：ステージに置く「モノ」（コイン・穴）の編集
	// =====================================================================
	if ( ImGui::BeginTabItem("配置物 (Items)") ) {
	if ( data_->railLines.empty() || currentEditRailIndex_ < 0
		|| currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		ImGui::TextDisabled("レールがありません（作成タブで作成／管理タブで選択）");
	} else {
		ImGui::Text("編集対象: 路線%d", currentEditRailIndex_);
		ImGui::SameLine();
		ImGui::TextDisabled("(路線の選択は管理タブ / Game View クリック)");
		ImGui::Separator();
	// コイン・ブロックの配置UIは専用の「配置エディタ」ウィンドウへ集約した（DrawItemWindow）
	ImGui::TextDisabled("コインとブロックの配置 → 「配置エディタ」ウィンドウへ移動しました");
	ImGui::Separator();

	// この路線の穴配列をノード数に合わせて整える（外はDrawWindow冒頭で整える）
	if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
		data_->railNodeHoles[currentEditRailIndex_].resize(
			data_->railLines[currentEditRailIndex_].size(), 0);
	}

	if ( ImGui::CollapsingHeader("穴（落下区間の指定）", ImGuiTreeNodeFlags_DefaultOpen) ) {
	ImGui::TextDisabled("「穴」にチェック→そのノード付近が落下区間（ジャンプで飛び越え可）");

	// --- 穴の一括指定 ---
	{
		ImGui::TextDisabled("穴＝そこに来たら落下する区間（落とし穴）。一気に指定できる↓");

		// 1クリックで「この路線まるごと」穴に/解除
		if ( ImGui::Button("この路線を全部 穴にする") ) {
			SelectWholeRail(currentEditRailIndex_);
			SetSelectionHole(true);
		}
		ImGui::SameLine();
		if ( ImGui::Button("この路線の穴を全解除") ) {
			SelectWholeRail(currentEditRailIndex_);
			SetSelectionHole(false);
		}

		// マウス箱選択 → その範囲だけ穴に（部分指定）
		int selCount = ( int ) GetMultiSelection().size();
		ImGui::Text("マウス選択: %d ノード", selCount);
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("Game View で何もない所からドラッグ→ノードを箱選択。\nその後このボタンで選択ノードだけを穴/通常に切り替え。");
		}
		ImGui::BeginDisabled(selCount == 0);
		if ( ImGui::Button("選択を穴にする") ) { SetSelectionHole(true); }
		ImGui::SameLine();
		if ( ImGui::Button("選択の穴を解除") ) { SetSelectionHole(false); }
		ImGui::EndDisabled();
	}
	ImGui::Separator();

	}
	}
	ImGui::EndTabItem();
	} // 配置物タブ

	// =====================================================================
	//  接続タブ（§5 管理UI）：溶接/T字の接続一覧・選択ハイライト・切断
	//   接続はレール座標から毎回導出する（保存しない派生データ）
	// =====================================================================
	if ( ImGui::BeginTabItem("接続 (Joints)") ) {
	// --- 接続ツール一式：溶接/連結/自動スナップ/足元ガイド（畳んで整理）---
	if ( ImGui::CollapsingHeader("接続ツール（溶接・連結・自動スナップ）", ImGuiTreeNodeFlags_DefaultOpen) ) {
	// --- 端点を溶接（データ自体をぴったり結合 → 実行時に座標がズレない）---
	if ( ImGui::Button("端点を溶接（近い端点をぴったり結合）") ) {
		const float kWeld = 0.7f; // ゲーム側の接続判定と同じ距離
		auto& lines = data_->railLines;

		auto weld = [&](Vector3& p, Vector3& q) -> bool{
			float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
			if ( dx * dx + dy * dy + dz * dz < kWeld * kWeld ) {
				Vector3 m = { ( p.x + q.x ) * 0.5f, ( p.y + q.y ) * 0.5f, ( p.z + q.z ) * 0.5f };
				p = m; q = m;
				return true;
			}
			return false;
			};

		bool any = false;
		for ( size_t a = 0; a < lines.size(); ++a ) {
			if ( lines[a].size() < 2 ) continue;
			// 自分の front-back（円状＝ループ用。2ノードだと潰れるので3ノード以上）
			if ( lines[a].size() >= 3 ) { any |= weld(lines[a].front(), lines[a].back()); }
			for ( size_t b = a + 1; b < lines.size(); ++b ) {
				if ( lines[b].size() < 2 ) continue;
				any |= weld(lines[a].front(), lines[b].front());
				any |= weld(lines[a].front(), lines[b].back());
				any |= weld(lines[a].back(),  lines[b].front());
				any |= weld(lines[a].back(),  lines[b].back());
			}
		}
		if ( any ) { RebuildRailPoints(); }
	}
	ImGui::TextDisabled("※繋げたい端点同士を結合し、緑線とノードのズレを無くす");

	// --- 近い線を連結（端点 → 他レール本体の最近点へ。線の途中での合流もOK）---
	if ( ImGui::Button("近い線を連結（端点を他レールの途中へ繋ぐ）") ) {
		ConnectNearbyLines();
	}
	ImGui::TextDisabled("※端点が他レールの近く(約1.2m)にあれば、その線の途中に共有ノードを足して連結");

	// --- 自動スナップ接続の設定（§5。プラレール風：端点ドラッグで自動接続）---
	ImGui::Checkbox("自動スナップ（端点ドラッグで自動接続）", &railAutoSnap_);
	if ( railAutoSnap_ ) {
		ImGui::SetNextItemWidth(140.0f);
		ImGui::SliderFloat("スナップ距離(m)", &railSnapDistance_, 0.3f, 3.0f, "%.1f");
		ImGui::Checkbox("高さも自動で合わせる（真上から近ければ接続）", &railSnapMatchHeight_);
		ImGui::TextDisabled("※立体交差（高さの違う線をまたがせる）を作りたい時はOFF");
	}
	ImGui::Checkbox("端点の足元ガイド（選択レールの端から地面へ縦線＋Y値）", &railEndGuide_);
	}
		const auto& lines = data_->railLines;
		auto dist3 = [](const Vector3& p, const Vector3& q) -> float{
			float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
			return std::sqrt(dx * dx + dy * dy + dz * dz);
			};
		const float kConn = 0.7f; // ランタイム(BuildRailConnections)の接続判定と同じ

		// 接続の導出：端点↔端点=溶接 / 端点↔他レール本体のノード上=T字
		struct Conn { bool isWeld; int railA; bool frontA; int railB; int nodeB; Vector3 pos; };
		std::vector<Conn> conns;
		for ( size_t a = 0; a < lines.size(); ++a ) {
			if ( lines[a].size() < 2 ) continue;
			for ( int fs = 0; fs < 2; ++fs ) {
				bool front = ( fs == 0 );
				Vector3 ep = front ? lines[a].front() : lines[a].back();
				// 溶接（端点同士）。ペア重複を避けるため相手は a より後ろだけ見る
				bool found = false;
				for ( size_t b = a + 1; b < lines.size() && !found; ++b ) {
					if ( lines[b].size() < 2 ) continue;
					const int ends[2] = { 0, ( int ) lines[b].size() - 1 };
					for ( int en : ends ) {
						if ( dist3(ep, lines[b][en]) < kConn ) {
							conns.push_back({ true, ( int ) a, front, ( int ) b, en, ep });
							found = true;
							break;
						}
					}
				}
				if ( found ) continue;
				// T字（端点が他レールの途中ノードの上にある）
				for ( size_t b = 0; b < lines.size() && !found; ++b ) {
					if ( b == a || lines[b].size() < 3 ) continue;
					for ( int nn = 1; nn + 1 < ( int ) lines[b].size(); ++nn ) {
						if ( dist3(ep, lines[b][nn]) < kConn ) {
							conns.push_back({ false, ( int ) a, front, ( int ) b, nn, ep });
							found = true;
							break;
						}
					}
				}
			}
		}

		ImGui::Text("接続一覧（%d件）:", ( int ) conns.size());
		ImGui::TextDisabled("※行クリックで該当ノードを選択ハイライト。切断で 0.3m 離す（Ctrl+Zで戻せる）");
		int disconnect = -1;
		for ( int ci = 0; ci < ( int ) conns.size(); ++ci ) {
			const Conn& c = conns[ci];
			ImGui::PushID(1000 + ci);
			char label[96];
			snprintf(label, sizeof(label), "%s  路線%d(%s) ↔ 路線%d%s",
				c.isWeld ? "溶接" : "T字",
				c.railA, c.frontA ? "先頭" : "末尾",
				c.railB, c.isWeld ? ( c.nodeB == 0 ? "(先頭)" : "(末尾)" ) : "(途中)");
			if ( ImGui::Selectable(label, selectedConnection_ == ci, 0, ImVec2(250.0f, 0.0f)) ) {
				selectedConnection_ = ci;
				// 該当ノードを選択（GameView のノードハイライトに乗る）
				SetCurrentRail(c.railA);
				SelectSingleNode(c.railA, c.frontA ? 0 : ( int ) lines[c.railA].size() - 1);
			}
			if ( ImGui::IsItemHovered() ) { // ホバーで Game View の両レールと接続点を光らせる
				hoveredListRail_  = c.railA;
				hoveredListRailB_ = c.railB;
				hoveredConnPos_   = c.pos;
				hoveredConnValid_ = true;
			}
			ImGui::SameLine();
			if ( ImGui::SmallButton("切断") ) { disconnect = ci; }
			ImGui::PopID();
		}

		// 切断：端点を自レールの内側へ 0.3m 引っ込める（即再スナップの無限ループ防止距離）
		if ( disconnect >= 0 && disconnect < ( int ) conns.size() ) {
			const Conn& c = conns[disconnect];
			auto pullBack = [&](int rail, bool front){
				auto& line = data_->railLines[rail];
				if ( line.size() < 2 ) return;
				Vector3& ep   = front ? line.front() : line.back();
				Vector3& next = front ? line[1] : line[line.size() - 2];
				Vector3 d = { next.x - ep.x, next.y - ep.y, next.z - ep.z };
				float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
				if ( l < 1e-4f ) return;
				const float kSep = 0.3f;
				ep = { ep.x + d.x / l * kSep, ep.y + d.y / l * kSep, ep.z + d.z / l * kSep };
			};
			pullBack(c.railA, c.frontA);
			if ( c.isWeld ) { pullBack(c.railB, c.nodeB == 0); } // 溶接は両側を離す
			selectedConnection_ = -1;
			RebuildRailPoints(); // 道・パッチ・ジョイントの再生成（履歴は CommitIfStable が拾う）
		}

	// --- 未接続の端の一覧：繋がっていない端をマップ全体から洗い出し、ワンクリックで修復 ---
	if ( ImGui::CollapsingHeader("未接続の端の一覧", ImGuiTreeNodeFlags_DefaultOpen) ) {
		int shownEnds = 0;
		for ( int r = 0; r < ( int ) data_->railLines.size(); ++r ) {
			if ( data_->railLines[r].size() < 2 ) continue;
			for ( int side = 0; side < 2; ++side ) {
				bool front = ( side == 0 );
				if ( IsRailEndConnected(r, front) ) continue; // ループもここで弾かれる
				Vector3 endPos = front ? data_->railLines[r].front() : data_->railLines[r].back();
				ImGui::PushID(r * 2 + side);
				char rowLabel[96];
				snprintf(rowLabel, sizeof(rowLabel), "路線%d %s @ (%.1f, %.1f, %.1f)",
					r, front ? "先頭" : "末尾", endPos.x, endPos.y, endPos.z);
				if ( ImGui::Selectable(rowLabel) ) { SelectWholeRail(r); }
				if ( ImGui::IsItemHovered() ) {
					hoveredListRail_  = r;
					hoveredListRailB_ = -1;
					hoveredConnPos_   = endPos;
					hoveredConnValid_ = true;
				}
				// 真上から見て近い相手がいれば、その場で高さを合わせて接続できる
				SnapTarget repairTarget = FindSnapTargetXZ(r, front, railSnapDistance_ * 2.0f);
				if ( repairTarget.valid ) {
					ImGui::SameLine();
					if ( ImGui::SmallButton("高さを合わせて接続") ) { ConnectToTarget(r, front, repairTarget); }
				}
				ImGui::PopID();
				++shownEnds;
			}
		}
		if ( shownEnds == 0 ) { ImGui::TextDisabled("未接続の端はありません（全部つながっています）"); }
	}
	{
		const char* jointModes[] = { "エディタのみ", "常に表示", "非表示" };
		ImGui::SetNextItemWidth(140.0f);
		ImGui::Combo("ジョイント表示", &railJointVisible_, jointModes, 3);
	}
	ImGui::Separator();
		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
	}
	ImGui::End();
#endif
}

// =====================================================================
//  配置エディタ（コイン・ブロック専用ウィンドウ）
//   「置く」系の作業をレール編集から分離して集約。ドッキングで
//   レールエディタの隣に並べたり、外に出したりできる
// =====================================================================
void RailEditor::DrawItemWindow(){
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(380, 500), ImGuiCond_FirstUseEver);
	ImGui::Begin("配置エディタ (Coin/Block)");
	if ( data_->railLines.empty() || currentEditRailIndex_ < 0
		|| currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		ImGui::TextDisabled("レールがありません（レールエディタの作成タブで作る）");
	} else {
		ImGui::Text("編集対象: 路線%d", currentEditRailIndex_);
		ImGui::SameLine();
		ImGui::TextDisabled("(路線の選択は管理タブ / Game View クリック)");
		ImGui::Separator();
	// --- 収集物（コイン）：選択レールに沿って等間隔に置く。Playで触れると取得 ---
	if ( ImGui::CollapsingHeader("収集物（コイン）", ImGuiTreeNodeFlags_DefaultOpen) ) {
		const auto& coinLine = data_->railLines[currentEditRailIndex_];
		int railCoinCount = 0;
		for ( const auto& coin : data_->coins ) {
			if ( coin.rail == currentEditRailIndex_ ) ++railCoinCount;
		}
		ImGui::Text("この路線: %d枚 / マップ全体: %d枚", railCoinCount, ( int ) data_->coins.size());

		static int   coinCount  = 5;    // 並べる枚数
		static float coinHeight = 1.0f; // レールからの高さ(m)
		static float coinMargin = 0.1f; // 両端に置かない割合（0=端まで置く）
		ImGui::PushItemWidth(140.0f);
		ImGui::SliderInt("枚数", &coinCount, 1, 30);
		ImGui::DragFloat("高さ(m)", &coinHeight, 0.05f, 0.0f, 5.0f, "%.2f");
		ImGui::SliderFloat("端の余白（割合）", &coinMargin, 0.0f, 0.4f);
		ImGui::PopItemWidth();

		if ( ImGui::Button("選択レールに等間隔で並べ直す") && coinLine.size() >= 2 ) {
			// 並べ直し方式：先にこの路線の既存コインを消す（連打しても重複して溜まらない）
			std::erase_if(data_->coins,
				[this](const CoinData& coin){ return coin.rail == currentEditRailIndex_; });
			// 折れ線の長さを測り、両端の余白を除いた範囲へ等間隔に配置する
			float total = 0.0f;
			for ( size_t k = 1; k < coinLine.size(); ++k ) {
				float dx = coinLine[k].x - coinLine[k - 1].x;
				float dy = coinLine[k].y - coinLine[k - 1].y;
				float dz = coinLine[k].z - coinLine[k - 1].z;
				total += std::sqrt(dx * dx + dy * dy + dz * dz);
			}
			float s0 = total * coinMargin;
			float s1 = total * ( 1.0f - coinMargin );
			for ( int k = 0; k < coinCount; ++k ) {
				float t = ( coinCount > 1 ) ? ( float ) k / ( float ) ( coinCount - 1 ) : 0.5f;
				CoinData coin;
				coin.rail   = currentEditRailIndex_;
				coin.dist   = s0 + ( s1 - s0 ) * t;
				coin.height = coinHeight;
				data_->coins.push_back(coin);
			}
			RebuildRailPoints(); // 世代番号を進めてゲーム側にコイン再生成を通知
		}
		ImGui::SameLine();
		if ( ImGui::Button("この路線のコインを削除") ) {
			std::erase_if(data_->coins,
				[this](const CoinData& coin){ return coin.rail == currentEditRailIndex_; });
			RebuildRailPoints();
		}
		if ( !data_->coins.empty() ) {
			if ( ImGui::Button("全コイン削除") ) {
				data_->coins.clear();
				RebuildRailPoints();
			}
		}
		ImGui::TextDisabled("※エディタ中も表示。Playで触れると取得（Play開始のたびに復活）");
		ImGui::Separator();
	}
	// --- ブロック（乗れる/ぶつかる1m角。マリオメーカー風のペイント配置）---
	if ( ImGui::CollapsingHeader("ブロック（乗れる/ぶつかる）", ImGuiTreeNodeFlags_DefaultOpen) ) {
		bool paintOn = blockPaintMode_;
		if ( paintOn ) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.25f, 1.0f)); }
		if ( ImGui::Button(paintOn ? "ブロック配置モード中（クリックで終了）" : "ブロック配置モードを開始", ImVec2(-1.0f, 26.0f)) ) {
			blockPaintMode_ = !blockPaintMode_;
		}
		if ( paintOn ) { ImGui::PopStyleColor(); }
		if ( ImGui::IsItemHovered() ) {
			ImGui::SetTooltip("Game View でレールの近くをクリック：左=置く / 右クリック=消す（ブロックを左クリックでも消える）\n"
				"マウスを上に動かすと高い段、横に動かすと道の脇に置ける。押しっぱなしで連続配置\n"
				"1マス=1m。道の中心のブロックは乗れて壁になる（脇にずらしたものは飾り）");
		}
		if ( blockPaintMode_ ) {
			// 置く/消す のモード切替（右クリックでも常に消せるが、明示モードがあると確実）
			int eraseModeInt = blockPaintErase_ ? 1 : 0;
			ImGui::RadioButton("置く##blockmode", &eraseModeInt, 0);
			ImGui::SameLine();
			ImGui::RadioButton("消しゴム##blockmode", &eraseModeInt, 1);
			blockPaintErase_ = ( eraseModeInt == 1 );
			// ブロックの種類（見た目＋性質）
			const char* blockTypeNames[] = {
				"スポンジ (黄)", "段ボール層", "斜面 45°", "ゆるい斜面 26°",
				"ジャンプ台 (緑)", "？ブロック (金)", "すり抜け床 (白)",
				"横長ブロック (2m)", "台座ブロック (2×2m)" };
			ImGui::SetNextItemWidth(200.0f);
			ImGui::Combo("種類##blocktype", &blockPaintType_, blockTypeNames, IM_ARRAYSIZE(blockTypeNames));
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("斜面＝歩いて登れる坂（隣のブロックへ向けて自動で向く）\n"
					"ジャンプ台＝上に飛び乗ると大きく跳ね返る\n"
					"？ブロック＝下から頭突きするとコインが出る（1回きり。Playごとに復活）\n"
					"すり抜け床＝上には乗れて、下と横からは通り抜けられる一方通行の床\n"
					"横長＝進行方向2mの長い足場を1個で。台座＝道幅いっぱいの2×2m土台\n"
					"ブロックを2個以上並べると、つなぎ目に紙花が自動で咲く");
			}

			// 塗り方（1クリックの形）
			const char* blockShapeNames[] = {
				"1個ずつ", "柱（下まで縦に埋める）", "階段（ドラッグで1段ずつ上がる）",
				"範囲フィル（ドラッグで矩形）" };
			ImGui::SetNextItemWidth(240.0f);
			ImGui::Combo("塗り方##blockshape", &blockPaintShape_, blockShapeNames, 4);
			if ( ImGui::IsItemHovered() ) {
				ImGui::SetTooltip("柱：クリックした段から地面まで一気に積む（塔や壁の土台に）。\n"
					"　　消しゴム時はそのセルの縦一列をまとめて消す\n"
					"階段：押しっぱなしでドラッグすると1マスごとに1段ずつ高くなる\n"
					"範囲フィル：ドラッグの始点〜終点の矩形（距離×高さ）を一括で塗る/消す");
			}
		}
		ImGui::Text("マップ全体: %d個", ( int ) data_->blocks.size());
		if ( !data_->blocks.empty() ) {
			ImGui::SameLine();
			if ( ImGui::SmallButton("全ブロック削除") ) {
				data_->blocks.clear();
				++blockVersion_;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(Ctrl+Zで戻せる)");
		}
		ImGui::TextDisabled("※階段・壁・足場を作ってレール上のアクションを増やせる");
		ImGui::TextDisabled("　上が空いたブロックは明るく、積んだ内部は暗く表示される");
		ImGui::Separator();
	}

		// --- 管理（個数の内訳と一括操作）---
		if ( ImGui::CollapsingHeader("管理（個数と一括削除）", ImGuiTreeNodeFlags_DefaultOpen) ) {
			int typeCounts[9] = {};
			int railBlockCount = 0;
			for ( const auto& block : data_->blocks ) {
				if ( block.type >= 0 && block.type < 9 ) { ++typeCounts[block.type]; }
				if ( block.rail == currentEditRailIndex_ ) { ++railBlockCount; }
			}
			const char* typeNamesShort[] = { "スポンジ", "段ボール", "斜面45", "斜面26", "バネ", "？", "すり抜け", "横長", "台座" };
			ImGui::TextDisabled("ブロックの内訳:");
			for ( int t = 0; t < 9; ++t ) {
				if ( typeCounts[t] > 0 ) { ImGui::Text("  %s: %d個", typeNamesShort[t], typeCounts[t]); }
			}
			ImGui::Text("この路線のブロック: %d個", railBlockCount);
			ImGui::SameLine();
			if ( ImGui::SmallButton("この路線のブロックを削除") ) {
				std::erase_if(data_->blocks,
					[this](const BlockData& block){ return block.rail == currentEditRailIndex_; });
				++blockVersion_;
			}
			ImGui::Text("コイン: マップ全体 %d枚", ( int ) data_->coins.size());
		}
	}
	ImGui::End();
#endif
}


// =====================================================================
//  カメラ演出専用ウィンドウ
//   レール上に「カメラゾーン」を置き、通過時のカメラ挙動（向き切替/固定カメラ）を編集する。
//   レールエディタとは別ウィンドウにして、ゾーン一覧・編集・プレビューをここへ集約。
// =====================================================================
void RailEditor::DrawCameraWindow(){
#ifdef USE_IMGUI
	ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_FirstUseEver);
	ImGui::Begin("カメラエディタ");

	// アンカー（レール上のノード位置）を取り出す補助
	auto anchorPos = [&](const LevelCameraZone& z, Vector3& out) -> bool {
		if ( z.railIndex < 0 || z.railIndex >= ( int ) data_->railLines.size() ) return false;
		const auto& line = data_->railLines[z.railIndex];
		if ( line.empty() ) return false;
		int n = std::clamp(z.nodeIndex, 0, ( int ) line.size() - 1);
		out = line[n];
		return true;
	};

	// --- 追加 ---
	bool nodeSelected = ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) data_->railLines.size()
		&& selectedRailNode_ >= 0 && selectedRailNode_ < ( int ) data_->railLines[currentEditRailIndex_].size() );
	ImGui::BeginDisabled(!nodeSelected);
	if ( ImGui::Button("選択ノードにカメラゾーンを追加") ) {
		LevelCameraZone z;
		z.railIndex = currentEditRailIndex_;
		z.nodeIndex = selectedRailNode_;
		data_->cameraZones.push_back(z);
		selectedCamZone_ = ( int ) data_->cameraZones.size() - 1;
		++railVersion_;
	}
	ImGui::EndDisabled();
	if ( !nodeSelected ) { ImGui::TextDisabled("(Game View でレールのノードをクリック選択してから押す)"); }
	ImGui::Separator();

	// --- ゾーン一覧（クリックで選択＋対象ノードも選択して Game View で分かるように）---
	ImGui::Text("ゾーン一覧 (%d 個):", ( int ) data_->cameraZones.size());
	for ( int i = 0; i < ( int ) data_->cameraZones.size(); ++i ) {
		const auto& z = data_->cameraZones[i];
		char zlabel[128];
		if ( z.mode == 1 ) {
			snprintf(zlabel, sizeof(zlabel), "%d : [向き切替 %.0f°%s%s] レール%d ノード%d",
				i, z.yawDeg, ( z.revert != 0 ? "・戻る" : "・維持" ),
				( z.freeze != 0 ? "・停止" : "" ), z.railIndex, z.nodeIndex);
		} else {
			snprintf(zlabel, sizeof(zlabel), "%d : [固定カメラ] レール%d ノード%d (半径%.1fm)",
				i, z.railIndex, z.nodeIndex, z.radius);
		}
		if ( ImGui::Selectable(zlabel, selectedCamZone_ == i) ) {
			selectedCamZone_ = i;
			// 対象ノードも選択して Game View 上でどこか分かるようにする
			if ( z.railIndex >= 0 && z.railIndex < ( int ) data_->railLines.size() ) {
				SelectSingleNode(z.railIndex, std::clamp(z.nodeIndex, 0,
					( int ) data_->railLines[z.railIndex].size() - 1));
			}
		}
	}
	if ( data_->cameraZones.empty() ) { ImGui::TextDisabled("(カメラゾーンなし)"); }
	ImGui::Separator();

	// --- 選択中ゾーンの編集 ---
	if ( selectedCamZone_ >= 0 && selectedCamZone_ < ( int ) data_->cameraZones.size() ) {
		LevelCameraZone& z = data_->cameraZones[selectedCamZone_];
		bool zchanged = false;

		// 対象（レール/ノード）をここから直接変更できる（Game Viewで選び直さなくてよい）
		int railMax = ( int ) data_->railLines.size() - 1;
		ImGui::SetNextItemWidth(110.0f);
		if ( ImGui::InputInt("対象レール", &z.railIndex) ) {
			z.railIndex = std::clamp(z.railIndex, 0, ( std::max )( railMax, 0 ));
			zchanged = true;
		}
		ImGui::SameLine();
		int nodeMax = ( z.railIndex >= 0 && z.railIndex <= railMax )
			? ( int ) data_->railLines[z.railIndex].size() - 1 : 0;
		ImGui::SetNextItemWidth(110.0f);
		if ( ImGui::InputInt("ノード", &z.nodeIndex) ) {
			z.nodeIndex = std::clamp(z.nodeIndex, 0, ( std::max )( nodeMax, 0 ));
			zchanged = true;
		}

		const char* modeLabels[] = { "固定カメラ（範囲内だけ）", "向き切替（通過で回す）" };
		ImGui::SetNextItemWidth(240.0f);
		zchanged |= ImGui::Combo("モード", &z.mode, modeLabels, 2);

		if ( z.mode == 1 ) {
			// よく使う向きはワンクリックで
			ImGui::TextDisabled("向き:");
			ImGui::SameLine(); if ( ImGui::SmallButton("後ろ(0°)") )   { z.yawDeg = 0.0f;    zchanged = true; }
			ImGui::SameLine(); if ( ImGui::SmallButton("右(90°)") )    { z.yawDeg = 90.0f;   zchanged = true; }
			ImGui::SameLine(); if ( ImGui::SmallButton("正面(180°)") ) { z.yawDeg = 180.0f;  zchanged = true; }
			ImGui::SameLine(); if ( ImGui::SmallButton("左(-90°)") )   { z.yawDeg = -90.0f;  zchanged = true; }
			ImGui::SetNextItemWidth(200.0f);
			zchanged |= ImGui::SliderFloat("カメラの向き (度)", &z.yawDeg, -180.0f, 180.0f, "%.0f");
			ImGui::SetNextItemWidth(140.0f);
			zchanged |= ImGui::DragFloat("距離 (m)", &z.dist, 0.1f, 2.0f, 40.0f);
			ImGui::SetNextItemWidth(140.0f);
			zchanged |= ImGui::DragFloat("高さ (m)", &z.height, 0.1f, 0.0f, 30.0f);

			// 半径から出たら通常の向きへ戻すか（OFF=次のトリガーまで維持）
			bool rev = ( z.revert != 0 );
			if ( ImGui::Checkbox("離れたら元の向きに戻す", &rev) ) { z.revert = rev ? 1 : 0; zchanged = true; }
			ImGui::SameLine();
			ImGui::TextDisabled(rev ? "(区間演出向け)" : "(次のトリガーまで維持)");

			// 回転が終わるまで時間を止めるか（角度を少し変えるだけなら OFF で動きながら回す）
			bool frz = ( z.freeze != 0 );
			if ( ImGui::Checkbox("回転中は時間を止める", &frz) ) { z.freeze = frz ? 1 : 0; zchanged = true; }
			ImGui::SameLine();
			ImGui::TextDisabled(frz ? "(回り終わったら再開)" : "(動きながら回す)");
		} else {
			zchanged |= ImGui::DragFloat3("カメラ位置オフセット", &z.offset.x, 0.1f);
		}
		ImGui::SetNextItemWidth(140.0f);
		zchanged |= ImGui::SliderFloat("視野角 (度)", &z.fovDeg, 20.0f, 100.0f, "%.0f");
		ImGui::SetNextItemWidth(140.0f);
		zchanged |= ImGui::DragFloat("発動半径 (m)", &z.radius, 0.1f, 0.5f, 30.0f);
		if ( zchanged ) { ++railVersion_; }

		// --- プレビュー ---
		//   ボタン=ワンショット / ライブ=ONの間、値をいじるたびにメインカメラへ即反映
		auto pushPreview = [&](){
			Vector3 a;
			if ( !anchorPos(z, a) ) return;
			Vector3 camPos;
			if ( z.mode == 1 ) {
				float yawRad = z.yawDeg * 3.14159265f / 180.0f;
				camPos = { a.x + std::sin(yawRad) * z.dist, a.y + z.height, a.z - std::cos(yawRad) * z.dist };
			} else {
				camPos = { a.x + z.offset.x, a.y + z.offset.y, a.z + z.offset.z };
			}
			// アンカー（プレイヤーの立ち位置想定）を注視する回転
			Vector3 look = { a.x - camPos.x, ( a.y + 1.0f ) - camPos.y, a.z - camPos.z };
			float horiz = std::sqrt(look.x * look.x + look.z * look.z);
			camPreviewPos_ = camPos;
			camPreviewRot_ = { std::atan2(-look.y, horiz), std::atan2(look.x, look.z), 0.0f };
			camPreviewPending_ = true;
		};

		if ( ImGui::Button("この画角をプレビュー") ) { pushPreview(); }
		ImGui::SameLine();
		if ( ImGui::Checkbox("ライブプレビュー", &camPreviewLive_) ) { /* トグルだけ */ }
		if ( camPreviewLive_ ) {
			pushPreview(); // ONの間は毎フレーム反映＝スライダーを動かすと画角がその場で変わる
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "● ライブ中：値をいじるとカメラが即追従");
		} else {
			ImGui::TextDisabled("(ライブONで、いじった値がすぐ画面に反映される)");
		}

		if ( ImGui::Button("このゾーンを複製") ) {
			LevelCameraZone dup = z;
			data_->cameraZones.push_back(dup);
			selectedCamZone_ = ( int ) data_->cameraZones.size() - 1;
			++railVersion_;
		}
		ImGui::SameLine();
		if ( ImGui::Button("このゾーンを削除") ) {
			data_->cameraZones.erase(data_->cameraZones.begin() + selectedCamZone_);
			selectedCamZone_ = -1;
			++railVersion_;
		}
	} else {
		camPreviewLive_ = false; // 選択が無ければライブ解除
		ImGui::TextDisabled("(一覧からゾーンを選択すると編集できます)");
	}

	ImGui::Separator();
	ImGui::TextDisabled("Game View: オレンジ球=向き切替 / 水色球=固定カメラ / 白い箱=カメラ位置の目安");
	ImGui::End();
#endif
}

// 「この画角をプレビュー」の要求をシーンが受け取る（1回で消費）
bool RailEditor::ConsumeCameraPreviewRequest(Vector3& outPos, Vector3& outRot){
	if ( !camPreviewPending_ ) return false;
	outPos = camPreviewPos_;
	outRot = camPreviewRot_;
	camPreviewPending_ = false;
	return true;
}
