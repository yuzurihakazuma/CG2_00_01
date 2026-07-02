#include "NodeEditor.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif
#include "externals/nlohmann/json.hpp"
#include "engine/base/TimeManager.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/particle/GPUParticleEmitter.h"
#include "engine/utils/EditorManager.h"
#include "engine/utils/Level/LevelEditor.h"
#include "engine/graphics/PipelineManager.h"
#include "engine/3d/obj/Obj3d.h"

using json = nlohmann::json;

namespace {
    // ノード見た目の定数
    constexpr float kNodeWidth = 150.0f;
    constexpr float kTitleH    = 24.0f;
    constexpr float kRowH      = 24.0f;
    constexpr float kPinR      = 5.5f;

    float ClampF(float v, float lo, float hi) {
        return v < lo ? lo : ( v > hi ? hi : v );
    }
}

void NodeEditor::Initialize() {
    // 前回保存したグラフがあれば自動で復元する
    if ( std::filesystem::exists(fileBuf_) ) {
        Load(fileBuf_);
        status_ = std::string("前回のグラフを読み込みました: ") + fileBuf_;
        return;
    }

    // 初回はデモグラフ：サイン波 × 数値 → 出力（動く値が流れるのが見える）
    int vA  = AddNode(NodeType::SinTime,  40.0f,  60.0f);
    int vB  = AddNode(NodeType::Value,    40.0f, 200.0f);
    int mul = AddNode(NodeType::Multiply, 260.0f, 120.0f);
    int res = AddNode(NodeType::Result,   470.0f, 130.0f);
    for ( auto& n : nodes_ ) {
        if ( n.id == vB ) n.value = 5.0f;
        if ( n.id == vA ) { n.value = 2.0f; n.param = 1.0f; }
    }
    links_.push_back({ vA, mul, 0 });
    links_.push_back({ vB, mul, 1 });
    links_.push_back({ mul, res, 0 });
}

// 毎フレーム：適用ノードの値をゲームに反映する（入力が接続されているものだけ）
void NodeEditor::Update() {
    if ( !runGraph_ ) return;

    for ( const Node& n : nodes_ ) {
        if ( !IsApplyNode(n.type) ) continue;
        if ( !HasInputLink(n.id, 0) ) continue; // 未接続なら何もしない（誤作動防止）

        float v = EvaluateInput(n.id, 0, 0);
        switch ( n.type ) {
        case NodeType::LightIntensity: {
            auto* lightData = Obj3dCommon::GetInstance()->GetLightData();
            if ( lightData ) { lightData->lights[0].intensity = ClampF(v, 0.0f, 10.0f); }
            break;
        }
        case NodeType::ParticleRate:
            if ( emitter_ ) { emitter_->GetData().emitRate = ClampF(v, 0.0f, 200.0f); }
            break;
        case NodeType::ParticleGravity:
            if ( emitter_ ) { emitter_->GetData().gravityY = ClampF(v, -10.0f, 10.0f); }
            break;
        case NodeType::TimeScale:
            Time::GetInstance()->SetTimeScale(ClampF(v, 0.0f, 2.0f));
            break;
        case NodeType::ObjPosY: {
            LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
            if ( le ) { le->SetObjectPosY(n.target, ClampF(v, -100.0f, 100.0f)); }
            break;
        }
        case NodeType::ObjRotY: {
            LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
            if ( le ) { le->SetObjectRotY(n.target, v); }
            break;
        }
        case NodeType::ObjScale: {
            LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
            if ( le ) { le->SetObjectScale(n.target, ClampF(v, 0.05f, 20.0f)); }
            break;
        }
        case NodeType::ObjParam: {
            LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
            if ( le ) { le->SetObjectShaderParam(n.target, ClampF(v, 0.0f, 1.0f)); }
            break;
        }
        case NodeType::GameParam: {
            // シーンが登録したゲーム値（プレイヤー速度・卵の投げ初速など）に反映
            if ( n.target >= 0 && n.target < ( int ) gameValues_.size() ) {
                const GameValue& gv = gameValues_[n.target];
                if ( gv.target ) { *gv.target = ClampF(v, gv.minV, gv.maxV); }
            }
            break;
        }
        default: break;
        }
    }
}

int NodeEditor::AddNode(NodeType type, float x, float y) {
    Node n;
    n.id = nextId_++;
    n.type = type;
    n.posX = x; n.posY = y;
    if ( type == NodeType::Value )   n.value = 1.0f;
    if ( type == NodeType::SinTime ) { n.value = 1.0f; n.param = 1.0f; }
    nodes_.push_back(n);
    return n.id;
}

void NodeEditor::RemoveNode(int nodeId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(),
        [nodeId](const Link& l) { return l.fromNode == nodeId || l.toNode == nodeId; }),
        links_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(),
        [nodeId](const Node& n) { return n.id == nodeId; }),
        nodes_.end());
}

void NodeEditor::RemoveLinksTo(int nodeId, int slot) {
    links_.erase(std::remove_if(links_.begin(), links_.end(),
        [&](const Link& l) { return l.toNode == nodeId && l.toSlot == slot; }),
        links_.end());
}

bool NodeEditor::HasInputLink(int nodeId, int slot) const {
    for ( const Link& l : links_ ) {
        if ( l.toNode == nodeId && l.toSlot == slot ) return true;
    }
    return false;
}

const char* NodeEditor::TypeName(NodeType t) const {
    switch ( t ) {
    case NodeType::Value:           return "数値";
    case NodeType::Time:            return "時間";
    case NodeType::SinTime:         return "サイン波";
    case NodeType::Add:             return "加算 (A+B)";
    case NodeType::Multiply:        return "乗算 (A×B)";
    case NodeType::Result:          return "出力";
    case NodeType::LightIntensity:  return "→ ライト強度";
    case NodeType::ParticleRate:    return "→ 発生レート";
    case NodeType::ParticleGravity: return "→ 重力";
    case NodeType::TimeScale:       return "→ タイムスケール";
    case NodeType::ObjPosY:         return "→ 位置Y";
    case NodeType::ObjRotY:         return "→ 回転Y";
    case NodeType::ObjScale:        return "→ 大きさ";
    case NodeType::ObjParam:        return "→ シェーダーパラメータ";
    case NodeType::ShaderTexture:   return "テクスチャ色";
    case NodeType::ShaderUV:        return "UV座標";
    case NodeType::ShaderColor:     return "定数色";
    case NodeType::ShaderParam:     return "パラメータ";
    case NodeType::ShaderMul:       return "色乗算 (A×B)";
    case NodeType::ShaderAdd:       return "色加算 (A+B)";
    case NodeType::ShaderMix:       return "ミックス";
    case NodeType::ShaderGray:      return "グレースケール";
    case NodeType::ShaderInvert:    return "色反転";
    case NodeType::ShaderOutput:    return "最終色 (シェーダー)";
    case NodeType::GameParam:       return "→ ゲーム値";
    }
    return "?";
}

