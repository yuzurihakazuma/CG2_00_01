#include "LevelEditor.h"

/// --- 標準ライブラリ ---
#include <cmath>
#include <cctype>
#include <filesystem>
#include <algorithm>

// --- エンジン側のファイル ---
#include "LevelManager.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/camera/Camera.h"


LevelEditor::LevelEditor() = default;
LevelEditor::~LevelEditor() = default;


void LevelEditor::Initialize(){
	// アセットブラウザ用に resources/ を走査
	ScanAssets();
	// マップファイル一覧を走査
	ScanMaps();

	// 最初は空の状態でスタートするか、デフォルトのマップを読み込む
	LoadAndCreateMap("resources/map/map01.json");
}

// resources/map/ を走査して .json 一覧を更新する
void LevelEditor::ScanMaps(){
	mapList_.clear();
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir("resources/map");
	if ( !fs::exists(dir, ec) ) return;
	for ( const auto& entry : fs::directory_iterator(dir, ec) ) {
		if ( !entry.is_regular_file() ) continue;
		std::string ext = entry.path().extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c){ return ( char ) std::tolower(c); });
		if ( ext != ".json" ) continue;
		mapList_.push_back(entry.path().filename().string());
	}
	std::sort(mapList_.begin(), mapList_.end());

	// 現在ファイルが一覧の何番目かを選択状態に反映
	selectedMapIndex_ = -1;
	for ( int i = 0; i < ( int ) mapList_.size(); ++i ) {
		if ( "resources/map/" + mapList_[i] == currentMapFile_ ) { selectedMapIndex_ = i; break; }
	}
}

// levelData_ の内容から表示用 object3ds_ を作り直す（Undo/Redo用）
void LevelEditor::RebuildObjects(){
	object3ds_.clear();
	for ( auto& objData : levelData_.objects ) {
		Model* model = ModelManager::GetInstance()->FindModel(objData.type);
		// 見つからなくても Update() の遅延解決に任せる（nullptrのまま作ってOK）
		std::unique_ptr<Obj3d> newObj = std::make_unique<Obj3d>();
		newObj->Initialize(model);
		newObj->SetCamera(camera_);
		newObj->SetTranslation(objData.translation);
		newObj->SetRotation(objData.rotation);
		newObj->SetScale(objData.scale);
		newObj->Update();
		object3ds_.push_back(std::move(newObj));
	}
	if ( selectedObjectIndex_ >= ( int ) levelData_.objects.size() ) {
		selectedObjectIndex_ = -1;
	}
}

// 変更「前」に呼んで履歴に積む
void LevelEditor::PushUndo(){
	undoStack_.push_back(levelData_);
	// 積みすぎ防止（50手まで）
	if ( undoStack_.size() > 50 ) { undoStack_.erase(undoStack_.begin()); }
	// 新しい操作をしたら Redo 履歴は無効
	redoStack_.clear();
}

void LevelEditor::Undo(){
	if ( undoStack_.empty() ) return;
	redoStack_.push_back(levelData_);
	levelData_ = undoStack_.back();
	undoStack_.pop_back();
	RebuildObjects();
	dirty_ = true;
}

void LevelEditor::Redo(){
	if ( redoStack_.empty() ) return;
	undoStack_.push_back(levelData_);
	levelData_ = redoStack_.back();
	redoStack_.pop_back();
	RebuildObjects();
	dirty_ = true;
}

// --- ノードエディタ用アクセス ---
int LevelEditor::GetObjectCount() const{
	return ( int ) levelData_.objects.size();
}

std::string LevelEditor::GetObjectLabel(int index) const{
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return "(なし)";
	return std::to_string(index) + ": " + levelData_.objects[index].type;
}

void LevelEditor::SetObjectPosY(int index, float y){
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return;
	if ( index >= ( int ) object3ds_.size() ) return;
	levelData_.objects[index].translation.y = y;
	object3ds_[index]->SetTranslation(levelData_.objects[index].translation);
	// ノード駆動のアニメーションなので dirty_ / Undo には積まない
}

void LevelEditor::SetObjectRotY(int index, float r){
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return;
	if ( index >= ( int ) object3ds_.size() ) return;
	levelData_.objects[index].rotation.y = r;
	object3ds_[index]->SetRotation(levelData_.objects[index].rotation);
}

