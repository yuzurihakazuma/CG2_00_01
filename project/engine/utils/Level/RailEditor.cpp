#include "RailEditor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

#include "engine/utils/ImGuiManager.h"
#include "engine/base/Input.h"

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
    hasInitial_     = true;

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
	data_->railGroups.resize(n);
	data_->railNodeHoles.resize(n);
	data_->railMotionTypes.resize(n, 0);
	data_->railMotionPhases.resize(n, 0.0f);
	data_->railOneWay.resize(n, 0);
	data_->railSpeedMuls.resize(n, 1.0f);
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
	data_->railGroups.push_back(group);
	data_->railNodeHoles.push_back(std::vector<int>(nodeCount, 0));
	data_->railMotionTypes.push_back(0);
	data_->railMotionPhases.push_back(0.0f);
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
	data_->railGroups.erase(data_->railGroups.begin() + idx);
	data_->railNodeHoles.erase(data_->railNodeHoles.begin() + idx);
	data_->railMotionTypes.erase(data_->railMotionTypes.begin() + idx);   // ※従来ここから4本が消えておらず、
	data_->railMotionPhases.erase(data_->railMotionPhases.begin() + idx); //   削除のたびに後続レールの動き設定が
	data_->railOneWay.erase(data_->railOneWay.begin() + idx);             //   1つずつズレるバグがあった
	data_->railSpeedMuls.erase(data_->railSpeedMuls.begin() + idx);
	// ガイド参照の付け替え：消した番号より後ろは1つ前へ、消した本人を指していたら解除
	for ( auto& guideRef : data_->railGuideRails ) {
		if ( guideRef == idx ) { guideRef = -1; }
		else if ( guideRef > idx ) { --guideRef; }
	}
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
	data_->railGroups.push_back(( railIdx < ( int ) data_->railGroups.size() ) ? data_->railGroups[railIdx] : "");
	if ( railIdx < ( int ) data_->railNodeHoles.size() ) data_->railNodeHoles.push_back(data_->railNodeHoles[railIdx]);
	else                                                 data_->railNodeHoles.push_back(std::vector<int>(data_->railLines.back().size(), 0));
	data_->railMotionTypes.push_back(( railIdx < ( int ) data_->railMotionTypes.size() ) ? data_->railMotionTypes[railIdx] : 0);
	data_->railMotionPhases.push_back(( railIdx < ( int ) data_->railMotionPhases.size() ) ? data_->railMotionPhases[railIdx] : 0.0f);
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
	multiSelection_.clear(); // ノード構成が変わるので選択を解除
	if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
	}
	if ( currentEditRailIndex_ < 0 ) currentEditRailIndex_ = 0;
	selectedRailNode_ = -1;
	committed_.lines   = data_->railLines; // 復元直後を基準に
	committed_.types   = data_->railTypes;
	committed_.motions = data_->railMotions;
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
		committedInit_ = true;
		return;
	}

	// 変化していなければ何もしない（配置・タイプ・動くレール設定のいずれかが変わったら記録）
	if ( RailLinesEqual(committed_.lines, data_->railLines)
		&& committed_.types == data_->railTypes
		&& MotionsEqual(committed_.motions, data_->railMotions) ) return;

	// 直前の安定状態を undo へ積み、現在を新しいチェックポイントに
	undoStack_.push_back(committed_);
	if ( undoStack_.size() > 100 ) undoStack_.erase(undoStack_.begin());
	redoStack_.clear();
	committed_.lines   = data_->railLines;
	committed_.types   = data_->railTypes;
	committed_.motions = data_->railMotions;
}

void RailEditor::Undo(){
	if ( undoStack_.empty() ) return;
	// 現在をredoへ
	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions;
	redoStack_.push_back(cur);
	RailSnapshot prev = undoStack_.back();
	undoStack_.pop_back();
	RestoreSnapshot(prev);
}

void RailEditor::Redo(){
	if ( redoStack_.empty() ) return;
	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions;
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
		&& MotionsEqual(initialMotions_, data_->railMotions) );
}

