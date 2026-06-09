#include "LevelEditor.h"

/// --- 標準ライブラリ ---
#include <cmath>

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
	LoadAndCreateMap("resources/map/map01.json");
}

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

// afterIndex の直後にノードを挿入
void LevelEditor::InsertRailNode(int afterIndex, const Vector3& p){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];
	int insertAt = afterIndex + 1;
	if ( insertAt < 0 ) insertAt = 0;
	if ( insertAt > ( int ) line.size() ) insertAt = ( int ) line.size();
	line.insert(line.begin() + insertAt, p);
	selectedRailNode_ = insertAt;
	++railVersion_;
}

// 指定ノードを削除
void LevelEditor::DeleteRailNode(int idx){
	if ( currentEditRailIndex_ < 0 || currentEditRailIndex_ >= ( int ) levelData_.railLines.size() ) return;
	auto& line = levelData_.railLines[currentEditRailIndex_];
	if ( idx < 0 || idx >= ( int ) line.size() ) return;
	line.erase(line.begin() + idx);
	selectedRailNode_ = -1;
	++railVersion_;
}

// ============================================================
// Undo / Redo
// ============================================================
void LevelEditor::RestoreSnapshot(const RailSnapshot& s){
	levelData_.railLines = s.lines;
	levelData_.railTypes = s.types;
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


void LevelEditor::Update(){

	// 選択したノードの移動
	if ( selectedRailNode_ >= 0 && selectedRailNode_ < levelData_.railLines[currentEditRailIndex_].size() ) {
		Input* input = Input::GetInstance();
		float moveStep = 0.2f;
		if ( input->Pushkey(DIK_UP) )    { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].z += moveStep; }
		if ( input->Pushkey(DIK_DOWN) )  { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].z -= moveStep; }
		if ( input->Pushkey(DIK_LEFT) )  { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].x -= moveStep; }
		if ( input->Pushkey(DIK_RIGHT) ) { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].x += moveStep; }
		if ( input->Pushkey(DIK_O) )     { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].y += moveStep; }
		if ( input->Pushkey(DIK_U) )     { levelData_.railLines[currentEditRailIndex_][selectedRailNode_].y -= moveStep; }

		// 矢印/O/Uでノードを動かしている間は編集とみなして世代番号を進める（緑線がライブ追従する）
		if ( input->Pushkey(DIK_UP) || input->Pushkey(DIK_DOWN) || input->Pushkey(DIK_LEFT) ||
			 input->Pushkey(DIK_RIGHT) || input->Pushkey(DIK_O) || input->Pushkey(DIK_U) ) {
			++railVersion_;
		}
	}

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
	//  1. Main Menu ウィンドウ（ファイル操作・追加）
	// =========================================================
	ImGui::Begin("メインメニュー");
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
	const char* modelNames[] = { "block", "fence", "plane", "sphere", "terrain", "animatedCube" };
	static int currentModelIndex = 0;
	ImGui::Combo("モデルの種類", &currentModelIndex, modelNames, IM_ARRAYSIZE(modelNames));
	if ( ImGui::Button("選択したモデルを追加") ) {
		LevelObjectData newObj;
		newObj.type = modelNames[currentModelIndex];
		newObj.translation = { 0.0f, 0.0f, 0.0f };
		newObj.rotation = { 0.0f, 0.0f, 0.0f };
		newObj.scale = { 1.0f, 1.0f, 1.0f };
		levelData_.objects.push_back(newObj);

		Model* model = ModelManager::GetInstance()->FindModel(newObj.type);
		if ( model != nullptr ) {
			std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
			obj->Initialize(model);
			obj->SetCamera(camera_);
			object3ds_.push_back(std::move(obj));
			selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
		}
	}
	ImGui::End();

	// =========================================================
	//  2. Hierarchy ウィンドウ（オブジェクト一覧とドロップ先）
	// =========================================================
	ImGui::Begin("ヒエラルキー (配置リスト)");
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
			levelData_.objects.push_back(newObj);

			Model* model = ModelManager::GetInstance()->FindModel(newObj.type);
			if ( model != nullptr ) {
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
	ImGui::Text("【 3Dモデル 】");
	ImGui::Separator();
	const char* assetModels[] = { "block", "fence", "plane", "sphere", "terrain", "animatedCube" };
	for ( int i = 0; i < IM_ARRAYSIZE(assetModels); ++i ) {
		ImGui::Selectable(assetModels[i]);
		if ( ImGui::BeginDragDropSource(ImGuiDragDropFlags_None) ) {
			ImGui::SetDragDropPayload("DND_MODEL", assetModels[i], strlen(assetModels[i]) + 1);
			ImGui::Text("モデルを配置: %s", assetModels[i]);
			ImGui::EndDragDropSource();
		}
	}
	ImGui::End();

	// =========================================================
	//  5. レールエディタ ウィンドウ
	// =========================================================
	ImGui::Begin("レールエディタ");

	// railTypes を railLines と必ず同数に保つ（-1=自動）
	if ( levelData_.railTypes.size() != levelData_.railLines.size() ) {
		levelData_.railTypes.resize(levelData_.railLines.size(), -1);
	}

	ImGui::Text("編集する路線を選択:");
	for ( int i = 0; i < levelData_.railLines.size(); ++i ) {
		std::string btnLabel = "路線 " + std::to_string(i);
		if ( i > 0 ) ImGui::SameLine();

		bool isSelected = ( currentEditRailIndex_ == i );
		if ( isSelected ) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
		}

		if ( ImGui::Button(btnLabel.c_str()) ) {
			currentEditRailIndex_ = i;
			selectedRailNode_ = -1; // 選択リセット
		}

		if ( isSelected ) {
			ImGui::PopStyleColor();
		}
	}
	ImGui::Separator();

	// --- このレールの「移動操作タイプ」（横=A/D移動 / 縦=W/S移動）---
	if ( currentEditRailIndex_ >= 0 && currentEditRailIndex_ < ( int ) levelData_.railLines.size() ) {
		int& t = levelData_.railTypes[currentEditRailIndex_];

		// 自動判定の結果（表示用）：front→back の主軸 |X|>=|Z| なら横
		const auto& nodes = levelData_.railLines[currentEditRailIndex_];
		int autoType = 0; // 0=横 / 1=縦
		if ( nodes.size() >= 2 ) {
			float dx = nodes.back().x - nodes.front().x; if ( dx < 0 ) dx = -dx;
			float dz = nodes.back().z - nodes.front().z; if ( dz < 0 ) dz = -dz;
			autoType = ( dx >= dz ) ? 0 : 1;
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
		ImGui::Separator();
	}

	if ( ImGui::Button("新しい路線(分岐)を追加") ) {
		levelData_.railLines.push_back(std::vector<Vector3>());
		currentEditRailIndex_ = static_cast< int >( levelData_.railLines.size() - 1 );
		selectedRailNode_ = -1;
		RebuildRailPoints(); // リストが増えたので再構築！
	}
	ImGui::Separator();

	if ( ImGui::Button("末尾にノードを追加 (押し出し)") ) {
		Vector3 newPos = { 0.0f, 0.0f, 0.0f };
		if ( !levelData_.railLines[currentEditRailIndex_].empty() ) {
			newPos = levelData_.railLines[currentEditRailIndex_].back();
			newPos.z += 5.0f; // 奥へ
		}
		levelData_.railLines[currentEditRailIndex_].push_back(newPos);
		RebuildRailPoints(); // 変更したので再構築
	}

	// --- マウスでレールを引く ---
	ImGui::Checkbox("マウス追加モード（地面クリックで末尾に追加）", &railDrawMode_);
	ImGui::DragFloat("追加する高さ Y", &railDrawHeight_, 0.05f);

	// グリッド・直角設定（マウスもボタンも共通で使う）
	ImGui::Checkbox("グリッド吸着", &railSnap_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("間隔(m)", &railGridSize_, 0.05f, 0.1f, 10.0f);
	ImGui::Checkbox("直角モード（X/Z軸に固定）", &railAxisLock_);
	ImGui::Checkbox("ノード吸着（他レール端点へ）", &railNodeSnap_);
	ImGui::SameLine();
	ImGui::Checkbox("フリーハンド", &railFreehand_);

	// Undo / Redo
	if ( ImGui::Button("元に戻す (Ctrl+Z)") ) { Undo(); }
	ImGui::SameLine();
	if ( ImGui::Button("やり直す (Ctrl+Y)") ) { Redo(); }

	ImGui::TextDisabled("Game View: ノードをクリック→ギズモ移動 / 右クリック→削除");
	ImGui::TextDisabled("線の上をクリック→途中に挿入 / 地面クリック→末尾追加");

	// 方向ボタンで1マスずつ伸ばす（確実に直角・正確サイズ）
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
	ImGui::End();

	ImGui::Separator();
#endif
}


void LevelEditor::SetCamera(const Camera* camera){
	camera_ = camera;
	for ( auto& obj : object3ds_ ) { obj->SetCamera(camera_); }

	for ( auto& lineSpheres : railSpheresAll_ ) {
		for ( auto& sphere : lineSpheres ) { sphere->SetCamera(camera_); }
	}
}