void LevelEditor::SetObjectScale(int index, float s){
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return;
	if ( index >= ( int ) object3ds_.size() ) return;
	levelData_.objects[index].scale = { s, s, s };
	object3ds_[index]->SetScale(levelData_.objects[index].scale);
}

void LevelEditor::SetObjectShaderParam(int index, float v){
	if ( index < 0 || index >= ( int ) object3ds_.size() ) return;
	// ディゾルブ用CBを「自由パラメータ」として使う
	// （生成シェーダーの『パラメータ』ノードがこの値を読む。
	//   通常シェーダーのオブジェクトに使うとディゾルブとして作用する点に注意）
	object3ds_[index]->SetDissolveThreshold(v);
}

Obj3d* LevelEditor::GetObject3d(int index){
	if ( index < 0 || index >= ( int ) object3ds_.size() ) return nullptr;
	return object3ds_[index].get();
}

// 今開いているファイルに上書き保存する
void LevelEditor::QuickSave(){
	LevelManager::GetInstance()->Save(currentMapFile_, levelData_);
	dirty_ = false;
	autoSaveTimer_ = 0.0f;
	ScanMaps(); // 新規に作られたファイルを一覧へ反映
}

// カメラの前方にあるスポーン地点を計算する
Vector3 LevelEditor::CalcSpawnPoint() const{
	if ( !camera_ ) return { 0.0f, 0.0f, 5.0f };
	// カメラのワールド行列の +Z 軸（3行目）＝視線方向
	const Matrix4x4& w = camera_->GetWorldMatrix();
	Vector3 fwd = { w.m[2][0], w.m[2][1], w.m[2][2] };
	float len = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
	if ( len > 0.0001f ) { fwd.x /= len; fwd.y /= len; fwd.z /= len; }
	Vector3 pos = camera_->GetWorldPosition();
	// 視線の少し先に置く。連続配置時は少しずつずらして重ならないようにする
	float dist = 8.0f;
	Vector3 p{ pos.x + fwd.x * dist, pos.y + fwd.y * dist, pos.z + fwd.z * dist };
	float off = ( float ) ( spawnCounter_ % 4 );
	p.x += off; p.z += off;
	return p;
}

// 見える位置にモデルを1個配置する共通処理
void LevelEditor::SpawnObject(const std::string& type){
	if ( !EnsureAssetLoaded(type) ) return;
	Model* model = ModelManager::GetInstance()->FindModel(type);
	if ( model == nullptr ) return;

	PushUndo(); // 配置前の状態を履歴へ

	Vector3 p = CalcSpawnPoint();
	if ( snapToGrid_ ) { p.x = std::round(p.x); p.y = std::round(p.y); p.z = std::round(p.z); }

	LevelObjectData newObj;
	newObj.type = type;
	newObj.translation = p;

	levelData_.objects.push_back(newObj);
	std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
	obj->Initialize(model);
	obj->SetCamera(camera_);
	obj->SetTranslation(p);
	obj->Update();
	object3ds_.push_back(std::move(obj));

	selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
	++spawnCounter_;
	dirty_ = true;
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
	currentMapFile_ = fileName; // 以後の上書き保存先
	dirty_ = false;
	autoSaveTimer_ = 0.0f;
	// 別マップを開いたら履歴はリセット
	undoStack_.clear();
	redoStack_.clear();

	for ( auto& objData : levelData_.objects ) {
		// 実際のモデルデータを検索して持ってくる（この時点で未ロードなら後で解決する）
		Model* model = ModelManager::GetInstance()->FindModel(objData.type);

		std::unique_ptr<Obj3d> newObj = std::make_unique<Obj3d>();
		// 検索したモデルを渡す！
		newObj->Initialize(model);
		newObj->SetCamera(camera_);
		newObj->SetTranslation(objData.translation);
		newObj->SetRotation(objData.rotation);
		newObj->SetScale(objData.scale);
		newObj->Update();
		object3ds_.push_back(std::move(newObj));
	}
	selectedObjectIndex_ = -1;
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

	// まだモデルが解決できていないオブジェクトを解決する。
	// （エディタは Framework 初期化時にマップを読むため、シーンがモデルを
	//   ロードするより早い。ここで毎フレーム FindModel し、見つかったら差し込む。
	//   ※ここでは読み込み(LoadModel)はせず検索のみなので安全）
	size_t n = ( std::min )( object3ds_.size(), levelData_.objects.size() );
	for ( size_t i = 0; i < n; ++i ) {
		if ( object3ds_[i]->GetModel() == nullptr ) {
			Model* m = ModelManager::GetInstance()->FindModel(levelData_.objects[i].type);
			if ( m ) { object3ds_[i]->SetModel(m); }
		}
	}

	for ( auto& obj : object3ds_ ) {
		obj->Update();
	}

	// 自動保存：変更があれば少し待ってから上書き保存（毎フレーム書かないようにデバウンス）
	if ( autoSave_ && dirty_ ) {
		autoSaveTimer_ += 1.0f;
		if ( autoSaveTimer_ >= 60.0f ) { // 約1秒後
			QuickSave();
		}
	}
}
void LevelEditor::Draw(){
	for ( auto& obj : object3ds_ ) {
		obj->Draw();
	}
}


