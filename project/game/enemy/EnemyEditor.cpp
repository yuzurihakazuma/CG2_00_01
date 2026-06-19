#include "EnemyEditor.h"
#include "engine/rail/SplineRail.h"
#include "externals/imgui/imgui.h"
#include <algorithm>
#include <cstdio>
#include <string>

EnemyEditor::EnemyEditor() = default;
EnemyEditor::~EnemyEditor() = default;

// 敵タイプ名のヘルパー（コンボボックスや詳細表示用）
const char* EnemyEditor::GetTypeName(EnemyType type) {
    switch ( type ) {
    case EnemyType::Zako:   return "Zako (雑魚敵)";
    case EnemyType::Strong: return "Strong (強敵)";
    default:                return "Unknown";
    }
}

// 一覧行に収まるよう短縮した名前
const char* EnemyEditor::GetTypeLabel(EnemyType type) {
    switch ( type ) {
    case EnemyType::Zako:   return "Zako";
    case EnemyType::Strong: return "Strong";
    default:                return "???";
    }
}

void EnemyEditor::Initialize() {
    // 起動時にテスト用の初期エネミー（Zako 1体）を登録しておく
    spawnDatas_.clear();
    // レール0、距離5.0mの場所に雑魚敵を初期配置する
    spawnDatas_.push_back({ EnemyType::Zako, 0, 5.0f });
    changed_ = true;
}

