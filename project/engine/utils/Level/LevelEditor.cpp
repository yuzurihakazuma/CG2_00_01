#include "LevelEditor.h"

/// --- 標準ライブラリ ---
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <algorithm>

// --- エンジン側のファイル ---
#include "LevelManager.h"
#include "RailEditor.h"
#include "engine/3d/obj/Obj3d.h"
#include "engine/utils/ImGuiManager.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/base/Input.h"
#include "engine/camera/Camera.h" // CalcSpawnPoint（カメラの視線の先に配置）用


LevelEditor::LevelEditor(){
	// レール編集は専用クラス(RailEditor)へ分離。データ(levelData_)のアドレスを渡して参照させる。
	railEditor_ = std::make_unique<RailEditor>(&levelData_);
}
LevelEditor::~LevelEditor() = default;



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

	// レール編集の選択・履歴をリセット（空なら1本用意する）
	railEditor_->OnMapChanged();

	++mapLoadVersion_; // シーンが敵を読み直す合図
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

	// 5. レールエディタ（レール編集は RailEditor クラスへ分離した）
	railEditor_->DrawWindow();

#endif
}


void LevelEditor::SetCamera(const Camera* camera){
	camera_ = camera;
	for ( auto& obj : object3ds_ ) { obj->SetCamera(camera_); }
}

// =====================================================================
//  外部ツール連携（FileEditor / NodeEditor 用。master_engine から移植）
// =====================================================================

// カメラの視線の先の配置位置を計算する
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

// モデル名を指定してカメラの前に1個配置する（FileEditor のダブルクリック配置用）
void LevelEditor::SpawnObject(const std::string& type){
	if ( !EnsureAssetLoaded(type) ) return;
	Model* model = ModelManager::GetInstance()->FindModel(type);
	if ( model == nullptr ) return;

	Vector3 p = CalcSpawnPoint();
	if ( snapToGrid_ ) { p.x = std::round(p.x); p.y = std::round(p.y); p.z = std::round(p.z); }

	LevelObjectData newObj;
	newObj.type = type;
	newObj.translation = p;

	// データと表示オブジェクトは必ずペアで追加する（インデックスのズレ防止）
	levelData_.objects.push_back(newObj);
	std::unique_ptr<Obj3d> obj = std::make_unique<Obj3d>();
	obj->Initialize(model);
	obj->SetCamera(camera_);
	obj->SetTranslation(p);
	obj->Update();
	object3ds_.push_back(std::move(obj));

	selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1;
	++spawnCounter_;
}

int LevelEditor::GetObjectCount() const{
	return ( int ) levelData_.objects.size();
}

std::string LevelEditor::GetObjectLabel(int index) const{
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return "(なし)";
	return std::to_string(index) + ": " + levelData_.objects[index].type;
}

Obj3d* LevelEditor::GetObject3d(int index){
	if ( index < 0 || index >= ( int ) object3ds_.size() ) return nullptr;
	return object3ds_[index].get();
}

void LevelEditor::SetObjectPosY(int index, float y){
	if ( index < 0 || index >= ( int ) levelData_.objects.size() ) return;
	if ( index >= ( int ) object3ds_.size() ) return;
	levelData_.objects[index].translation.y = y;
	object3ds_[index]->SetTranslation(levelData_.objects[index].translation);
	// ノード駆動のアニメーションなので Undo には積まない
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