void LevelEditor::DrawDebugUI(){
#ifdef USE_IMGUI


	// =========================================================
	//  1. Main Menu ウィンドウ（ファイル操作・追加）
	// =========================================================
	ImGui::Begin("メインメニュー");

	// --- 現在のファイルと保存状態 ---
	ImGui::Text("現在のマップ: %s", currentMapFile_.c_str());
	ImGui::SameLine();
	if ( dirty_ ) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[未保存]");
	else          ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "[保存済]");

	// 上書き保存（名前入力不要）。Ctrl+S でも保存
	if ( ImGui::Button("上書き保存") ) { QuickSave(); }
	ImGui::SameLine();
	ImGui::Checkbox("自動保存", &autoSave_);
	if ( ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) ) { QuickSave(); }

	ImGui::Separator();

	// --- マップ一覧から選んで読み込む（名前手入力不要） ---
	const char* mapPreview = ( selectedMapIndex_ >= 0 && selectedMapIndex_ < ( int ) mapList_.size() )
		? mapList_[selectedMapIndex_].c_str() : "(選択してください)";
	if ( ImGui::BeginCombo("マップ一覧", mapPreview) ) {
		for ( int i = 0; i < ( int ) mapList_.size(); ++i ) {
			if ( ImGui::Selectable(mapList_[i].c_str(), selectedMapIndex_ == i) ) selectedMapIndex_ = i;
		}
		ImGui::EndCombo();
	}
	if ( ImGui::Button("選択マップを読み込む")
		&& selectedMapIndex_ >= 0 && selectedMapIndex_ < ( int ) mapList_.size() ) {
		LoadAndCreateMap("resources/map/" + mapList_[selectedMapIndex_]);
	}
	ImGui::SameLine();
	if ( ImGui::Button("一覧を更新") ) { ScanMaps(); }

	ImGui::Separator();

	// --- 別名で保存 / 新規作成 ---
	char buffer[256];
	strcpy_s(buffer, saveFileName_.c_str());
	if ( ImGui::InputText("新規 / 別名", buffer, sizeof(buffer)) ) { saveFileName_ = buffer; }
	if ( ImGui::Button("この名前で保存（新規作成）") ) {
		std::string name = saveFileName_;
		if ( name.size() < 5 || name.substr(name.size() - 5) != ".json" ) name += ".json";
		currentMapFile_ = "resources/map/" + name; // 以後の上書き先を新ファイルに切り替え
		QuickSave();
	}
	ImGui::SameLine();
	if ( ImGui::Button("マップをクリア") ) {
		PushUndo(); // クリア前を履歴へ（Ctrl+Zで戻せる）
		object3ds_.clear();
		levelData_.objects.clear();
		selectedObjectIndex_ = -1;
		dirty_ = true;
	}

	ImGui::Separator();

	// --- Undo / Redo ---
	if ( ImGui::Button("元に戻す (Ctrl+Z)") ) { Undo(); }
	ImGui::SameLine();
	if ( ImGui::Button("やり直す (Ctrl+Y)") ) { Redo(); }
	ImGui::SameLine();
	ImGui::TextDisabled("履歴 %d / 先 %d", ( int ) undoStack_.size(), ( int ) redoStack_.size());
	// キーボードショートカット（テキスト入力中は無効）
	if ( !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl ) {
		if ( ImGui::IsKeyPressed(ImGuiKey_Z) ) { Undo(); }
		if ( ImGui::IsKeyPressed(ImGuiKey_Y) ) { Redo(); }
	}

	ImGui::Separator();

	// --- クイック配置（カメラの前にポンと出す） ---
	ImGui::TextDisabled("クイック配置（カメラの見ている先に出ます）");
	if ( ImGui::Button("ブロックを置く") ) { SpawnObject("block"); }

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
		SpawnObject(assetList_[currentModelIndex].name);
	}
	ImGui::End();

	// =========================================================
	//  2. Hierarchy ウィンドウ（オブジェクト一覧とドロップ先）
	// =========================================================
	ImGui::Begin("ヒエラルキー (配置リスト)");

	// リストを広げる（一番下にドロップ用の余白を少し残すため -40.0f にする）
	if ( ImGui::BeginListBox("##ObjectList", ImVec2(-FLT_MIN, -40.0f)) ) {
		for ( int i = 0; i < levelData_.objects.size(); ++i ) {
			std::string label = std::to_string(i) + ": " + levelData_.objects[i].type;
			if ( ImGui::Selectable(label.c_str(), selectedObjectIndex_ == i) ) {
				selectedObjectIndex_ = i;
			}
		}
		ImGui::EndListBox();
	}

	//  ドロップ先の的（まと） 🌟🌟
	// ウィンドウの残りのスペースを「見えない的」にする
	ImGui::Dummy(ImGui::GetContentRegionAvail());

	// もし、この「ヒエラルキー」ウィンドウの余白に何かがドロップされたら…
	if ( ImGui::BeginDragDropTarget() ) {
		// "DND_MODEL" というラベルの荷物を受け取る
		if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MODEL") ) {
			// 荷物（文字列）を取り出す
			const char* droppedModelName = ( const char* ) payload->Data;

			// ドロップされたモデルをカメラ前方に追加！
			SpawnObject(droppedModelName);
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
			PushUndo(); // 削除前を履歴へ
			levelData_.objects.erase(levelData_.objects.begin() + selectedObjectIndex_);
			object3ds_.erase(object3ds_.begin() + selectedObjectIndex_);
			selectedObjectIndex_ = -1;
			dirty_ = true;
		}

		if ( selectedObjectIndex_ != -1 ) {
			ImGui::SameLine();
			if ( ImGui::Button("複製") ) {
				PushUndo(); // 複製前を履歴へ
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
					dirty_ = true;
				}
			}

			ImGui::Checkbox("グリッドにスナップ (1.0刻み)", &snapToGrid_);
			ImGui::Separator();

			bool isChanged = false;
			float moveStep = snapToGrid_ ? 1.0f : 0.1f;
			isChanged |= ImGui::DragFloat3("座標", &objData.translation.x, moveStep);
			if ( ImGui::IsItemActivated() ) { PushUndo(); } // ドラッグ開始時に1回だけ履歴へ
			isChanged |= ImGui::DragFloat3("回転", &objData.rotation.x, 0.05f);
			if ( ImGui::IsItemActivated() ) { PushUndo(); }
			isChanged |= ImGui::DragFloat3("スケール", &objData.scale.x, 0.1f);
			if ( ImGui::IsItemActivated() ) { PushUndo(); }

			if ( isChanged ) {
				if ( snapToGrid_ ) {
					objData.translation.x = std::round(objData.translation.x);
					objData.translation.y = std::round(objData.translation.y);
					objData.translation.z = std::round(objData.translation.z);
				}
				object3ds_[selectedObjectIndex_]->SetTranslation(objData.translation);
				object3ds_[selectedObjectIndex_]->SetRotation(objData.rotation);
				object3ds_[selectedObjectIndex_]->SetScale(objData.scale);
					dirty_ = true;
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


#endif
}


void LevelEditor::SetCamera(const Camera* camera){
	camera_ = camera;
	// 既にロード済みのオブジェクトにもカメラを反映する
	for ( auto& obj : object3ds_ ) {
		obj->SetCamera(camera_);
	}
}