// ================================================================
//  レール選択用ドロップダウンを描画するヘルパー関数
//  SliderInt の代わりに情報付きコンボボックスを使い、
//  レール番号・タイプ・長さ・配置済み敵数を表示して選択できるようにする
// ================================================================
static bool DrawRailCombo(const char* label,
                          int* railIndex,
                          const std::vector<SplineRail>& rails,
                          const std::vector<EnemySpawnData>& spawns) {
    if ( rails.empty() ) return false;

    // 各レールに配置済みの敵数を数える
    std::vector<int> countPerRail(rails.size(), 0);
    for ( const auto& s : spawns ) {
        if ( s.railIndex >= 0 && s.railIndex < static_cast<int>(rails.size()) ) {
            countPerRail[s.railIndex]++;
        }
    }

    // 現在選択中のレールのプレビュー文字列を作成
    *railIndex = std::clamp(*railIndex, 0, static_cast<int>(rails.size()) - 1);
    const SplineRail& cur = rails[*railIndex];
    const char* curType = (cur.type == SplineRail::RailType::Horizontal) ? "横" : "縦";
    char preview[128];
    std::snprintf(preview, sizeof(preview), "Rail %d  [%s] %.1fm",
        *railIndex, curType, cur.GetLength());

    bool changed = false;
    if ( ImGui::BeginCombo(label, preview) ) {
        for ( int i = 0; i < static_cast<int>(rails.size()); ++i ) {
            const SplineRail& r = rails[i];
            const char* rType = (r.type == SplineRail::RailType::Horizontal) ? "横" : "縦";

            // ラベル：レール番号 / タイプ / 長さ / ループ有無 / 配置済み数
            char itemLabel[128];
            if ( countPerRail[i] > 0 ) {
                std::snprintf(itemLabel, sizeof(itemLabel),
                    "Rail %d  [%s] %.1fm%s  (%d体)",
                    i, rType, r.GetLength(),
                    r.isLoop ? " Loop" : "",
                    countPerRail[i]);
            } else {
                std::snprintf(itemLabel, sizeof(itemLabel),
                    "Rail %d  [%s] %.1fm%s",
                    i, rType, r.GetLength(),
                    r.isLoop ? " Loop" : "");
            }

            bool selected = (*railIndex == i);
            if ( ImGui::Selectable(itemLabel, selected) ) {
                *railIndex = i;
                changed = true;
            }
            if ( selected ) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// ================================================================
//  距離入力ウィジェット：DragFloat（ドラッグで調整 + Ctrl+クリックで直接入力）
//  + 始点・中央・終点のクイックボタン
// ================================================================
static bool DrawDistanceWidget(const char* idSuffix, float* distance, float maxDist) {
    if ( maxDist < 0.01f ) maxDist = 0.01f;

    bool changed = false;
    char label[32];
    std::snprintf(label, sizeof(label), "距離(m)##%s", idSuffix);

    // DragFloat：ドラッグで滑らかに調整。Ctrl+クリックで数値直接入力も可能
    if ( ImGui::DragFloat(label, distance, 0.1f, 0.0f, maxDist, "%.1f") ) {
        changed = true;
    }
    *distance = std::clamp(*distance, 0.0f, maxDist);

    // パーセント表示（現在位置が全長のどの辺りかひと目で分かる）
    float pct = (maxDist > 0.01f) ? (*distance / maxDist * 100.0f) : 0.0f;
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f%%)", pct);

    // クイック配置ボタン
    char b0[16], b1[16], b2[16];
    std::snprintf(b0, sizeof(b0), "始点##%s", idSuffix);
    std::snprintf(b1, sizeof(b1), "中央##%s", idSuffix);
    std::snprintf(b2, sizeof(b2), "終点##%s", idSuffix);
    if ( ImGui::Button(b0) )  { *distance = 0.0f;          changed = true; }
    ImGui::SameLine();
    if ( ImGui::Button(b1) )  { *distance = maxDist * 0.5f; changed = true; }
    ImGui::SameLine();
    if ( ImGui::Button(b2) )  { *distance = maxDist;        changed = true; }

    return changed;
}

// ================================================================
//  メインの描画関数
// ================================================================
void EnemyEditor::DrawWindow(const std::vector<SplineRail>& splineRails) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(360.0f, 500.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("敵配置エディタ (Enemy Editor)");

    // ================================================================
    //  セクション1：新しい敵を追加する
    // ================================================================
    if ( ImGui::CollapsingHeader("新規配置", ImGuiTreeNodeFlags_DefaultOpen) ) {

        if ( splineRails.empty() ) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                "レールがありません。先にレールを作成してください。");
        } else {
            // --- 敵の種類を選択 ---
            const char* enemyTypeNames[] = { "Zako (雑魚敵)", "Strong (強敵)" };
            ImGui::Combo("種類##new", &spawnEnemyTypeIdx_, enemyTypeNames, IM_ARRAYSIZE(enemyTypeNames));

            // --- 配置先レール番号の選択（情報付きドロップダウン）---
            DrawRailCombo("レール##new", &spawnRailIndex_, splineRails, spawnDatas_);

            // --- レール上の距離 ---
            float maxDist = splineRails[spawnRailIndex_].GetLength();
            DrawDistanceWidget("new", &spawnDistance_, maxDist);

            ImGui::Spacing();

            // --- 配置ボタン ---
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.5f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.2f, 0.65f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.25f, 0.8f, 0.25f, 1.0f));
            if ( ImGui::Button("＋ 敵を追加", ImVec2(-1.0f, 28.0f)) ) {
                EnemyType type = (spawnEnemyTypeIdx_ == 0) ? EnemyType::Zako : EnemyType::Strong;
                spawnDatas_.push_back({ type, spawnRailIndex_, spawnDistance_ });
                changed_ = true;
            }
            ImGui::PopStyleColor(3);
        }
    }

    ImGui::Spacing();

    // ================================================================
    //  セクション2：配置済みエネミーの一覧と編集
    // ================================================================
    char listHeader[64];
    std::snprintf(listHeader, sizeof(listHeader),
        "配置済み一覧 (%d 体)###enemylist", static_cast<int>(spawnDatas_.size()));
    if ( ImGui::CollapsingHeader(listHeader, ImGuiTreeNodeFlags_DefaultOpen) ) {

        if ( spawnDatas_.empty() ) {
            ImGui::TextDisabled("  エネミーが配置されていません。");
        } else {
            ImGui::TextDisabled("  ドラッグで並び替え / クリックで編集");
        }

        // --- 全削除ボタン ---
        if ( spawnDatas_.size() >= 2 ) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.12f, 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.9f, 0.22f, 0.22f, 1.0f));
            if ( ImGui::Button("全削除##clear", ImVec2(-1.0f, 0.0f)) ) {
                spawnDatas_.clear();
                selectedEntry_ = -1;
                changed_ = true;
            }
            ImGui::PopStyleColor(3);
        }

        // --- 各エネミーの一覧表示 ---
        int deleteIdx = -1;

        for ( int i = 0; i < static_cast<int>(spawnDatas_.size()); ++i ) {
            auto& spawn = spawnDatas_[i];
            ImGui::PushID(i);

            float railLen = 0.0f;
            bool validRail = (spawn.railIndex >= 0
                && spawn.railIndex < static_cast<int>(splineRails.size()));
            if ( validRail ) {
                railLen = splineRails[spawn.railIndex].GetLength();
            }

            bool isSelected = (selectedEntry_ == i);

            // 背景色：選択中は青、奇数行はわずかに暗くして縞模様
            if ( isSelected ) {
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.18f, 0.35f, 0.65f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.22f, 0.42f, 0.75f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.28f, 0.50f, 0.85f, 0.9f));
            } else if ( i % 2 == 1 ) {
                ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.2f, 0.2f, 0.22f, 0.3f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  ImVec4(0.3f, 0.3f, 0.35f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,   ImVec4(0.35f, 0.35f, 0.4f, 0.6f));
            }

            // ラベル行
            char label[128];
            std::snprintf(label, sizeof(label), "%-6s  Rail %d   %.1f m",
                GetTypeLabel(spawn.type), spawn.railIndex, spawn.distance);

            if ( ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_AllowOverlap) ) {
                selectedEntry_ = isSelected ? -1 : i;
            }

            if ( isSelected || (i % 2 == 1) ) {
                ImGui::PopStyleColor(3);
            }

            // ドラッグ＆ドロップ：ドラッグソース
            if ( ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID) ) {
                ImGui::SetDragDropPayload("ENEMY_REORDER", &i, sizeof(int));
                dragSourceIdx_ = i;
                ImGui::Text("%s  Rail %d  %.1fm",
                    GetTypeLabel(spawn.type), spawn.railIndex, spawn.distance);
                ImGui::EndDragDropSource();
            }

            // ドラッグ＆ドロップ：ドロップターゲット
            if ( ImGui::BeginDragDropTarget() ) {
                if ( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENEMY_REORDER") ) {
                    int srcIdx = *static_cast<const int*>(payload->Data);
                    if ( srcIdx != i && srcIdx >= 0
                        && srcIdx < static_cast<int>(spawnDatas_.size()) ) {
                        EnemySpawnData moving = spawnDatas_[srcIdx];
                        spawnDatas_.erase(spawnDatas_.begin() + srcIdx);
                        int insertAt = (srcIdx < i) ? (i - 1) : i;
                        spawnDatas_.insert(spawnDatas_.begin() + insertAt, moving);
                        selectedEntry_ = insertAt;
                        changed_ = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // --- 選択中のエントリはインライン編集UI を展開 ---
            if ( selectedEntry_ == i ) {
                ImGui::Indent(16.0f);

                // 種類の変更
                int typeIdx = (spawn.type == EnemyType::Zako) ? 0 : 1;
                const char* typeNames[] = { "Zako (雑魚敵)", "Strong (強敵)" };
                if ( ImGui::Combo("種類##edit", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames)) ) {
                    spawn.type = (typeIdx == 0) ? EnemyType::Zako : EnemyType::Strong;
                    changed_ = true;
                }

                // レール番号の変更（情報付きドロップダウン）
                if ( !splineRails.empty() ) {
                    if ( DrawRailCombo("レール##edit", &spawn.railIndex, splineRails, spawnDatas_) ) {
                        // レールが変わったので距離を新レールの範囲に収める
                        float newLen = splineRails[spawn.railIndex].GetLength();
                        spawn.distance = std::clamp(spawn.distance, 0.0f, newLen);
                        changed_ = true;
                    }
                }

                // 距離の変更
                float editMax = validRail ? railLen : 100.0f;
                if ( DrawDistanceWidget("edit", &spawn.distance, editMax) ) {
                    changed_ = true;
                }

                ImGui::Spacing();

                // --- 操作ボタン行：複製と削除を横に並べる ---
                // 複製ボタン（この敵と同じ設定をコピーして末尾に追加）
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.35f, 0.55f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.25f, 0.45f, 0.7f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.3f, 0.55f, 0.85f, 1.0f));
                float halfW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if ( ImGui::Button("複製", ImVec2(halfW, 0.0f)) ) {
                    // 現在選択中の敵を複製して末尾に追加
                    spawnDatas_.push_back(spawn);
                    changed_ = true;
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();

                // 削除ボタン
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.5f, 0.12f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  ImVec4(0.7f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,   ImVec4(0.9f, 0.22f, 0.22f, 1.0f));
                if ( ImGui::Button("削除", ImVec2(halfW, 0.0f)) ) {
                    deleteIdx = i;
                }
                ImGui::PopStyleColor(3);

                ImGui::Unindent(16.0f);
                ImGui::Spacing();
            }

            ImGui::PopID();
        }

        // 削除はループ後にまとめて処理
        if ( deleteIdx >= 0 ) {
            spawnDatas_.erase(spawnDatas_.begin() + deleteIdx);
            selectedEntry_ = -1;
            changed_ = true;
        }
    }

    ImGui::End();
#endif
}
