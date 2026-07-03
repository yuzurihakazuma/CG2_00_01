#include "game/stage/StageFlow.h"

#include "game/rail/RailField.h"
#include "game/combat/HitFeel.h"
#include "engine/graphics/DebugDraw.h"
#include "engine/math/VectorMath.h"
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

using namespace VectorMath;

// ゴール判定＋ゴールマーカー（金色の環。動くレール上でも追従する）
void StageFlow::Update(const Vector3& playerPos, const RailField& rails, HitFeel& hitFeel){
    if ( !rails.HasGoal() ) return;

    Vector3 gp = rails.GetGoalPos();
    DebugDraw::GetInstance()->Sphere({ gp.x, gp.y + 0.8f, gp.z }, 0.8f, { 1.0f, 0.85f, 0.2f, 1.0f }, 16);

    if ( !goalReached_ ) {
        Vector3 d = playerPos - gp;
        if ( Length(d) < 1.2f ) {
            goalReached_ = true;
            hitFeel.Trigger(0.08f, 0.3f); // 到達の手応え
        }
    }
}

// ゴール到達表示（マップのゴール地点に触れた時だけ）
void StageFlow::DrawDebugUI(){
#ifdef USE_IMGUI
    if ( !goalReached_ ) return;
    ImGui::SetNextWindowPos(ImVec2(20.0f, 60.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##goal", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "★ ゴール！");
    ImGui::End();
#endif
}
