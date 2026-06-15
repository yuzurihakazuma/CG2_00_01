#include "LevelEditor.h"

/// --- 標準ライブラリ ---
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <algorithm>

// --- エンジン側のファイル ---
#include "LevelManager.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/base/Input.h"


LevelEditor::LevelEditor() = default;
LevelEditor::~LevelEditor() = default;

// railLines（ノード列）の一致判定（Vector3 に operator== が無いので手動）
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


void LevelEditor::Initialize(){
	// アセットブラウザ用に resources/ を走査
	ScanAssets();

	// 最初は空の状態でスタートするか、デフォルトのマップを読み込む
	LoadAndCreateMap("resources/map/map01.json");
}

// resources/ を再帰走査して .obj / .gltf をアセット一覧に登録する
void LevelEditor::ScanAssets(){
	assetList_.clear();

	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path root("resources");
	if ( !fs::exists(root, ec) ) return;

	for ( const auto& entry : fs::recursive_directory_iterator(root, ec) ) {
		if ( !entry.is_regular_file() ) continue;
		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c){ return ( char ) std::tolower(c); });
		if ( ext != ".obj" && ext != ".gltf" ) continue;

		AssetEntry asset;
		asset.name = entry.path().stem().string();
		asset.dir = entry.path().parent_path().generic_string();
		asset.file = entry.path().filename().string();
		asset.display = fs::relative(entry.path(), root, ec).generic_string();
		assetList_.push_back(asset);
	}
	std::sort(assetList_.begin(), assetList_.end(),
		[](const AssetEntry& a, const AssetEntry& b){ return a.display < b.display; });
}

// モデルが未ロードならアセット一覧から探してロードする
bool LevelEditor::EnsureAssetLoaded(const std::string& name){
	if ( ModelManager::GetInstance()->FindModel(name) ) return true;
	for ( const auto& asset : assetList_ ) {
		if ( asset.name == name ) {
			ModelManager::GetInstance()->LoadModel(asset.name, asset.dir, asset.file);
			return ModelManager::GetInstance()->FindModel(name) != nullptr;
		}
	}
	return false;
}
// マップの読み込みと生成
void LevelEditor::LoadAndCreateMap(const std::string& fileName){
	object3ds_.clear();
	levelData_ = LevelManager::GetInstance()->Load(fileName);

	for ( auto& objData : levelData_.objects ) {
		Model* model = ModelManager::GetInstance()->FindModel(objData.type);
		std::unique_ptr<Obj3d> newObj = std::make_unique<Obj3d>();
		newObj->Initialize(model);
		newObj->SetCamera(camera_);
		newObj->SetTranslation(objData.translation);
		newObj->SetRotation(objData.rotation);
		newObj->SetScale(objData.scale);
		newObj->Update();
		object3ds_.push_back(std::move(newObj));
	}
	selectedObjectIndex_ = -1;

	// レールが1つもない場合は空のレールを作っておく
	if ( levelData_.railLines.empty() ) {
		levelData_.railLines.push_back(std::vector<Vector3>());
	}
	currentEditRailIndex_ = 0;
	selectedRailNode_ = -1;

	// ★すべての路線のプレビューを再構築
	RebuildRailPoints();
}

// ★追加：すべての路線の球体を描画用にセットアップする
void LevelEditor::RebuildRailPoints(){
	// レールの可視化(緑線)はゲーム側(GamePlayScene)が担当するので、
	// ここでは赤い球の生成はやめ、編集の世代番号だけを進めて変化を通知する。
	railSpheresAll_.clear();
	pathPointsAll_.clear();
	++railVersion_;
}

// --- マウス編集サポート ---
int LevelEditor::GetCurrentRailNodeCount() const{
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return 0;
	return ( int ) levelData_.railLines[currentEditRailIndex_].size();
}
bool LevelEditor::GetRailNodePos(int idx, Vector3& out) const{
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return false;
	const auto& line = levelData_.railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return false;
	out = line[idx];
	return true;
}
void LevelEditor::SetRailNodePos(int idx, const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return;
	line[idx] = ApplyNodeSnap(p); // 他レールの端点が近ければ吸着
	++railVersion_; // 緑線・ゲーム側へライブ反映
}
float LevelEditor::SnapValue(float v) const{
	if ( !railSnap_ || railGridSize_ <= 0.0f ) return v;
	return std::round(v / railGridSize_) * railGridSize_;
}

