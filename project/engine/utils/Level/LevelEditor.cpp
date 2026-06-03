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
	railSpheresAll_.clear();
	pathPointsAll_.clear();

	Model* sphereModel = ModelManager::GetInstance()->FindModel("sphere");
	if ( !sphereModel ) return;

	for ( size_t i = 0; i < levelData_.railLines.size(); ++i ) {
		std::vector<std::unique_ptr<Obj3d>> lineSpheres;
		for ( const auto& pos : levelData_.railLines[i] ) {
			auto sp = std::make_unique<Obj3d>();
			sp->Initialize(sphereModel);
			sp->SetCamera(camera_);
			sp->SetScale({ 0.5f, 0.5f, 0.5f });
			sp->SetTranslation(pos);
			sp->Update();
			lineSpheres.push_back(std::move(sp));
		}
		railSpheresAll_.push_back(std::move(lineSpheres));

		// パスポイント用（Updateで中身を生成する）
		pathPointsAll_.push_back(std::vector<std::unique_ptr<Obj3d>>());
	}
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
	}

	for ( auto& obj : object3ds_ ) { obj->Update(); }

	Model* sphereModel = ModelManager::GetInstance()->FindModel("sphere");

	// ★すべての路線の球体と軌道を更新
	for ( size_t i = 0; i < levelData_.railLines.size(); ++i ) {
		// 1. ノード球体の位置を最新の座標に合わせる
		if ( railSpheresAll_.size() > i && railSpheresAll_[i].size() == levelData_.railLines[i].size() ) {
			for ( size_t j = 0; j < railSpheresAll_[i].size(); ++j ) {
				railSpheresAll_[i][j]->SetTranslation(levelData_.railLines[i][j]);

				// 現在編集中の路線の選択中ノードは少し大きく表示！
				if ( i == currentEditRailIndex_ && j == selectedRailNode_ ) {
					railSpheresAll_[i][j]->SetScale({ 0.8f, 0.8f, 0.8f });
				} else {
					railSpheresAll_[i][j]->SetScale({ 0.4f, 0.4f, 0.4f });
				}
				railSpheresAll_[i][j]->Update();
			}
		}

		// 2. 軌道(見えないレール)を計算して細かい点を配置
		SplineRail previewRail;
		for ( const auto& pos : levelData_.railLines[i] ) {
			previewRail.nodes.push_back(pos);
		}

		if ( previewRail.nodes.size() >= 2 && pathPointsAll_.size() > i ) {
			float maxT = static_cast< float >( previewRail.nodes.size() - 1 );
			int requiredPoints = static_cast< int >( maxT * 10.0f );

			// 足りない分だけ追加
			while ( pathPointsAll_[i].size() < requiredPoints && sphereModel ) {
				auto point = std::make_unique<Obj3d>();
				point->Initialize(sphereModel);
				point->SetCamera(camera_);
				point->SetScale({ 0.1f, 0.1f, 0.1f });
				pathPointsAll_[i].push_back(std::move(point));
			}

			// 座標更新
			float step = maxT / ( requiredPoints - 1 );
			for ( int j = 0; j < requiredPoints; ++j ) {
				float t = j * step;
				pathPointsAll_[i][j]->SetTranslation(previewRail.EvaluatePosition(t));
				pathPointsAll_[i][j]->Update();
			}

			// 余分な点は画面外へ隠す
			for ( size_t j = requiredPoints; j < pathPointsAll_[i].size(); ++j ) {
				pathPointsAll_[i][j]->SetTranslation({ 0.0f, -9999.0f, 0.0f });
				pathPointsAll_[i][j]->Update();
			}
		}
	}
}

void LevelEditor::Draw(){
	for ( auto& obj : object3ds_ ) { obj->Draw(); }

	// すべての路線の球体を描画
	for ( auto& lineSpheres : railSpheresAll_ ) {
		for ( auto& sphere : lineSpheres ) { sphere->Draw(); }
	}
	// すべての軌道を描画
	for ( auto& pathPoints : pathPointsAll_ ) {
		for ( auto& point : pathPoints ) { point->Draw(); }
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
	ImGui::Separator();

	ImGui::BeginChild("RailNodeList", ImVec2(0, 300), true);
	for ( size_t i = 0; i < levelData_.railLines[currentEditRailIndex_].size(); ++i ) {
		ImGui::PushID(static_cast< int >(i));
		std::string label = "Node " + std::to_string(i);

		if ( ImGui::Selectable(label.c_str(), selectedRailNode_ == i, ImGuiSelectableFlags_AllowOverlap, ImVec2(80, 0)) ) {
			selectedRailNode_ = static_cast< int >(i);
		}
		ImGui::SameLine();
		ImGui::PushItemWidth(150.0f);
		ImGui::DragFloat3(( "##" + label ).c_str(), &levelData_.railLines[currentEditRailIndex_][i].x, 0.5f);
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