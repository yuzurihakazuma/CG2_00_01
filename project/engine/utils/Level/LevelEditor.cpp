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


LevelEditor::LevelEditor() = default;
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
		// 実際のモデルデータを検索して持ってくる
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

	for ( auto& obj : object3ds_ ) {
		obj->Update();
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

			// ドロップされたモデルを新しく追加！
			LevelObjectData newObj;
			newObj.type = droppedModelName;
			newObj.translation = { 0.0f, 0.0f, 0.0f }; // 原点に配置
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
				selectedObjectIndex_ = ( int ) levelData_.objects.size() - 1; // 今追加したものを選択状態にする
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


#endif
}


void LevelEditor::SetCamera(const Camera* camera){
	camera_ = camera;
	// 既にロード済みのオブジェクトにもカメラを反映する
	for ( auto& obj : object3ds_ ) {
		obj->SetCamera(camera_);
	}
}