// 編集開始時（マップ読込直後）の状態へ一発で戻す。
//   現在の状態を undo に積んでから戻すので、Ctrl+Z で元の作業に復帰できる。
void RailEditor::ResetToInitial(){
	if ( !CanResetToInitial() ) return;

	RailSnapshot cur; cur.lines = data_->railLines; cur.types = data_->railTypes; cur.motions = data_->railMotions;
	undoStack_.push_back(cur);
	if ( undoStack_.size() > 100 ) undoStack_.erase(undoStack_.begin());
	redoStack_.clear();

	data_->railLines   = initialLines_;
	data_->railTypes   = initialTypes_;
	data_->railMotions = initialMotions_; // 動くレール設定も初期へ戻す
	multiSelection_.clear();
	selectedRailNode_ = -1;
	if ( currentEditRailIndex_ >= ( int ) data_->railLines.size() ) {
		currentEditRailIndex_ = ( int ) data_->railLines.size() - 1;
	}
	if ( currentEditRailIndex_ < 0 ) currentEditRailIndex_ = 0;

	committed_.lines = data_->railLines; // 戻した直後を基準に
	committed_.types = data_->railTypes;
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

void RailEditor::DrawWindow(){
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

	// --- 表示トグル（目立つ場所に。ジャンプ予測線＝既定OFF / 動きプレビュー＝既定OFF）---
	ImGui::Checkbox("ジャンプ予測線を表示", &railReachLines_);
	if ( ImGui::IsItemHovered() ) {
		ImGui::SetTooltip("プレイヤーのジャンプ物理の弾道線（Gapの端から出る点線）。\n多くて見づらい時はOFF、飛び移り距離を確認したい時だけON");
	}
	ImGui::SameLine();
	ImGui::Checkbox("動きをプレビュー", &railMotionPreview_);
	if ( ImGui::IsItemHovered() ) {
		ImGui::SetTooltip("動くレール（親子付けリフト含む）をエディタ中も再生する。\nPlayを押さなくても、リフトの組み方が合っているかその場で確認できる");
	}
	// 到達チェックの結果を常時表示（詳細と再チェックは接続タブ。編集すると自動更新）
	if ( routeChecked_ && data_->goalRailIndex >= 0 ) {
		ImGui::SameLine();
		if ( routeReachable_ ) { ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "ゴール:到達可"); }
		else                   { ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "ゴール:不可"); }
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

		// --- このレールの「動き」（ムービングプラットフォーム）。畳んで整理（親子付けは下の欄）---
		if ( ImGui::CollapsingHeader("動くレール（揺れ/波形/ガイド追従）") ) {
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
		                             "ガイドレール追従 (経路を一周し続ける)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("波形", &mtype, waveLabels, 4) ) { motionChanged = true; }
		if ( mtype == 3 ) {
			// ガイド追従：このレール全体が、指定したガイドレールの経路に沿って一周し続ける。
			//   らせんガイド＋小さい足場数枚（位相をずらす）＝自動で上へ運ばれる乗り継ぎ足場
			data_->railGuideRails.resize(data_->railLines.size(), -1);
			int& guideIdx = data_->railGuideRails[currentEditRailIndex_];
			ImGui::SetNextItemWidth(110.0f);
			if ( ImGui::InputInt("ガイドレール番号", &guideIdx) ) {
				if ( guideIdx < -1 ) guideIdx = -1;
				if ( guideIdx >= ( int ) data_->railLines.size() ) guideIdx = ( int ) data_->railLines.size() - 1;
				if ( guideIdx == currentEditRailIndex_ ) guideIdx = -1; // 自分自身は不可
				motionChanged = true;
			}
			ImGui::TextDisabled("周期=1周にかかる秒数 / 位相=スタート位置(0〜1) / 振幅は使わない\n"
				"足場を複数並べて位相を 0, 0.25, 0.5... とずらすと数珠つなぎで運ばれる");
		}
		float& mphase = data_->railMotionPhases[currentEditRailIndex_];
		ImGui::SetNextItemWidth(140.0f);
		if ( ImGui::SliderFloat("位相 (0〜1)", &mphase, 0.0f, 1.0f, "%.2f") ) { motionChanged = true; }

		if ( motionChanged ) { ++railVersion_; } // ゲーム側へ即反映
		ImGui::TextDisabled("例: 振幅(0,0,3) 周期2 → 奥行き±3mを2秒で往復 / 位相0.5=半周期ずれ");
		}
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
					data_->railOneWay.resize(railTotal, 0);
					data_->railSpeedMuls.resize(railTotal, 1.0f);
					data_->railGuideRails.resize(railTotal, -1);
					for ( int g = 0; g < ( int ) data_->railLines.size(); ++g ) {
						if ( g == currentEditRailIndex_ || data_->railGroups[g] != currentGroup ) continue;
						data_->railTypes[g]       = data_->railTypes[currentEditRailIndex_];
						data_->railMotions[g]     = data_->railMotions[currentEditRailIndex_];
						data_->railGroundTypes[g] = data_->railGroundTypes[currentEditRailIndex_];
						data_->railVisible[g]     = data_->railVisible[currentEditRailIndex_];
						data_->railLineModes[g]   = data_->railLineModes[currentEditRailIndex_];
						data_->railRoadModes[g]   = data_->railRoadModes[currentEditRailIndex_];
						data_->railMotionTypes[g] = data_->railMotionTypes[currentEditRailIndex_];
						data_->railOneWay[g]      = data_->railOneWay[currentEditRailIndex_];
						data_->railSpeedMuls[g]   = data_->railSpeedMuls[currentEditRailIndex_];
						data_->railGuideRails[g]  = data_->railGuideRails[currentEditRailIndex_];
					}
					++railVersion_;
				}
				if ( ImGui::IsItemHovered() ) {
					ImGui::SetTooltip("波形・ガイド番号・周期・道/表示設定などを一括反映。\n位相だけはコピーしない（足場の数珠つなぎは位相をずらして使うため）");
				}
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
		ImGui::TextDisabled("【リフトの作り方】作成タブ「動くリフト一式」→クリック設置→動きをプレビューON");
		ImGui::TextDisabled("  既存の足場を動かす→グループの「自動でリフト化」/ 親レール番号を入れて追従");
		ImGui::TextDisabled("【仕上げ】接続タブ「到達チェック」→OK(緑)ならクリア可能 / ゴースト走行で流れ確認");
		ImGui::TextDisabled("  経路色: オレンジ=走る / シアン=ジャンプ / マゼンタ=ふんばり必要");
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

	// --- 接続ツール一式：溶接/連結/自動スナップ/足元ガイド（畳んで整理）---
	if ( ImGui::CollapsingHeader("接続ツール（溶接・連結・自動スナップ）") ) {
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

	// ※「到達判定の表示」チェックは管理タブ上部の「ジャンプ予測線を表示」と重複していたため統合済み

	// --- スタート→ゴール到達チェック＆ゴースト走行（Playせずにステージ成立とプレイヤーの走りを確認）---
	{
		if ( ImGui::Button("到達チェック＆経路表示") ) { BuildRouteCheck(); }
		ImGui::SameLine();
		if ( ImGui::Button("経路をクリア") ) {
			routePoints_.clear(); routeCum_.clear();
			routeChecked_ = false; ghostRun_ = false;
		}
		if ( routeChecked_ ) {
			if ( data_->goalRailIndex < 0 ) {
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "ゴールが未設定です（管理タブの「選択ノードをゴールに」で設定）");
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
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "NG: ゴールへ到達できません（下の未接続一覧や接続を確認）");
			}
			ImGui::Checkbox("編集したら自動で再チェック", &routeAutoRecheck_);
		}
	}
	ImGui::Separator();

	// --- 接続の一覧（マップ全体）：どこで何が繋がっているかを一覧で確認できる。
	//     行にホバーすると Game View で両方のレールと接続点が黄色く光る ---
	if ( ImGui::CollapsingHeader("接続の一覧（マップ全体）") ) {
		const char* connTypeNames[] = { "溶接", "T字", "交差" };
		int shownConns = 0;
		for ( int a = 0; a < ( int ) data_->railLines.size(); ++a ) {
			for ( const auto& conn : GetRailConnections(a) ) {
				if ( conn.otherRail < a ) continue; // ペアの重複を除く
				char rowLabel[128];
				snprintf(rowLabel, sizeof(rowLabel), "%s: 路線%d <-> 路線%d @ (%.1f, %.1f, %.1f)###conn_%d_%d_%d",
					connTypeNames[std::clamp(conn.type, 0, 2)], a, conn.otherRail,
					conn.pos.x, conn.pos.y, conn.pos.z, a, conn.otherRail, conn.type);
				if ( ImGui::Selectable(rowLabel) ) { SelectWholeRail(a); } // クリックで片方を選択
				if ( ImGui::IsItemHovered() ) {
					hoveredListRail_  = a;
					hoveredListRailB_ = conn.otherRail;
					hoveredConnPos_   = conn.pos;
					hoveredConnValid_ = true;
				}
				++shownConns;
			}
		}
		if ( shownConns == 0 ) { ImGui::TextDisabled("接続はまだありません"); }
	}

	// --- 未接続の端の一覧：繋がっていない端をマップ全体から洗い出し、ワンクリックで修復 ---
	if ( ImGui::CollapsingHeader("未接続の端の一覧") ) {
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

	// この路線の穴配列をノード数に合わせて整える（外はDrawWindow冒頭で整える）
	if ( currentEditRailIndex_ < ( int ) data_->railNodeHoles.size() ) {
		data_->railNodeHoles[currentEditRailIndex_].resize(
			data_->railLines[currentEditRailIndex_].size(), 0);
	}

	if ( ImGui::CollapsingHeader("穴（落下区間の指定）") ) {
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

	// --- ノード一覧・編集（追加/挿入/削除。座標はクリックでキーボード入力可）---
	if ( ImGui::CollapsingHeader("ノード一覧・編集") ) {
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

	ImGui::EndTabItem();
	} // 作成タブ

	// =====================================================================
	//  接続タブ（§5 管理UI）：溶接/T字の接続一覧・選択ハイライト・切断
	//   接続はレール座標から毎回導出する（保存しない派生データ）
	// =====================================================================
	if ( ImGui::BeginTabItem("接続 (Joints)") ) {
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

		ImGui::EndTabItem();
	}

	ImGui::EndTabBar();
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
