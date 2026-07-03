#include "RailEditor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <algorithm>

#include "engine/utils/ImGuiManager.h"
#include "engine/base/Input.h"

static bool RailLinesEqual(const std::vector<std::vector<Vector3>>& a,
                           const std::vector<std::vector<Vector3>>& b){
	if ( a.size() != b.size() ) return false;
	for ( size_t i = 0; i < a.size(); ++i ) {
		if ( a[i].size() != b[i].size() ) return false;
		for ( size_t j = 0; j < a[i].size(); ++j ) {
			if ( a[i][j].x != b[i][j].x || a[i][j].y != b[i][j].y || a[i][j].z != b[i][j].z ) return false;
		}
	}
	return true;
}

// 動くレール設定(Vector4)の一致判定（Vector4 に operator== が無いので手動）
static bool MotionsEqual(const std::vector<Vector4>& a, const std::vector<Vector4>& b){
	if ( a.size() != b.size() ) return false;
	for ( size_t i = 0; i < a.size(); ++i ) {
		if ( a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].w != b[i].w ) return false;
	}
	return true;
}

// 折れ線を Catmull-Rom 補間でなめらかにする（手描きキャンバス・なめらか化ボタンで共用）。
//   seg: 区間あたりの分割数（多いほど滑らか）。点が3個未満ならそのまま返す。
static std::vector<Vector3> SmoothPolylineCR(const std::vector<Vector3>& pts, int seg){
	const int n = static_cast< int >( pts.size() );
	if ( n < 3 || seg < 2 ) return pts;
	std::vector<Vector3> out;
	out.reserve(static_cast< size_t >( n ) * seg + 1);
	auto at = [&]( int i ) -> const Vector3&{ return pts[std::clamp(i, 0, n - 1)]; };
	for ( int i = 0; i + 1 < n; ++i ) {
		const Vector3& p0 = at(i - 1);
		const Vector3& p1 = at(i);
		const Vector3& p2 = at(i + 1);
		const Vector3& p3 = at(i + 2);
		for ( int s = 0; s < seg; ++s ) {
			float t  = static_cast< float >( s ) / static_cast< float >( seg );
			float t2 = t * t, t3 = t2 * t;
			Vector3 q;
			q.x = 0.5f * ( ( 2.0f * p1.x ) + ( -p0.x + p2.x ) * t + ( 2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x ) * t2 + ( -p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x ) * t3 );
			q.y = 0.5f * ( ( 2.0f * p1.y ) + ( -p0.y + p2.y ) * t + ( 2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y ) * t2 + ( -p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y ) * t3 );
			q.z = 0.5f * ( ( 2.0f * p1.z ) + ( -p0.z + p2.z ) * t + ( 2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z ) * t2 + ( -p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z ) * t3 );
			out.push_back(q);
		}
	}
	out.push_back(pts.back());
	return out;
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
        data_->railGroundTypes.resize(data_->railLines.size(), 0);
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

// スタンプ（配置待ちシェイプ）を at を原点として新しい路線として設置する
void RailEditor::PlaceStamp(const Vector3& at){
	if ( pendingStamp_.empty() ) return;

	std::vector<Vector3> line = pendingStamp_;
	for ( auto& n : line ) { n.x += at.x; n.y += at.y; n.z += at.z; }

	data_->railLines.push_back(std::move(line));
	data_->railTypes.push_back(-1);
	data_->railMotions.push_back(Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	data_->railGroundTypes.push_back(0);
	data_->railVisible.push_back(1);
	data_->railNodeHoles.push_back(std::vector<int>(data_->railLines.back().size(), 0));
	data_->railMotionTypes.push_back(0);
	data_->railMotionPhases.push_back(0.0f);
	data_->railOneWay.push_back(0);
	data_->railSpeedMuls.push_back(1.0f);
	SelectWholeRail(( int ) data_->railLines.size() - 1); // 置いた直後にギズモで微調整できる
	RebuildRailPoints();

	pendingStamp_.clear();
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
void RailEditor::DrawWindow(){
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
		data_->railGroundTypes.resize(data_->railLines.size(), 0);
	}
	if ( data_->railVisible.size() != data_->railLines.size() ) {
		data_->railVisible.resize(data_->railLines.size(), 1);
	}
	if ( data_->railTypes.size() != data_->railLines.size() ) {
		data_->railTypes.resize(data_->railLines.size(), -1);
	}
	if ( data_->railNodeHoles.size() != data_->railLines.size() ) {
		data_->railNodeHoles.resize(data_->railLines.size());
	}

	// --- 路線リスト（クリック＝路線まるごと選択 / 複製 / 削除）---
	ImGui::Text("路線リスト:");
	{
		int duplicateRail = -1;
		int deleteRail = -1;

		for ( int i = 0; i < ( int ) data_->railLines.size(); ++i ) {
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
			ImGui::SameLine();
			if ( ImGui::SmallButton("複製") ) { duplicateRail = i; }
			ImGui::SameLine();
			if ( ImGui::SmallButton("削除") ) { deleteRail = i; }
			ImGui::PopID();
		}

		// 複製：少し奥にずらしたコピーを作り、すぐ動かせるよう選択しておく（Ctrl+Dと共通処理）
		if ( duplicateRail >= 0 ) {
			DuplicateRail(duplicateRail);
		}
		// 削除（最後の1本は消さない）
		if ( deleteRail >= 0 && data_->railLines.size() > 1 ) {
			data_->railLines.erase(data_->railLines.begin() + deleteRail);
			data_->railTypes.erase(data_->railTypes.begin() + deleteRail);
			data_->railMotions.erase(data_->railMotions.begin() + deleteRail);
			if ( deleteRail < ( int ) data_->railGroundTypes.size() )
				data_->railGroundTypes.erase(data_->railGroundTypes.begin() + deleteRail);
			if ( deleteRail < ( int ) data_->railVisible.size() )
				data_->railVisible.erase(data_->railVisible.begin() + deleteRail);
			if ( deleteRail < ( int ) data_->railNodeHoles.size() )
				data_->railNodeHoles.erase(data_->railNodeHoles.begin() + deleteRail);
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

		// --- このレールの「動き」（ムービングプラットフォーム）---
		Vector4& motion = data_->railMotions[currentEditRailIndex_];
		bool motionChanged = false;
		ImGui::Text("動くレール (全て0で停止):");
		motionChanged |= ImGui::DragFloat3("振幅 XYZ (m)", &motion.x, 0.05f);
		ImGui::SetNextItemWidth(110.0f);
		motionChanged |= ImGui::DragFloat("周期 (秒)", &motion.w, 0.05f, 0.1f, 60.0f);
		if ( motion.w < 0.1f ) motion.w = 0.1f;

		// 波形（sin往復 / 端で一時停止つき往復 / 円運動）と位相（複数レールの動きをずらす）
		data_->railMotionTypes.resize(data_->railLines.size(), 0);
		data_->railMotionPhases.resize(data_->railLines.size(), 0.0f);
		int& mtype = data_->railMotionTypes[currentEditRailIndex_];
		const char* waveLabels[] = { "サイン往復", "停止つき往復 (端で一瞬止まる)", "円運動 (X,Z振幅で円)" };
		ImGui::SetNextItemWidth(220.0f);
		if ( ImGui::Combo("波形", &mtype, waveLabels, 3) ) { motionChanged = true; }
		float& mphase = data_->railMotionPhases[currentEditRailIndex_];
		ImGui::SetNextItemWidth(140.0f);
		if ( ImGui::SliderFloat("位相 (0〜1)", &mphase, 0.0f, 1.0f, "%.2f") ) { motionChanged = true; }

		if ( motionChanged ) { ++railVersion_; } // ゲーム側へ即反映
		ImGui::TextDisabled("例: 振幅(0,0,3) 周期2 → 奥行き±3mを2秒で往復 / 位相0.5=半周期ずれ");
		ImGui::Separator();

		// --- 地面タイプ（落下は「穴」で指定する。NoGround は廃止）---
		data_->railGroundTypes.resize(data_->railLines.size(), 0);
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
		ImGui::TextDisabled("【キーボード】矢印=1マス移動 / Q,E=下,上 / Delete=削除 / Ctrl+D=路線複製");
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

	// --- 引き終わったレールへ後からノードを追加（端の延長線上に1個伸ばす）---
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

		const char* shapeNames[] = { "直線", "L字", "円 (ループ)", "階段", "S字カーブ" };
		ImGui::Text("形を選んでパラメータを決めて「生成」:");
		ImGui::SetNextItemWidth(150.0f);
		ImGui::Combo("形", &shapeType, shapeNames, IM_ARRAYSIZE(shapeNames));

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
				// すぐ路線にせず「配置待ち（スタンプ）」にする → Game Viewでクリックした場所に設置
				pendingStamp_ = std::move(line);
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
		data_->railGroundTypes.push_back(0);
		data_->railVisible.push_back(1);
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