const char* NodeEditor::TypeDesc(NodeType t) const {
    switch ( t ) {
    case NodeType::Value:           return "固定の数値を出す";
    case NodeType::Time:            return "起動からの経過時間（秒）を出す";
    case NodeType::SinTime:         return "sin波で揺れる値を出す（速さ×振幅）";
    case NodeType::Add:             return "AとBを足す";
    case NodeType::Multiply:        return "AとBを掛ける";
    case NodeType::Result:          return "値を表示する（確認用）";
    case NodeType::LightIntensity:  return "繋いだ値を平行光源の強度に反映（0〜10）";
    case NodeType::ParticleRate:    return "繋いだ値をパーティクル発生レートに反映（0〜200）";
    case NodeType::ParticleGravity: return "繋いだ値をパーティクル重力Yに反映（-10〜10）";
    case NodeType::TimeScale:       return "繋いだ値をタイムスケールに反映（0〜2）";
    case NodeType::ObjPosY:         return "選んだ配置オブジェクトのY座標に反映（上下に動かせる）";
    case NodeType::ObjRotY:         return "選んだ配置オブジェクトのY回転に反映（回せる）";
    case NodeType::ObjScale:        return "選んだ配置オブジェクトの大きさに反映";
    case NodeType::ObjParam:        return "シェーダーの『パラメータ』ノードを外から動かす（0〜1）";
    case NodeType::ShaderTexture:   return "モデルのテクスチャの色";
    case NodeType::ShaderUV:        return "UV座標を色として出す（デバッグ用）";
    case NodeType::ShaderColor:     return "好きな色（RGBA）";
    case NodeType::ShaderParam:     return "「→ シェーダーパラメータ」で外から動かせる値（0〜1）";
    case NodeType::ShaderMul:       return "色を掛け合わせる（ティント等）";
    case NodeType::ShaderAdd:       return "色を足す（明るくする）";
    case NodeType::ShaderMix:       return "AとBをTで混ぜる（T=0でA、T=1でB）";
    case NodeType::ShaderGray:      return "白黒にする";
    case NodeType::ShaderInvert:    return "色を反転する（ネガ）";
    case NodeType::ShaderOutput:    return "ここに繋いだ色でHLSLを自動生成し、選んだオブジェクトに適用";
    case NodeType::GameParam:       return "繋いだ値をゲームの変数に反映（プレイヤー速度・卵の投げ初速など。ノード内で対象を選ぶ）";
    }
    return "";
}

int NodeEditor::InputCount(NodeType t) const {
    switch ( t ) {
    case NodeType::ShaderMix: return 3;
    case NodeType::Add:
    case NodeType::Multiply:
    case NodeType::ShaderMul:
    case NodeType::ShaderAdd: return 2;
    case NodeType::Result:
    case NodeType::LightIntensity:
    case NodeType::ParticleRate:
    case NodeType::ParticleGravity:
    case NodeType::TimeScale:
    case NodeType::ObjPosY:
    case NodeType::ObjRotY:
    case NodeType::ObjScale:
    case NodeType::ObjParam:
    case NodeType::GameParam:
    case NodeType::ShaderGray:
    case NodeType::ShaderInvert:
    case NodeType::ShaderOutput: return 1;
    default: return 0;
    }
}

bool NodeEditor::HasOutput(NodeType t) const {
    return !IsApplyNode(t) && t != NodeType::Result && t != NodeType::ShaderOutput;
}

bool NodeEditor::IsApplyNode(NodeType t) const {
    return t == NodeType::LightIntensity || t == NodeType::ParticleRate
        || t == NodeType::ParticleGravity || t == NodeType::TimeScale
        || t == NodeType::GameParam
        || IsObjApplyNode(t);
}

bool NodeEditor::IsObjApplyNode(NodeType t) const {
    return t == NodeType::ObjPosY || t == NodeType::ObjRotY
        || t == NodeType::ObjScale || t == NodeType::ObjParam;
}

bool NodeEditor::IsShaderNode(NodeType t) const {
    return t == NodeType::ShaderTexture || t == NodeType::ShaderUV
        || t == NodeType::ShaderColor || t == NodeType::ShaderParam
        || t == NodeType::ShaderMul || t == NodeType::ShaderAdd
        || t == NodeType::ShaderMix || t == NodeType::ShaderGray
        || t == NodeType::ShaderInvert || t == NodeType::ShaderOutput;
}

// 入力スロットへ繋がっている値を評価（未接続なら inConst の値＝直接入力した数値）
float NodeEditor::EvaluateInput(int nodeId, int slot, int depth) const {
    for ( const Link& l : links_ ) {
        if ( l.toNode == nodeId && l.toSlot == slot ) {
            return Evaluate(l.fromNode, depth + 1);
        }
    }
    for ( const Node& n : nodes_ ) {
        if ( n.id == nodeId ) { return ( slot >= 0 && slot < 2 ) ? n.inConst[slot] : 0.0f; }
    }
    return 0.0f;
}

float NodeEditor::Evaluate(int nodeId, int depth) const {
    if ( depth > 32 ) return 0.0f; // 循環参照ガード
    const Node* node = nullptr;
    for ( const Node& n : nodes_ ) { if ( n.id == nodeId ) { node = &n; break; } }
    if ( !node ) return 0.0f;

    switch ( node->type ) {
    case NodeType::Value:    return node->value;
    case NodeType::Time:     return Time::GetInstance()->GetTotalTime();
    case NodeType::SinTime:  return std::sin(Time::GetInstance()->GetTotalTime() * node->value) * node->param;
    case NodeType::Add:      return EvaluateInput(nodeId, 0, depth) + EvaluateInput(nodeId, 1, depth);
    case NodeType::Multiply: return EvaluateInput(nodeId, 0, depth) * EvaluateInput(nodeId, 1, depth);
    default:                 return EvaluateInput(nodeId, 0, depth); // 出力・適用ノードは入力そのまま
    }
}