// 直角ロック＋グリッド＋ノード吸着を適用した「実際に置かれる位置」を返す（プレビューと共用）
Vector3 LevelEditor::ComputePlacement(const Vector3& raw) const{
	Vector3 pos = raw;

	if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) levelData_.railLines.size() ) {
		const auto& line = levelData_.railLines[currentEditRailIndex_];
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

void LevelEditor::AppendRailNodeAt(const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];

	line.push_back(ComputePlacement(p));
	selectedRailNode_ = ( int ) line.size() - 1; // 追加した点を選択状態に
	multiSelection_.clear();
	multiSelection_.push_back({ currentEditRailIndex_, selectedRailNode_ });
	++railVersion_;
}

// 方向ボタン用：前ノード（無ければ配置高さの原点）から相対移動して追加
void LevelEditor::AppendRailNodeRelative(float dx, float dy, float dz){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];

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
Vector3 LevelEditor::ApplyNodeSnap(const Vector3& p) const{
	if ( !railNodeSnap_ ) return p;
	float bestSq = railNodeSnapRadius_ * railNodeSnapRadius_;
	Vector3 best = p;
	bool found = false;
	for ( int r = 0; r < ( int ) levelData_.railLines.size(); ++r ) {
		if ( r == currentEditRailIndex_ ) continue; // 自分のレール内には吸着しない（潰れ防止）
		for ( const auto& n : levelData_.railLines[r] ) {
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
void LevelEditor::AddToSelection(int rail, int node){
	if ( rail < 0 || rail >= ( int ) levelData_.railLines.size() ) return;
	if ( node < 0 || node >= ( int ) levelData_.railLines[rail].size() ) return;
	for ( const auto& r : multiSelection_ ) {
		if ( r.rail == rail && r.node == node ) return; // 重複登録しない
	}
	multiSelection_.push_back({ rail, node });
}

void LevelEditor::SelectSingleNode(int rail, int node){
	SetCurrentRail(rail);
	selectedRailNode_ = node;
	multiSelection_.clear();
	AddToSelection(rail, node);
}

void LevelEditor::SelectWholeRail(int railIdx){
	if ( railIdx < 0 || railIdx >= ( int ) levelData_.railLines.size() ) return;
	SetCurrentRail(railIdx);
	multiSelection_.clear();
	for ( int n = 0; n < ( int ) levelData_.railLines[railIdx].size(); ++n ) {
		multiSelection_.push_back({ railIdx, n });
	}
}

Vector3 LevelEditor::GetSelectionCenter() const{
	Vector3 c { 0.0f, 0.0f, 0.0f };
	int cnt = 0;
	for ( const auto& r : multiSelection_ ) {
		Vector3 p;
		if ( GetNodePosOf(r.rail, r.node, p) ) { c.x += p.x; c.y += p.y; c.z += p.z; ++cnt; }
	}
	if ( cnt > 0 ) { c.x /= cnt; c.y /= cnt; c.z /= cnt; }
	return c;
}

void LevelEditor::TranslateSelection(const Vector3& delta){
	bool moved = false;
	for ( const auto& r : multiSelection_ ) {
		if ( r.rail < 0 || r.rail >= ( int ) levelData_.railLines.size() ) continue;
		auto& line = levelData_.railLines[r.rail];
		if ( r.node < 0 || r.node >= ( int ) line.size() ) continue;
		line[r.node].x += delta.x;
		line[r.node].y += delta.y;
		line[r.node].z += delta.z;
		moved = true;
	}
	if ( moved ) { ++railVersion_; } // 緑線・ゲーム側へライブ反映
}

void LevelEditor::SetCurrentRail(int idx){
	if ( idx < 0 || idx >= ( int ) levelData_.railLines.size() ) return;
	if ( currentEditRailIndex_ != idx ) {
		currentEditRailIndex_ = idx;
		selectedRailNode_ = -1;
	}
}

int LevelEditor::GetNodeCountOf(int rail) const{
	if ( rail < 0 || rail >= ( int ) levelData_.railLines.size() ) return 0;
	return ( int ) levelData_.railLines[rail].size();
}

// スタンプ（配置待ちシェイプ）を at を原点として新しい路線として設置する
void LevelEditor::PlaceStamp(const Vector3& at){
	if ( pendingStamp_.empty() ) return;

	std::vector<Vector3> line = pendingStamp_;
	for ( auto& n : line ) { n.x += at.x; n.y += at.y; n.z += at.z; }

	levelData_.railLines.push_back(std::move(line));
	levelData_.railTypes.push_back(-1);
	levelData_.railMotions.push_back(Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	SelectWholeRail(( int ) levelData_.railLines.size() - 1); // 置いた直後にギズモで微調整できる
	RebuildRailPoints();

	pendingStamp_.clear();
}

// 複数選択中のノードを一括削除
void LevelEditor::DeleteSelectedNodes(){
	if ( multiSelection_.empty() ) return;

	// 消すたびに後ろのノード番号がズレないよう、rail降順→node降順で消す
	std::vector<NodeRef> refs = multiSelection_;
	std::sort(refs.begin(), refs.end(), [](const NodeRef& a, const NodeRef& b){
		if ( a.rail != b.rail ) return a.rail > b.rail;
		return a.node > b.node;
		});

	for ( const auto& r : refs ) {
		if ( r.rail < 0 || r.rail >= ( int ) levelData_.railLines.size() ) continue;
		auto& line = levelData_.railLines[r.rail];
		if ( r.node < 0 || r.node >= ( int ) line.size() ) continue;
		line.erase(line.begin() + r.node);
	}
	multiSelection_.clear();
	selectedRailNode_ = -1;
	++railVersion_;
}

// 路線を複製して選択（少し奥にずらしたコピー）
void LevelEditor::DuplicateRail(int railIdx){
	if ( railIdx < 0 || railIdx >= ( int ) levelData_.railLines.size() ) return;

	std::vector<Vector3> copy = levelData_.railLines[railIdx];
	for ( auto& n : copy ) { n.z += 2.0f; }
	levelData_.railLines.push_back(std::move(copy));
	levelData_.railTypes.push_back(levelData_.railTypes[railIdx]);
	levelData_.railMotions.push_back(levelData_.railMotions[railIdx]);
	SelectWholeRail(( int ) levelData_.railLines.size() - 1);
	RebuildRailPoints();
}

bool LevelEditor::GetNodePosOf(int rail, int node, Vector3& out) const{
	if ( rail < 0 || rail >= ( int ) levelData_.railLines.size() ) return false;
	const auto& line = levelData_.railLines[rail];
	if ( node < 0 || node >= ( int ) line.size() ) return false;
	out = line[node];
	return true;
}

// 表示用：横(0)/縦(1)。railTypes が -1(自動)なら front→back の主軸で判定（実装と同じ1.5バイアス）
int LevelEditor::GetRailDisplayType(int rail) const{
	if ( rail < 0 || rail >= ( int ) levelData_.railLines.size() ) return 0;
	if ( rail < ( int ) levelData_.railTypes.size() ) {
		int t = levelData_.railTypes[rail];
		if ( t == 0 ) return 0;
		if ( t == 1 ) return 1;
	}
	const auto& nodes = levelData_.railLines[rail];
	if ( nodes.size() < 2 ) return 0;
	float dx = std::abs(nodes.back().x - nodes.front().x);
	float dz = std::abs(nodes.back().z - nodes.front().z);
	return ( dz > dx * 1.5f ) ? 1 : 0;
}

// afterIndex の直後にノードを挿入
void LevelEditor::InsertRailNode(int afterIndex, const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];
	int insertAt = afterIndex + 1;
	if ( insertAt < 0 ) insertAt = 0;
	if ( insertAt > ( int ) line.size() ) insertAt = ( int ) line.size();
	line.insert(line.begin() + insertAt, p);
	selectedRailNode_ = insertAt;
	// ノード番号がずれるので選択を挿入ノードだけに引き直す
	multiSelection_.clear();
	multiSelection_.push_back({ currentEditRailIndex_, insertAt });
	++railVersion_;
}

// 指定ノードを削除
void LevelEditor::DeleteRailNode(int idx){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return;
	line.erase(line.begin() + idx);
	selectedRailNode_ = -1;
	multiSelection_.clear(); // ノード番号がずれるので選択を解除
	++railVersion_;
}

// ============================================================
// Undo / Redo
// ============================================================
void LevelEditor::RestoreSnapshot(const RailSnapshot& s){
	levelData_.railLines = s.lines;
	levelData_.railTypes = s.types;
	multiSelection_.clear(); // ノード構成が変わるので選択を解除
	if ( currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) {
		currentEditRailIndex_ = ( int ) levelData_.railLines.size() - 1;
	}
	if ( currentEditRailIndex_ < 0 ) currentEditRailIndex_ = 0;
	selectedRailNode_ = -1;
	committed_.lines = levelData_.railLines; // 復元直後を基準に
	committed_.types = levelData_.railTypes;
	++railVersion_;
}

// マウス非操作の瞬間に、前回チェックポイントとの差分があれば履歴へ積む
void LevelEditor::CommitIfStable(){
	Input* input = Input::GetInstance();
	// ドラッグ中(左/右ボタン押下中)はまだ確定させない（一連の操作を1ステップにまとめる）
	if ( input->PushMouseButton(0) || input->PushMouseButton(1) ) return;

	if ( !committedInit_ ) {
		committed_.lines = levelData_.railLines;
		committed_.types = levelData_.railTypes;
		committedInit_ = true;
		return;
	}

	// 変化していなければ何もしない
	if ( RailLinesEqual(committed_.lines, levelData_.railLines) && committed_.types == levelData_.railTypes ) return;

	// 直前の安定状態を undo へ積み、現在を新しいチェックポイントに
	undoStack_.push_back(committed_);
	if ( undoStack_.size() > 100 ) undoStack_.erase(undoStack_.begin());
	redoStack_.clear();
	committed_.lines = levelData_.railLines;
	committed_.types = levelData_.railTypes;
}

void LevelEditor::Undo(){
	if ( undoStack_.empty() ) return;
	// 現在をredoへ
	RailSnapshot cur; cur.lines = levelData_.railLines; cur.types = levelData_.railTypes;
	redoStack_.push_back(cur);
	RailSnapshot prev = undoStack_.back();
	undoStack_.pop_back();
	RestoreSnapshot(prev);
}

void LevelEditor::Redo(){
	if ( redoStack_.empty() ) return;
	RailSnapshot cur; cur.lines = levelData_.railLines; cur.types = levelData_.railTypes;
	undoStack_.push_back(cur);
	RailSnapshot next = redoStack_.back();
	redoStack_.pop_back();
	RestoreSnapshot(next);
}


// Blenderインポータ等の外部から変換済みデータを受け取って反映する
void LevelEditor::ApplyImportedData(const LevelData& data, bool additive){
	if ( !additive ) {
		levelData_.objects.clear();
		object3ds_.clear();
		selectedObjectIndex_ = -1;
	}
	if ( !data.name.empty() ) { levelData_.name = data.name; }

	for ( const auto& objData : data.objects ) {
		levelData_.objects.push_back(objData);

		Model* model = ModelManager::GetInstance()->FindModel(objData.type);
		// モデルが見つからない場合は球で代替表示（データは objData.type のまま保持）
		if ( !model ) { model = ModelManager::GetInstance()->FindModel("sphere"); }

		std::unique_ptr<Obj3d> newObj = std::make_unique<Obj3d>();
		newObj->Initialize(model);
		newObj->SetCamera(camera_);
		newObj->SetTranslation(objData.translation);
		newObj->SetRotation(objData.rotation);
		newObj->SetScale(objData.scale);
		newObj->Update();
		object3ds_.push_back(std::move(newObj));
	}
}

void LevelEditor::Update(){

	// ※ノードのキーボード移動は EditorManager（Game View側）に移行した。
	//   矢印キー=グリッド1マス移動 / Q,E=上下 / Delete=削除 / Ctrl+D=路線複製

	for ( auto& obj : object3ds_ ) { obj->Update(); }

	// ※レールの可視化(緑線)はゲーム側(GamePlayScene)が GetRailLines() を参照して描画する。
	//   ここで赤い球やパス点は生成・更新しない。
}

void LevelEditor::Draw(){
	for ( auto& obj : object3ds_ ) { obj->Draw(); }
	// レールの可視化（緑線）はゲーム側(GamePlayScene)が描画するため、ここでは何も描かない
}


void LevelEditor::DrawDebugUI(){
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
	//  1. ヒエラルキー ウィンドウ
	//     上段: マップファイル操作・モデル追加（折りたたみ）
	//     下段: オブジェクト一覧とドロップ先
	// =========================================================
	ImGui::Begin("ヒエラルキー (配置リスト)");
	if ( ImGui::CollapsingHeader("マップファイル・モデル追加", ImGuiTreeNodeFlags_DefaultOpen) ) {
	char buffer[256];
	strcpy_s(buffer, saveFileName_.c_str());
	if ( ImGui::InputText("保存ファイル名", buffer, sizeof(buffer)) ) {
		saveFileName_ = buffer;
	}
	std::string fullPath = "resources/map/" + saveFileName_;
	if ( ImGui::Button("マップを保存") ) { LevelManager::GetInstance()->Save(fullPath, levelData_); }
	ImGui::SameLine();
	if ( ImGui::Button("マップを読み込む") ) { LoadAndCreateMap(fullPath); }
	ImGui::SameLine();
	if ( ImGui::Button("マップをクリア") ) {
		object3ds_.clear();
		levelData_.objects.clear();
		selectedObjectIndex_ = -1;
	}
	ImGui::Separator();

	// アセット一覧（resources/ 走査結果）から選んで追加
	static int currentModelIndex = 0;
	if ( currentModelIndex >= ( int ) assetList_.size() ) currentModelIndex = 0;
	const char* previewName = assetList_.empty() ? "(なし)" : assetList_[currentModelIndex].display.c_str();
	if ( ImGui::BeginCombo("モデルの種類", previewName) ) {
		for ( int i = 0; i < ( int ) assetList_.size(); ++i ) {
			if ( ImGui::Selectable(assetList_[i].display.c_str(), currentModelIndex == i) ) {
				currentModelIndex = i;
			}
		}
		ImGui::EndCombo();
	}

	if ( ImGui::Button("選択したモデルを追加") && !assetList_.empty() ) {
		LevelObjectData newObj;
		newObj.type = assetList_[currentModelIndex].name;
		newObj.translation = { 0.0f, 0.0f, 0.0f };
		newObj.rotation = { 0.0f, 0.0f, 0.0f };
		newObj.scale = { 1.0f, 1.0f, 1.0f };

		EnsureAssetLoaded(newObj.type); // 未ロードならこのタイミングで自動ロード
		Model* model = ModelManager::GetInstance()->FindModel(newObj.type);
		if ( model != nullptr ) {
			// データと表示オブジェクトは必ずペアで追加する（インデックスのズレ防止）
			levelData_.objects.push_back(newObj);
			std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
			obj->Initialize(model);
			obj->SetCamera(camera_);
			object3ds_.push_back(std::move(obj));
			selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
		}
	}
	} // CollapsingHeader: マップファイル・モデル追加
	ImGui::Separator();

	// =========================================================
	//  2. オブジェクト一覧とドロップ先
	// =========================================================
	if ( ImGui::BeginListBox("##ObjectList", ImVec2(-FLT_MIN, -40.0f)) ) {
		for ( int i = 0; i < levelData_.objects.size(); ++i ) {
			std::string label = std::to_string(i) + ": " + levelData_.objects[i].type;
			if ( ImGui::Selectable(label.c_str(), selectedObjectIndex_ == i) ) { selectedObjectIndex_ = i; }
		}
		ImGui::EndListBox();
	}
	ImGui::Dummy(ImGui::GetContentRegionAvail());
	if ( ImGui::BeginDragDropTarget() ) {
		if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MODEL") ) {
			const char* droppedModelName = ( const char* ) payload->Data;
			LevelObjectData newObj;
			newObj.type = droppedModelName;
			newObj.translation = { 0.0f, 0.0f, 0.0f };
			newObj.rotation = { 0.0f, 0.0f, 0.0f };
			newObj.scale = { 1.0f, 1.0f, 1.0f };

			EnsureAssetLoaded(newObj.type); // 未ロードならこのタイミングで自動ロード
			Model* model = ModelManager::GetInstance()->FindModel(newObj.type);
			if ( model != nullptr ) {
				// データと表示オブジェクトは必ずペアで追加する（インデックスのズレ防止）
				levelData_.objects.push_back(newObj);
				std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
				obj->Initialize(model);
				obj->SetCamera(camera_);
				object3ds_.push_back(std::move(obj));
				selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
			}
		}
		ImGui::EndDragDropTarget();
	}
	ImGui::End();

	// =========================================================
	// ⚙ 3. Inspector ウィンドウ（選択中のオブジェクト編集）
	// =========================================================
	ImGui::Begin("インスペクター (詳細設定)");
	if ( selectedObjectIndex_ >= 0 && selectedObjectIndex_ < levelData_.objects.size() ) {
		auto& objData = levelData_.objects[selectedObjectIndex_];
		ImGui::Text("選択中: [%d] %s", selectedObjectIndex_, objData.type.c_str());
		ImGui::Separator();
		if ( ImGui::Button("オブジェクトを削除") ) {
			levelData_.objects.erase(levelData_.objects.begin() + selectedObjectIndex_);
			object3ds_.erase(object3ds_.begin() + selectedObjectIndex_);
			selectedObjectIndex_ = -1;
		}

		if ( selectedObjectIndex_ != -1 ) {
			ImGui::SameLine();
			if ( ImGui::Button("複製") ) {
				LevelObjectData dupObj = objData;
				levelData_.objects.push_back(dupObj);
				Model* model = ModelManager::GetInstance()->FindModel(dupObj.type);
				if ( model != nullptr ) {
					std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
					obj->Initialize(model);
					obj->SetCamera(camera_);
					obj->SetTranslation(dupObj.translation);
					obj->SetRotation(dupObj.rotation);
					obj->SetScale(dupObj.scale);
					object3ds_.push_back(std::move(obj));
					selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
				}
			}

			ImGui::Checkbox("グリッドにスナップ (1.0刻み)", &snapToGrid_);
			ImGui::Separator();
			bool isChanged = false;
			float moveStep = snapToGrid_ ? 1.0f : 0.1f;
			isChanged |= ImGui::DragFloat3("座標", &objData.translation.x, moveStep);
			isChanged |= ImGui::DragFloat3("回転", &objData.rotation.x, 0.05f);
			isChanged |= ImGui::DragFloat3("スケール", &objData.scale.x, 0.1f);

			if ( isChanged ) {
				if ( snapToGrid_ ) {
					objData.translation.x = std::round(objData.translation.x);
					objData.translation.y = std::round(objData.translation.y);
					objData.translation.z = std::round(objData.translation.z);
				}
				object3ds_[selectedObjectIndex_]->SetTranslation(objData.translation);
				object3ds_[selectedObjectIndex_]->SetRotation(objData.rotation);
				object3ds_[selectedObjectIndex_]->SetScale(objData.scale);
			}
		}
	} else {
		ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "オブジェクトが選択されていません");
	}
	ImGui::End();

	// =========================================================
	//  4. アセットブラウザ ウィンドウ（ドラッグ元）
	// =========================================================
	ImGui::Begin("アセットブラウザ (Assets)");

	ImGui::Text("【 3Dモデル 】 %d 件", ( int ) assetList_.size());
	ImGui::SameLine();
	if ( ImGui::Button("再スキャン") ) { ScanAssets(); }
	ImGui::TextDisabled("resources/ 内の .obj / .gltf を自動列挙");
	ImGui::Separator();

	// resources/ 走査で見つけたモデルをドラッグ＆ドロップ元として表示
	for ( int i = 0; i < ( int ) assetList_.size(); ++i ) {
		const AssetEntry& asset = assetList_[i];

		// リスト表示（同名モデルも区別できるよう相対パスで表示）
		ImGui::Selectable(asset.display.c_str());

		if ( ImGui::BeginDragDropSource(ImGuiDragDropFlags_None) ) {
			// "DND_MODEL" というラベルで、モデルの名前を荷物として送る
			ImGui::SetDragDropPayload("DND_MODEL", asset.name.c_str(), asset.name.size() + 1);

			// ドラッグ中にマウスカーソルにくっついて表示される文字
			ImGui::Text("モデルを配置: %s", asset.name.c_str());

			ImGui::EndDragDropSource();
		}
	}
	if ( assetList_.empty() ) {
		ImGui::TextDisabled("(モデルが見つかりません)");
	}

	ImGui::End();

	// =========================================================
	//  5. レールエディタ ウィンドウ（1つのウィンドウ内をタブで分割）
	//     「管理」タブ … 既存レールの一覧・プロパティ・座標
	//     「作成」タブ … 新しいレールを作る（シェイプ生成・マウス描画）
	// =========================================================
	ImGui::Begin("レールエディタ");
	if ( ImGui::BeginTabBar("RailEditorTabs") ) {
	if ( ImGui::BeginTabItem("管理 (Rails)") ) {

	// railTypes / railMotions を railLines と必ず同数に保つ
	if ( levelData_.railMotions.size() != levelData_.railLines.size() ) {
		levelData_.railMotions.resize(levelData_.railLines.size(), Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
	}
	if ( levelData_.railTypes.size() != levelData_.railLines.size() ) {
		levelData_.railTypes.resize(levelData_.railLines.size(), -1);
	}

	// --- 路線リスト（クリック＝路線まるごと選択 / 複製 / 削除）---
	ImGui::Text("路線リスト:");
	{
		int duplicateRail = -1;
		int deleteRail = -1;

		for ( int i = 0; i < ( int ) levelData_.railLines.size(); ++i ) {
			ImGui::PushID(i);
			bool isSelected = ( currentEditRailIndex_ == i );

			int t = levelData_.railTypes[i];
			const char* typeStr = ( t == 0 ) ? "横" : ( t == 1 ) ? "縦" : "自動";
			char label[64];
			snprintf(label, sizeof(label), "路線 %d  (%dノード, %s)",
				i, ( int ) levelData_.railLines[i].size(), typeStr);

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
		if ( deleteRail >= 0 && levelData_.railLines.size() > 1 ) {
			levelData_.railLines.erase(levelData_.railLines.begin() + deleteRail);
			levelData_.railTypes.erase(levelData_.railTypes.begin() + deleteRail);
			levelData_.railMotions.erase(levelData_.railMotions.begin() + deleteRail);
			if ( currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) {
				currentEditRailIndex_ = ( int ) levelData_.railLines.size() - 1;
			}
			selectedRailNode_ = -1;
			ClearMultiSelection();
			RebuildRailPoints();
		}
	}
	ImGui::Separator();

	// --- このレールの「移動操作タイプ」（横=A/D移動 / 縦=W/S移動）---
	if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) levelData_.railLines.size() ) {
		int& t = levelData_.railTypes[currentEditRailIndex_];

		// 自動判定の結果（表示用）：縦判定には Z が X の1.5倍以上必要（実装と同じ式）
		const auto& nodes = levelData_.railLines[currentEditRailIndex_];
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
		Vector4& motion = levelData_.railMotions[currentEditRailIndex_];
		bool motionChanged = false;
		ImGui::Text("動くレール (sin波で往復・全て0で停止):");
		motionChanged |= ImGui::DragFloat3("振幅 XYZ (m)", &motion.x, 0.05f);
		ImGui::SetNextItemWidth(110.0f);
		motionChanged |= ImGui::DragFloat("周期 (秒)", &motion.w, 0.05f, 0.1f, 60.0f);
		if ( motion.w < 0.1f ) motion.w = 0.1f;
		if ( motionChanged ) { ++railVersion_; } // ゲーム側へ即反映
		ImGui::TextDisabled("例: 振幅(0,0,3) 周期2 → 奥行きに±3mを2秒で往復");
		ImGui::Separator();
	}

	// Undo / Redo
	if ( ImGui::Button("元に戻す (Ctrl+Z)") ) { Undo(); }
	ImGui::SameLine();
	if ( ImGui::Button("やり直す (Ctrl+Y)") ) { Redo(); }

	ImGui::TextDisabled("【Game View操作】");
	ImGui::TextDisabled("線クリック→路線まるごと選択(ギズモで移動) / ノードクリック→1点選択");
	ImGui::TextDisabled("空白をドラッグ→矩形選択(まとめて移動) / Shift+クリック→追加選択");
	ImGui::TextDisabled("Ctrl+線クリック→ノード挿入 / 右クリック→ノード削除");
	ImGui::TextDisabled("【キーボード】矢印=1マス移動 / Q,E=下,上 / Delete=削除 / Ctrl+D=路線複製");

	// --- 路線全体を移動（数値での微調整用。ふだんはギズモで動かせる）---
	{
		static float wholeMove[3] = { 0.0f, 0.0f, 0.0f };
		ImGui::Text("路線全体を移動:");
		ImGui::SetNextItemWidth(180.0f);
		ImGui::DragFloat3("##WholeMove", wholeMove, 0.1f);
		ImGui::SameLine();
		if ( ImGui::Button("適用##WholeMove") ) {
			auto& line = levelData_.railLines[currentEditRailIndex_];
			for ( auto& n : line ) { n.x += wholeMove[0]; n.y += wholeMove[1]; n.z += wholeMove[2]; }
			wholeMove[0] = wholeMove[1] = wholeMove[2] = 0.0f;
			RebuildRailPoints();
		}
	}

	// --- 端点を溶接（データ自体をぴったり結合 → 実行時に座標がズレない）---
	if ( ImGui::Button("端点を溶接（近い端点をぴったり結合）") ) {
		const float kWeld = 0.7f; // ゲーム側の接続判定と同じ距離
		auto& lines = levelData_.railLines;

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
	ImGui::Separator();

	// --- 形を整えるツール（ワンクリックで 直線 / カーブ）---
	{
		auto& line = levelData_.railLines[currentEditRailIndex_];
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

		ImGui::PushItemWidth(140.0f);
		ImGui::SliderFloat("カーブの強さ", &curveAmount, -10.0f, 10.0f);
		const char* axisNames[] = { "X (横)", "Y (上)", "Z (奥)" };
		ImGui::Combo("カーブの向き", &curveAxis, axisNames, 3);
		ImGui::PopItemWidth();
		ImGui::TextDisabled("※カーブは中間ノードを曲げます。保存→Playでゲームに反映");
		ImGui::Separator();
	}

	ImGui::BeginChild("RailNodeList", ImVec2(0, 300), true);
	for ( size_t i = 0; i < levelData_.railLines[currentEditRailIndex_].size(); ++i ) {
		ImGui::PushID(static_cast< int >(i));
		std::string label = "Node " + std::to_string(i);

		if ( ImGui::Selectable(label.c_str(), selectedRailNode_ == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(80, 0)) ) {
			selectedRailNode_ = static_cast< int >(i);
		}
		ImGui::SameLine();
		ImGui::PushItemWidth(150.0f);
		if ( ImGui::DragFloat3(( "##" + label ).c_str(), &levelData_.railLines[currentEditRailIndex_][i].x, 0.5f) ) {
			++railVersion_; // 数値編集でも緑線をライブ更新
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();

		if ( ImGui::Button("↓ 下に挿入") ) {
			Vector3 newPos = levelData_.railLines[currentEditRailIndex_][i];
			if ( i + 1 < levelData_.railLines[currentEditRailIndex_].size() ) {
				newPos.x = ( levelData_.railLines[currentEditRailIndex_][i].x + levelData_.railLines[currentEditRailIndex_][i + 1].x ) * 0.5f;
				newPos.y = ( levelData_.railLines[currentEditRailIndex_][i].y + levelData_.railLines[currentEditRailIndex_][i + 1].y ) * 0.5f;
				newPos.z = ( levelData_.railLines[currentEditRailIndex_][i].z + levelData_.railLines[currentEditRailIndex_][i + 1].z ) * 0.5f;
			} else {
				newPos.z += 5.0f;
			}
			levelData_.railLines[currentEditRailIndex_].insert(levelData_.railLines[currentEditRailIndex_].begin() + i + 1, newPos);

			RebuildRailPoints(); // 配列が変わったので一括再構築！
			ImGui::PopID();
			break;
		}

		ImGui::SameLine();
		if ( ImGui::Button("削除") ) {
			levelData_.railLines[currentEditRailIndex_].erase(levelData_.railLines[currentEditRailIndex_].begin() + i);

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

	// --- 空の路線・押し出し ---
	if ( ImGui::Button("空の路線を追加") ) {
		levelData_.railLines.push_back(std::vector<Vector3>());
		levelData_.railTypes.push_back(-1);
		levelData_.railMotions.push_back(Vector4 { 0.0f, 0.0f, 0.0f, 2.0f });
		currentEditRailIndex_ = ( int ) levelData_.railLines.size() - 1;
		selectedRailNode_ = -1;
		ClearMultiSelection();
		RebuildRailPoints();
	}
	ImGui::SameLine();
	if ( ImGui::Button("末尾にノードを追加 (押し出し)") ) {
		if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) levelData_.railLines.size() ) {
			Vector3 newPos = { 0.0f, railDrawHeight_, 0.0f };
			if ( !levelData_.railLines[currentEditRailIndex_].empty() ) {
				newPos = levelData_.railLines[currentEditRailIndex_].back();
				newPos.z += 5.0f; // 奥へ
			}
			levelData_.railLines[currentEditRailIndex_].push_back(newPos);
			RebuildRailPoints();
		}
	}
	ImGui::Separator();

	// --- マウスで描く設定 ---
	ImGui::Text("マウスで描く:");
	ImGui::Checkbox("マウス追加モード（地面クリックで末尾に追加）", &railDrawMode_);
	ImGui::DragFloat("追加する高さ Y", &railDrawHeight_, 0.05f);
	ImGui::Checkbox("グリッド吸着", &railSnap_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("間隔(m)", &railGridSize_, 0.05f, 0.1f, 10.0f);
	ImGui::Checkbox("直角モード（X/Z軸に固定）", &railAxisLock_);
	ImGui::Checkbox("ノード吸着（他レール端点へ）", &railNodeSnap_);
	ImGui::SameLine();
	ImGui::Checkbox("フリーハンド", &railFreehand_);
	ImGui::Separator();

	// --- 方向ボタンで1マスずつ伸ばす（確実に直角・正確サイズ）---
	ImGui::Text("方向ボタンで伸ばす:");
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

	ImGui::EndTabItem();
	} // 作成タブ
	ImGui::EndTabBar();
	}
	ImGui::End();
#endif
}


void LevelEditor::SetCamera(const Camera* camera){
	camera_ = camera;
	for ( auto& obj : object3ds_ ) { obj->SetCamera(camera_); }

	for ( auto& lineSpheres : railSpheresAll_ ) {
		for ( auto& sphere : lineSpheres ) { sphere->SetCamera(camera_); }
	}
}