void NodeEditor::Save(const std::string& path) const {
    json j;
    j["nodes"] = json::array();
    for ( const Node& n : nodes_ ) {
        j["nodes"].push_back({
            { "id", n.id }, { "type", ( int ) n.type },
            { "x", n.posX }, { "y", n.posY },
            { "value", n.value }, { "param", n.param },
            { "target", n.target },
            { "in0", n.inConst[0] }, { "in1", n.inConst[1] }, { "in2", n.inConst[2] },
            { "cr", n.color[0] }, { "cg", n.color[1] }, { "cb", n.color[2] }, { "ca", n.color[3] },
        });
    }
    j["links"] = json::array();
    for ( const Link& l : links_ ) {
        j["links"].push_back({ { "from", l.fromNode }, { "to", l.toNode }, { "slot", l.toSlot } });
    }

    std::filesystem::path p(path);
    if ( p.has_parent_path() ) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream f(path);
    if ( f ) { f << j.dump(4); }
}

void NodeEditor::Load(const std::string& path) {
    std::ifstream f(path);
    if ( !f ) return;
    json j;
    f >> j;

    nodes_.clear();
    links_.clear();
    nextId_ = 1;
    for ( auto& jn : j.value("nodes", json::array()) ) {
        Node n;
        n.id    = jn.value("id", 0);
        n.type  = ( NodeType ) jn.value("type", 0);
        n.posX  = jn.value("x", 0.0f);
        n.posY  = jn.value("y", 0.0f);
        n.value = jn.value("value", 0.0f);
        n.param = jn.value("param", 1.0f);
        n.target = jn.value("target", 0);
        n.inConst[0] = jn.value("in0", 0.0f);
        n.inConst[1] = jn.value("in1", 0.0f);
        n.inConst[2] = jn.value("in2", 0.0f);
        n.color[0] = jn.value("cr", 1.0f);
        n.color[1] = jn.value("cg", 1.0f);
        n.color[2] = jn.value("cb", 1.0f);
        n.color[3] = jn.value("ca", 1.0f);
        nodes_.push_back(n);
        nextId_ = ( std::max )( nextId_, n.id + 1 );
    }
    for ( auto& jl : j.value("links", json::array()) ) {
        links_.push_back({ jl.value("from", 0), jl.value("to", 0), jl.value("slot", 0) });
    }
}

// ============================================================
// シェーダー生成（ノードグラフ → HLSL式）
// ============================================================

// 入力スロットのHLSL式（未接続なら inConst の値を灰色として使う）
std::string NodeEditor::GenShaderInput(int nodeId, int slot, int depth) const {
    for ( const Link& l : links_ ) {
        if ( l.toNode == nodeId && l.toSlot == slot ) {
            return GenShaderExpr(l.fromNode, depth + 1);
        }
    }
    // 未接続：直接入力した数値を float4(c,c,c,1) として使う
    float c = 0.0f;
    for ( const Node& n : nodes_ ) {
        if ( n.id == nodeId ) { c = ( slot >= 0 && slot < 3 ) ? n.inConst[slot] : 0.0f; break; }
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "float4(%.4ff, %.4ff, %.4ff, 1.0f)", c, c, c);
    return buf;
}

// ノード1個分のHLSL式を再帰生成する
std::string NodeEditor::GenShaderExpr(int nodeId, int depth) const {
    if ( depth > 16 ) return "float4(1.0f, 0.0f, 1.0f, 1.0f)"; // 循環はマゼンタ
    const Node* node = nullptr;
    for ( const Node& n : nodes_ ) { if ( n.id == nodeId ) { node = &n; break; } }
    if ( !node ) return "float4(0.0f, 0.0f, 0.0f, 1.0f)";

    char buf[160];
    switch ( node->type ) {
    case NodeType::ShaderTexture:
        return "texColor";
    case NodeType::ShaderUV:
        return "float4(input.texcoord.x, input.texcoord.y, 0.0f, 1.0f)";
    case NodeType::ShaderColor:
        snprintf(buf, sizeof(buf), "float4(%.4ff, %.4ff, %.4ff, %.4ff)",
            node->color[0], node->color[1], node->color[2], node->color[3]);
        return buf;
    case NodeType::ShaderParam:
        return "float4(gDissolve.threshold, gDissolve.threshold, gDissolve.threshold, 1.0f)";
    case NodeType::ShaderMul:
        return "(" + GenShaderInput(nodeId, 0, depth) + " * " + GenShaderInput(nodeId, 1, depth) + ")";
    case NodeType::ShaderAdd:
        return "(" + GenShaderInput(nodeId, 0, depth) + " + " + GenShaderInput(nodeId, 1, depth) + ")";
    case NodeType::ShaderMix:
        return "lerp(" + GenShaderInput(nodeId, 0, depth) + ", "
                       + GenShaderInput(nodeId, 1, depth) + ", ("
                       + GenShaderInput(nodeId, 2, depth) + ").x)";
    case NodeType::ShaderGray: {
        std::string a = GenShaderInput(nodeId, 0, depth);
        return "float4(dot((" + a + ").rgb, float3(0.299f, 0.587f, 0.114f)).xxx, (" + a + ").a)";
    }
    case NodeType::ShaderInvert: {
        std::string a = GenShaderInput(nodeId, 0, depth);
        return "float4(float3(1.0f, 1.0f, 1.0f) - (" + a + ").rgb, (" + a + ").a)";
    }
    default:
        // シェーダー式にCPUノードが混ざった場合は白（ドメイン違い）
        return "float4(1.0f, 1.0f, 1.0f, 1.0f)";
    }
}

// 「最終色」ノードの適用：HLSL生成 → ファイル出力 → コンパイル → PSO → 対象オブジェクトへ
void NodeEditor::ApplyShaderToTarget(const Node& outputNode) {
    // 1. 式の生成（未接続ならテクスチャそのまま）
    std::string expr = HasInputLink(outputNode.id, 0)
        ? GenShaderInput(outputNode.id, 0, 0)
        : "texColor";

    // 2. Object3D 互換のピクセルシェーダーを組み立てる
    //    （バインディングは Object3d.PS.hlsl と同じ＝ルートシグネチャ互換）
    std::string src;
    src += "// ノードエディタが自動生成したシェーダー（手で編集しても次の適用で上書きされます）\n";
    src += "#include \"../Object3d/Object3d.hlsli\"\n\n";
    src += "ConstantBuffer<Material> gMaterial : register(b0);\n";
    src += "Texture2D<float4> gTexture : register(t0);\n";
    src += "SamplerState gSampler : register(s0);\n";
    src += "ConstantBuffer<DirectionalLightData> gDirectionalLightData : register(b1);\n";
    src += "ConstantBuffer<Camera> gCamera : register(b2);\n";
    src += "ConstantBuffer<PointLight> gPointLight : register(b3);\n";
    src += "ConstantBuffer<SpotLight> gSpotLight : register(b4);\n";
    src += "Texture2D<float4> gNoiseTexture : register(t1);\n";
    src += "ConstantBuffer<DissolveData> gDissolve : register(b5);\n";
    src += "TextureCube<float32_t4> gEnvironmentTexture : register(t3);\n\n";
    src += "struct PixelShaderOutput {\n";
    src += "    float4 color : SV_TARGET0;\n";
    src += "    float4 mask : SV_TARGET1;\n";
    src += "};\n\n";
    src += "PixelShaderOutput main(VertexShaderOutput input)\n";
    src += "{\n";
    src += "    PixelShaderOutput output;\n";
    src += "    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);\n";
    src += "    float4 texColor = gTexture.Sample(gSampler, transformedUV.xy);\n";
    src += "    output.color = " + expr + ";\n";
    src += "    output.mask = float4(0.0f, 0.0f, 0.0f, 0.0f);\n";
    src += "    return output;\n";
    src += "}\n";

    // 3. ファイルへ書き出し（ノードIDごとに別ファイル＝複数マテリアル共存可）
    std::filesystem::create_directories("resources/shaders/Generated");
    std::string path = "resources/shaders/Generated/NodeGraph_" + std::to_string(outputNode.id) + ".PS.hlsl";
    {
        std::ofstream f(path);
        if ( !f ) { status_ = "シェーダーファイルの書き出しに失敗しました"; return; }
        f << src;
    }

    // 4. コンパイル＆PSO生成（CPU/デバイス処理のみ＝UI中でも安全）
    std::wstring wpath(path.begin(), path.end());
    auto pso = PipelineManager::GetInstance()->CreateCustomObject3DPipeline(wpath);
    if ( !pso ) { status_ = "シェーダーのコンパイルに失敗しました: " + path; return; }

    // 5. 対象オブジェクトに適用
    LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
    Obj3d* obj = le ? le->GetObject3d(outputNode.target) : nullptr;
    if ( !obj ) { status_ = "対象オブジェクトが見つかりません（配置してから適用してください）"; return; }
    obj->SetCustomPipeline(pso);
    status_ = "シェーダーを適用しました → " + ( le ? le->GetObjectLabel(outputNode.target) : "" )
        + "  (" + path + ")";
}

void NodeEditor::DrawDebugUI(bool* openFlag) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowSize(ImVec2(860, 520), ImGuiCond_FirstUseEver);
    if ( !ImGui::Begin("ノードエディタ", openFlag) ) { ImGui::End(); return; }

    // --- ヘッダー：保存/読込/実行 ---
    ImGui::SetNextItemWidth(260.0f);
    ImGui::InputText("##nodefile", fileBuf_, sizeof(fileBuf_));
    ImGui::SameLine();
    if ( ImGui::Button("保存") ) { Save(fileBuf_); status_ = std::string("保存しました: ") + fileBuf_; }
    ImGui::SameLine();
    if ( ImGui::Button("読み込み") ) { Load(fileBuf_); status_ = std::string("読み込みました: ") + fileBuf_; }
    ImGui::SameLine();
    ImGui::Checkbox("グラフを実行", &runGraph_);
    ImGui::SameLine();
    if ( ImGui::Button("視点リセット") ) { scrollX_ = 0.0f; scrollY_ = 0.0f; }
    ImGui::SameLine();
    ImGui::TextDisabled("ノード %d 個 / 接続 %d 本", ( int ) nodes_.size(), ( int ) links_.size());
    // Ctrl+S で保存（テキスト入力中は無効）
    if ( !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) ) {
        Save(fileBuf_);
        status_ = std::string("保存しました: ") + fileBuf_;
    }
    if ( !status_.empty() ) { ImGui::TextDisabled("%s", status_.c_str()); }

    // =========================================================
    // 左：ノードパレット（クリックで追加）
    // =========================================================
    ImGui::BeginChild("palette", ImVec2(170.0f, 0.0f), true);
    ImGui::TextDisabled("クリックで追加");
    ImGui::Separator();

    struct PaletteEntry { NodeType type; ImVec4 col; };
    const PaletteEntry inputs[] = {
        { NodeType::Value,   ImVec4(0.27f, 0.47f, 0.78f, 1.0f) },
        { NodeType::Time,    ImVec4(0.35f, 0.60f, 0.85f, 1.0f) },
        { NodeType::SinTime, ImVec4(0.67f, 0.43f, 0.78f, 1.0f) },
    };
    const PaletteEntry maths[] = {
        { NodeType::Add,      ImVec4(0.27f, 0.67f, 0.35f, 1.0f) },
        { NodeType::Multiply, ImVec4(0.24f, 0.63f, 0.59f, 1.0f) },
    };
    const PaletteEntry outs[] = {
        { NodeType::Result,          ImVec4(0.78f, 0.47f, 0.24f, 1.0f) },
        { NodeType::LightIntensity,  ImVec4(0.85f, 0.30f, 0.30f, 1.0f) },
        { NodeType::ParticleRate,    ImVec4(0.85f, 0.30f, 0.30f, 1.0f) },
        { NodeType::ParticleGravity, ImVec4(0.85f, 0.30f, 0.30f, 1.0f) },
        { NodeType::TimeScale,       ImVec4(0.85f, 0.30f, 0.30f, 1.0f) },
    };
    const PaletteEntry objs[] = {
        { NodeType::ObjPosY,  ImVec4(0.80f, 0.40f, 0.65f, 1.0f) },
        { NodeType::ObjRotY,  ImVec4(0.80f, 0.40f, 0.65f, 1.0f) },
        { NodeType::ObjScale, ImVec4(0.80f, 0.40f, 0.65f, 1.0f) },
        { NodeType::ObjParam, ImVec4(0.80f, 0.40f, 0.65f, 1.0f) },
    };
    const PaletteEntry games[] = {
        { NodeType::GameParam, ImVec4(0.90f, 0.60f, 0.20f, 1.0f) },
    };
    const PaletteEntry shaders[] = {
        { NodeType::ShaderTexture, ImVec4(0.20f, 0.55f, 0.60f, 1.0f) },
        { NodeType::ShaderUV,      ImVec4(0.20f, 0.55f, 0.60f, 1.0f) },
        { NodeType::ShaderColor,   ImVec4(0.20f, 0.55f, 0.60f, 1.0f) },
        { NodeType::ShaderParam,   ImVec4(0.20f, 0.55f, 0.60f, 1.0f) },
        { NodeType::ShaderMul,     ImVec4(0.25f, 0.62f, 0.55f, 1.0f) },
        { NodeType::ShaderAdd,     ImVec4(0.25f, 0.62f, 0.55f, 1.0f) },
        { NodeType::ShaderMix,     ImVec4(0.25f, 0.62f, 0.55f, 1.0f) },
        { NodeType::ShaderGray,    ImVec4(0.25f, 0.62f, 0.55f, 1.0f) },
        { NodeType::ShaderInvert,  ImVec4(0.25f, 0.62f, 0.55f, 1.0f) },
        { NodeType::ShaderOutput,  ImVec4(0.90f, 0.55f, 0.15f, 1.0f) },
    };

    // 追加位置：見えている範囲の中で階段状にずらす（重なり防止）
    auto addAtView = [&](NodeType t) {
        int k = ( int ) nodes_.size();
        AddNode(t, -scrollX_ + 80.0f + ( float ) ( k % 5 ) * 40.0f,
                   -scrollY_ + 60.0f + ( float ) ( k % 5 ) * 40.0f);
    };
    auto paletteButtons = [&](const PaletteEntry* list, int count) {
        for ( int i = 0; i < count; ++i ) {
            ImGui::PushStyleColor(ImGuiCol_Button, list[i].col);
            if ( ImGui::Button(TypeName(list[i].type), ImVec2(-FLT_MIN, 0.0f)) ) {
                addAtView(list[i].type);
            }
            ImGui::PopStyleColor();
            if ( ImGui::IsItemHovered() ) { ImGui::SetTooltip("%s", TypeDesc(list[i].type)); }
        }
    };

    ImGui::Text("入力（値を作る）");
    paletteButtons(inputs, 3);
    ImGui::Spacing();
    ImGui::Text("計算");
    paletteButtons(maths, 2);
    ImGui::Spacing();
    ImGui::Text("出力・ゲームに適用");
    paletteButtons(outs, 5);
    ImGui::Spacing();
    ImGui::Text("配置オブジェクトに適用");
    paletteButtons(objs, 4);
    ImGui::Spacing();
    ImGui::Text("ゲームの値に適用");
    paletteButtons(games, 1);
    ImGui::Spacing();
    ImGui::Text("シェーダー（マテリアル）");
    paletteButtons(shaders, 10);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextWrapped("使い方:");
    ImGui::TextWrapped("・ノードはタイトルを掴んで移動");
    ImGui::TextWrapped("・出力ピンを長押し→引っ張って接続（クリック→クリックでも可）");
    ImGui::TextWrapped("・ノード本体に落としてもOK（空きピンに繋がる）");
    ImGui::TextWrapped("・Esc / 右クリック=接続キャンセル");
    ImGui::TextWrapped("・入力ピン右クリック=切断");
    ImGui::TextWrapped("・未接続のA/Bは数値を直接入力できる");
    ImGui::TextWrapped("・クリックで選択 → Delete=削除 / Ctrl+D=複製");
    ImGui::TextWrapped("・空きを左ドラッグ=画面移動 / ホイール=スクロール");
    ImGui::TextWrapped("・ウィンドウはゲームの外にドラッグで出せる！");
    ImGui::EndChild();

    ImGui::SameLine();

    // =========================================================
    // 右：キャンバス
    // =========================================================
    ImGui::BeginChild("canvas", ImVec2(0, 0), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 origin(canvasPos.x + scrollX_, canvasPos.y + scrollY_); // キャンバス原点(画面座標)

    // ※背景に InvisibleButton を敷くと ImGui の仕様（先に出したアイテムが当たりを独占）で
    //   ノードが一切操作できなくなるため、背景アイテムは置かない。
    //   パン/右クリックメニューは「何のアイテムにも触れていない時」だけ末尾で処理する。

    // ホイールで縦スクロール（Shift+ホイールで横）
    if ( ImGui::IsWindowHovered() ) {
        float wheel = ImGui::GetIO().MouseWheel;
        if ( wheel != 0.0f ) {
            if ( ImGui::GetIO().KeyShift ) scrollX_ += wheel * 40.0f;
            else                           scrollY_ += wheel * 40.0f;
        }
    }

    // グリッド背景
    const float grid = 32.0f;
    ImU32 gridCol = IM_COL32(120, 120, 130, 28);
    for ( float x = std::fmod(scrollX_, grid); x < canvasSize.x; x += grid ) {
        dl->AddLine(ImVec2(canvasPos.x + x, canvasPos.y), ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y), gridCol);
    }
    for ( float y = std::fmod(scrollY_, grid); y < canvasSize.y; y += grid ) {
        dl->AddLine(ImVec2(canvasPos.x, canvasPos.y + y), ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y), gridCol);
    }

    // --- ピン位置計算のラムダ ---
    auto nodeHeight = [&](const Node& n) {
        int rows = ( std::max )( InputCount(n.type), 1 );
        float extra = 0.0f;
        if ( n.type == NodeType::Value )   extra = kRowH;
        if ( n.type == NodeType::SinTime ) extra = kRowH * 2.0f;
        if ( n.type == NodeType::Result || IsApplyNode(n.type) ) extra = kRowH;
        if ( IsObjApplyNode(n.type) ) extra = kRowH * 2.0f;            // ターゲット選択コンボの分
        if ( n.type == NodeType::ShaderColor )  extra = kRowH;         // カラーピッカーの分
        if ( n.type == NodeType::ShaderOutput ) extra = kRowH * 2.0f;  // コンボ＋適用ボタンの分
        return kTitleH + rows * kRowH + extra + 8.0f;
    };
    auto inPinPos = [&](const Node& n, int slot) {
        return ImVec2(origin.x + n.posX, origin.y + n.posY + kTitleH + kRowH * ( slot + 0.5f ));
    };
    auto outPinPos = [&](const Node& n) {
        return ImVec2(origin.x + n.posX + kNodeWidth, origin.y + n.posY + kTitleH + kRowH * 0.5f);
    };

    // --- 接続線の描画 ---
    for ( const Link& l : links_ ) {
        const Node* from = nullptr; const Node* to = nullptr;
        for ( const Node& n : nodes_ ) {
            if ( n.id == l.fromNode ) from = &n;
            if ( n.id == l.toNode )   to = &n;
        }
        if ( !from || !to ) continue;
        ImVec2 a = outPinPos(*from);
        ImVec2 b = inPinPos(*to, l.toSlot);
        dl->AddBezierCubic(a, ImVec2(a.x + 50, a.y), ImVec2(b.x - 50, b.y), b,
            IM_COL32(220, 220, 120, 220), 2.5f);
    }

    // --- ノード描画 ---
    int deleteNodeId = -1;
    int raiseNodeId = -1; // クリックしたノードを最前面へ
    for ( Node& n : nodes_ ) {
        ImGui::PushID(n.id);
        float h = nodeHeight(n);
        ImVec2 p0(origin.x + n.posX, origin.y + n.posY);
        ImVec2 p1(p0.x + kNodeWidth, p0.y + h);

        // 種別ごとのタイトル色
        ImU32 titleCol =
            n.type == NodeType::Value        ? IM_COL32(70, 120, 200, 255) :
            n.type == NodeType::Time         ? IM_COL32(90, 150, 220, 255) :
            n.type == NodeType::SinTime      ? IM_COL32(170, 110, 200, 255) :
            n.type == NodeType::Add          ? IM_COL32(70, 170, 90, 255) :
            n.type == NodeType::Multiply     ? IM_COL32(60, 160, 150, 255) :
            n.type == NodeType::Result       ? IM_COL32(200, 120, 60, 255) :
            n.type == NodeType::ShaderOutput ? IM_COL32(230, 140, 40, 255) : // シェーダー出力はオレンジ
            IsShaderNode(n.type)             ? IM_COL32(50, 140, 155, 255) : // シェーダー系は青緑
                                               IM_COL32(215, 75, 75, 255);   // 適用ノードは赤

        // 本体（先に敷いてドラッグ移動＆右クリック削除を受ける）
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton("node", ImVec2(kNodeWidth, h));
        if ( ImGui::IsItemActivated() ) {
            raiseNodeId = n.id;      // 触ったら最前面へ
            selectedNodeId_ = n.id;  // 選択（Delete=削除 / Ctrl+D=複製）
        }
        if ( ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            n.posX += d.x; n.posY += d.y;
        }

        // 接続中にノード本体へ落とす/クリック → 最初の空き入力へ接続（狙いが甘くてもOK）
        if ( linkingFromNode_ >= 0 && linkingFromNode_ != n.id
            && InputCount(n.type) > 0 && ImGui::IsItemHovered() ) {
            // ピンの真上ならピン側の処理に任せる
            ImVec2 mp = ImGui::GetIO().MousePos;
            bool overPin = false;
            for ( int s = 0; s < InputCount(n.type); ++s ) {
                ImVec2 pp = inPinPos(n, s);
                if ( std::fabs(mp.x - pp.x) <= 12.0f && std::fabs(mp.y - pp.y) <= 12.0f ) { overPin = true; break; }
            }
            bool doConnect = !overPin &&
                ( ( !linkingSticky_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) )
                || ( linkingSticky_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) ) );
            if ( doConnect ) {
                int slot = 0;
                for ( int s = 0; s < InputCount(n.type); ++s ) {
                    if ( !HasInputLink(n.id, s) ) { slot = s; break; }
                }
                RemoveLinksTo(n.id, slot);
                links_.push_back({ linkingFromNode_, n.id, slot });
                linkingFromNode_ = -1;
            }
        }
        if ( ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) ) {
            ImGui::OpenPopup("node_menu");
        }
        if ( ImGui::IsItemHovered() ) { ImGui::SetTooltip("%s", TypeDesc(n.type)); }
        if ( ImGui::BeginPopup("node_menu") ) {
            ImGui::TextDisabled("%s", TypeName(n.type));
            if ( ImGui::MenuItem("このノードを削除") ) { deleteNodeId = n.id; }
            ImGui::EndPopup();
        }

        // 背景＋タイトル（選択中は明るい枠で強調）
        dl->AddRectFilled(p0, p1, IM_COL32(45, 47, 55, 240), 6.0f);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + kTitleH), titleCol, 6.0f, ImDrawFlags_RoundCornersTop);
        if ( n.id == selectedNodeId_ ) {
            dl->AddRect(p0, p1, IM_COL32(255, 220, 90, 255), 6.0f, 0, 2.5f);
        } else {
            dl->AddRect(p0, p1, IM_COL32(0, 0, 0, 160), 6.0f);
        }
        dl->AddText(ImVec2(p0.x + 8, p0.y + 4), IM_COL32(255, 255, 255, 255), TypeName(n.type));

        // 入力ピン（当たり判定は広め＝掴みやすく）
        for ( int s = 0; s < InputCount(n.type); ++s ) {
            ImVec2 pp = inPinPos(n, s);
            ImGui::SetCursorScreenPos(ImVec2(pp.x - 12, pp.y - 12));
            ImGui::InvisibleButton(( "in" + std::to_string(s) ).c_str(), ImVec2(24, 24));
            bool hov = ImGui::IsItemHovered();
            // ドラッグして離す or クリック接続モードでクリック → 接続
            bool doConnect = hov && linkingFromNode_ >= 0 &&
                ( ( !linkingSticky_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) )
                || ( linkingSticky_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) ) );
            if ( doConnect ) {
                if ( linkingFromNode_ != n.id ) {
                    RemoveLinksTo(n.id, s); // 入力は1本まで（差し替え）
                    links_.push_back({ linkingFromNode_, n.id, s });
                }
                linkingFromNode_ = -1;
            }
            // 右クリックで切断
            if ( hov && ImGui::IsMouseReleased(ImGuiMouseButton_Right) ) { RemoveLinksTo(n.id, s); }
            // 接続中は全入力ピンを緑に光らせて「ここに繋げる」と分かるようにする
            ImU32 pinCol = ( linkingFromNode_ >= 0 && linkingFromNode_ != n.id )
                ? IM_COL32(120, 255, 140, 255)
                : ( hov ? IM_COL32(255, 255, 160, 255) : IM_COL32(200, 200, 210, 255) );
            dl->AddCircleFilled(pp, hov ? kPinR + 2.0f : kPinR, pinCol);

            // 演算系（加算/乗算/シェーダー演算）の未接続入力には数値を直接入力できる欄を出す
            bool isMath = ( n.type == NodeType::Add || n.type == NodeType::Multiply
                || n.type == NodeType::ShaderMul || n.type == NodeType::ShaderAdd
                || n.type == NodeType::ShaderMix );
            if ( isMath && !HasInputLink(n.id, s) && s < 3 ) {
                ImGui::SetCursorScreenPos(ImVec2(pp.x + 12.0f, pp.y - 10.0f));
                ImGui::SetNextItemWidth(kNodeWidth - 36.0f);
                ImGui::PushID(s);
                ImGui::DragFloat("##inconst", &n.inConst[s], 0.05f);
                ImGui::PopID();
            } else {
                static const char* kLbl2[] = { "A", "B", "T" };
                const char* lbl = ( InputCount(n.type) >= 2 && s < 3 ) ? kLbl2[s] : "in";
                dl->AddText(ImVec2(pp.x + 10, pp.y - 8), IM_COL32(200, 200, 210, 255), lbl);
            }
        }

        // 出力ピン（ドラッグ or クリックで接続開始。当たり判定は広め）
        if ( HasOutput(n.type) ) {
            ImVec2 pp = outPinPos(n);
            ImGui::SetCursorScreenPos(ImVec2(pp.x - 12, pp.y - 12));
            ImGui::InvisibleButton("out", ImVec2(24, 24));
            bool hov = ImGui::IsItemHovered();
            if ( ImGui::IsItemActivated() ) {
                linkingFromNode_ = n.id;
                linkingSticky_ = false;
                linkStartX_ = ImGui::GetIO().MousePos.x;
                linkStartY_ = ImGui::GetIO().MousePos.y;
            }
            dl->AddCircleFilled(pp, hov ? kPinR + 2.0f : kPinR,
                hov || linkingFromNode_ == n.id
                ? IM_COL32(255, 255, 160, 255) : IM_COL32(230, 200, 120, 255));
        }

        // ノード内ウィジェット（値の編集・結果表示）
        float widgetY = p0.y + kTitleH + kRowH * ( std::max )( InputCount(n.type), 1 );
        if ( n.type == NodeType::ShaderColor ) {
            // 定数色：カラーピッカー
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, p0.y + kTitleH + 4));
            ImGui::SetNextItemWidth(kNodeWidth - 16);
            ImGui::ColorEdit4("##col", n.color,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        } else if ( n.type == NodeType::ShaderOutput ) {
            // 最終色：ターゲット選択＋適用/解除
            LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
            ImGui::SetNextItemWidth(kNodeWidth - 16);
            std::string current = le ? le->GetObjectLabel(n.target) : "(なし)";
            if ( ImGui::BeginCombo("##starget", current.c_str()) ) {
                int count = le ? le->GetObjectCount() : 0;
                if ( count == 0 ) { ImGui::TextDisabled("(配置オブジェクトなし)"); }
                for ( int i = 0; i < count; ++i ) {
                    if ( ImGui::Selectable(le->GetObjectLabel(i).c_str(), n.target == i) ) {
                        n.target = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY + kRowH));
            if ( ImGui::Button("適用", ImVec2(( kNodeWidth - 24 ) * 0.5f, 0)) ) {
                ApplyShaderToTarget(n);
            }
            ImGui::SameLine();
            if ( ImGui::Button("解除", ImVec2(( kNodeWidth - 24 ) * 0.5f, 0)) ) {
                if ( Obj3d* obj = le ? le->GetObject3d(n.target) : nullptr ) {
                    obj->SetCustomPipeline(nullptr);
                    status_ = "シェーダーを解除しました";
                }
            }
        } else if ( n.type == NodeType::Value ) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, p0.y + kTitleH + 4));
            ImGui::SetNextItemWidth(kNodeWidth - 16);
            ImGui::DragFloat("##v", &n.value, 0.1f);
        } else if ( n.type == NodeType::SinTime ) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
            ImGui::SetNextItemWidth(kNodeWidth - 16);
            ImGui::DragFloat("##spd", &n.value, 0.1f, 0.0f, 20.0f, "速さ %.1f");
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY + kRowH));
            ImGui::SetNextItemWidth(kNodeWidth - 16);
            ImGui::DragFloat("##amp", &n.param, 0.1f, 0.0f, 100.0f, "振幅 %.1f");
        } else if ( n.type == NodeType::Result ) {
            float v = Evaluate(n.id, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "= %.3f", v);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "%s", buf);
        } else if ( IsApplyNode(n.type) ) {
            // Obj系はターゲット（配置オブジェクト）をコンボで選ぶ
            if ( IsObjApplyNode(n.type) ) {
                LevelEditor* le = EditorManager::GetInstance()->GetLevelEditor();
                ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
                ImGui::SetNextItemWidth(kNodeWidth - 16);
                std::string current = le ? le->GetObjectLabel(n.target) : "(なし)";
                if ( ImGui::BeginCombo("##target", current.c_str()) ) {
                    int count = le ? le->GetObjectCount() : 0;
                    if ( count == 0 ) { ImGui::TextDisabled("(配置オブジェクトなし)"); }
                    for ( int i = 0; i < count; ++i ) {
                        if ( ImGui::Selectable(le->GetObjectLabel(i).c_str(), n.target == i) ) {
                            n.target = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                widgetY += kRowH;
            }
            // ゲーム値：シーンが登録した変数（プレイヤー速度・卵の投げ初速など）をコンボで選ぶ
            if ( n.type == NodeType::GameParam ) {
                ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
                ImGui::SetNextItemWidth(kNodeWidth - 16);
                const char* current = ( n.target >= 0 && n.target < ( int ) gameValues_.size() )
                    ? gameValues_[n.target].label.c_str() : "(なし)";
                if ( ImGui::BeginCombo("##gtarget", current) ) {
                    if ( gameValues_.empty() ) { ImGui::TextDisabled("(ゲーム値が未登録です)"); }
                    for ( int i = 0; i < ( int ) gameValues_.size(); ++i ) {
                        if ( ImGui::Selectable(gameValues_[i].label.c_str(), n.target == i) ) {
                            n.target = i;
                        }
                    }
                    ImGui::EndCombo();
                }
                widgetY += kRowH;
            }
            // 状態表示（接続＋実行中なら緑、未接続なら灰色）
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 8, widgetY));
            if ( HasInputLink(n.id, 0) ) {
                float v = Evaluate(n.id, 0);
                char buf[32];
                snprintf(buf, sizeof(buf), "適用中 %.2f", v);
                if ( runGraph_ ) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "%s", buf);
                else             ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "停止中 %.2f", v);
            } else {
                ImGui::TextDisabled("未接続");
            }
        } else if ( n.type == NodeType::Time ) {
            float v = Evaluate(n.id, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f s", v);
            dl->AddText(ImVec2(p1.x - 58, p0.y + 5), IM_COL32(255, 255, 255, 180), buf);
        } else {
            // 加算/乗算は現在値をタイトル横に小さく表示
            float v = Evaluate(n.id, 0);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", v);
            dl->AddText(ImVec2(p1.x - 46, p0.y + 5), IM_COL32(255, 255, 255, 180), buf);
        }

        ImGui::PopID();
    }

    // 接続中の仮接続線（出力ピン → マウス）
    if ( linkingFromNode_ >= 0 ) {
        const Node* from = nullptr;
        for ( const Node& n : nodes_ ) { if ( n.id == linkingFromNode_ ) { from = &n; break; } }
        if ( from ) {
            ImVec2 a = outPinPos(*from);
            ImVec2 b = ImGui::GetIO().MousePos;
            dl->AddBezierCubic(a, ImVec2(a.x + 50, a.y), ImVec2(b.x - 50, b.y), b,
                IM_COL32(255, 255, 160, 200), 2.0f);
        }

        ImVec2 mp = ImGui::GetIO().MousePos;
        if ( !linkingSticky_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) ) {
            // ほぼ動かず離した＝クリックだった → クリック接続モードに移行（線がついてくる）
            float dx = mp.x - linkStartX_;
            float dy = mp.y - linkStartY_;
            if ( dx * dx + dy * dy < 36.0f ) { linkingSticky_ = true; }
            else                             { linkingFromNode_ = -1; } // ドラッグ空振り
        } else if ( linkingSticky_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) ) {
            // 空クリック＝キャンセル（ピン/ノード上での接続は先に処理済み）
            linkingFromNode_ = -1;
        }
        // Esc / 右クリックでもキャンセル
        if ( ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ) {
            linkingFromNode_ = -1;
        }
    }

    // =========================================================
    // 空きスペースの操作（全アイテムの後に判定＝ノード操作を邪魔しない）
    // =========================================================
    bool emptyHovered = ImGui::IsWindowHovered()
        && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

    // 左ドラッグでパン（空きスペースから開始した時だけ）
    if ( emptyHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) ) {
        panningLeft_ = true;
        if ( linkingFromNode_ < 0 ) { selectedNodeId_ = -1; } // 空クリックで選択解除
    }
    if ( !ImGui::IsMouseDown(ImGuiMouseButton_Left) ) { panningLeft_ = false; }
    if ( panningLeft_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        scrollX_ += d.x; scrollY_ += d.y;
    }
    // 中ボタンドラッグでもパン
    if ( ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        scrollX_ += d.x; scrollY_ += d.y;
    }
    // 空きスペースを右クリック → ノード追加メニュー
    if ( emptyHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && linkingFromNode_ < 0 ) {
        ImGui::OpenPopup("add_node");
    }

    // キーボード操作（テキスト入力中は無効）
    if ( ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && !ImGui::GetIO().WantTextInput ) {
        // Delete = 選択ノードを削除
        if ( selectedNodeId_ >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete) ) {
            deleteNodeId = selectedNodeId_;
            selectedNodeId_ = -1;
        }
        // Ctrl+D = 選択ノードを複製
        if ( selectedNodeId_ >= 0 && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) ) {
            for ( const Node& n : nodes_ ) {
                if ( n.id == selectedNodeId_ ) {
                    Node copy = n;
                    copy.id = nextId_++;
                    copy.posX += 30.0f; copy.posY += 30.0f;
                    nodes_.push_back(copy);
                    selectedNodeId_ = copy.id;
                    break;
                }
            }
        }
    }

    // ノード追加メニュー（キャンバス右クリック）
    if ( ImGui::BeginPopup("add_node") ) {
        ImVec2 mp = ImGui::GetMousePosOnOpeningCurrentPopup();
        float cx = mp.x - origin.x;
        float cy = mp.y - origin.y;
        ImGui::TextDisabled("ノードを追加");
        ImGui::Separator();
        NodeType allTypes[] = {
            NodeType::Value, NodeType::Time, NodeType::SinTime,
            NodeType::Add, NodeType::Multiply, NodeType::Result,
            NodeType::LightIntensity, NodeType::ParticleRate,
            NodeType::ParticleGravity, NodeType::TimeScale,
            NodeType::ObjPosY, NodeType::ObjRotY, NodeType::ObjScale, NodeType::ObjParam,
            NodeType::ShaderTexture, NodeType::ShaderUV, NodeType::ShaderColor,
            NodeType::ShaderParam, NodeType::ShaderMul, NodeType::ShaderAdd,
            NodeType::ShaderMix, NodeType::ShaderGray, NodeType::ShaderInvert,
            NodeType::ShaderOutput,
        };
        for ( NodeType t : allTypes ) {
            if ( ImGui::MenuItem(TypeName(t)) ) { AddNode(t, cx, cy); }
            if ( ImGui::IsItemHovered() ) { ImGui::SetTooltip("%s", TypeDesc(t)); }
        }
        ImGui::EndPopup();
    }

    if ( deleteNodeId >= 0 ) { RemoveNode(deleteNodeId); }

    // クリックしたノードを最前面（＝配列の末尾）へ移動して、重なっても操作しやすくする
    if ( raiseNodeId >= 0 ) {
        auto it = std::find_if(nodes_.begin(), nodes_.end(),
            [raiseNodeId](const Node& n) { return n.id == raiseNodeId; });
        if ( it != nodes_.end() && it + 1 != nodes_.end() ) {
            Node moved = *it;
            nodes_.erase(it);
            nodes_.push_back(moved);
        }
    }

    ImGui::EndChild();
    ImGui::End();
#endif
}
