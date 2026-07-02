#pragma once
#include <string>
#include <vector>
#include <cstdint>

class GPUParticleEmitter;

// =====================================================================
//  NodeEditor
//   Unreal Engine のブループリント風のノードグラフエディタ。
//   ・左のパレットからクリックでノード追加（右クリックでも可）
//   ・ピン同士をドラッグして接続 → 値が流れて計算される
//   ・「適用ノード」に繋ぐと実際のゲームがリアルタイムに変わる
//       - ライト強度 / パーティクル発生レート / パーティクル重力 / タイムスケール
//   ・起動時に自動読み込み、Ctrl+S / 保存ボタンで保存
// =====================================================================
class NodeEditor {
public:
    void Initialize();

    // 毎フレーム呼ぶ：グラフを評価して「適用ノード」の値をゲームに反映する
    // （ウィンドウが閉じていてもグラフは動き続ける）
    void Update();

    // openFlag: ウィンドウの×ボタンで閉じられるようにするためのフラグ
    void DrawDebugUI(bool* openFlag);

    // パーティクル系の適用ノードが操作するエミッター（シーンから受け取る）
    void SetParticleEmitter(GPUParticleEmitter* emitter) { emitter_ = emitter; }

    // --- ゲーム値の登録（シーンから受け取る）---
    //   「→ ゲーム値」ノードのターゲット一覧になる。プレイヤー速度・卵の投げ初速など、
    //   ゲーム側の float 変数を登録すると、ノードから直接動かせる。
    //   ※ポインタは所有しない。シーン終了時に必ず ClearGameValues() を呼ぶこと（ダングリング防止）。
    void RegisterGameValue(const std::string& label, float* target, float minV, float maxV) {
        gameValues_.push_back({ label, target, minV, maxV });
    }
    void ClearGameValues() { gameValues_.clear(); }

private:
    // ノードの種類
    enum class NodeType : int {
        // --- 入力（値を作る） ---
        Value = 0,        // 数値（定数）
        Time,             // 経過時間（秒）
        SinTime,          // sin(時間×速さ)×振幅（動く値）
        // --- 計算 ---
        Add,              // 加算 a+b
        Multiply,         // 乗算 a*b
        // --- 表示 ---
        Result,           // 出力（結果表示のみ）
        // --- 適用（ゲームに作用する） ---
        LightIntensity,   // 平行光源の強度に反映
        ParticleRate,     // パーティクル発生レートに反映
        ParticleGravity,  // パーティクル重力Yに反映
        TimeScale,        // タイムスケールに反映
        // --- 適用（配置オブジェクトに作用する。ノード内でターゲットを選ぶ） ---
        ObjPosY,          // 選んだオブジェクトのY座標に反映
        ObjRotY,          // 選んだオブジェクトのY回転に反映
        ObjScale,         // 選んだオブジェクトの大きさに反映
        ObjParam,         // 選んだオブジェクトのシェーダーパラメータ(0〜1)に反映
        // --- シェーダー（マテリアルグラフ。HLSLを自動生成してコンパイル） ---
        ShaderTexture,    // テクスチャ色
        ShaderUV,         // UV座標（デバッグ表示にも便利）
        ShaderColor,      // 定数色（RGBA）
        ShaderParam,      // パラメータ（CPUノードから動かせる 0〜1）
        ShaderMul,        // 色乗算 A×B
        ShaderAdd,        // 色加算 A+B
        ShaderMix,        // ミックス lerp(A, B, T)
        ShaderGray,       // グレースケール化
        ShaderInvert,     // 色反転
        ShaderOutput,     // 最終色（コンパイルしてオブジェクトに適用）
        // --- 適用（シーンが登録したゲーム値に作用する。※末尾に追加＝保存データ互換のため） ---
        GameParam,        // 登録されたゲームの変数（プレイヤー速度・卵の投げ初速など）に反映
    };

    struct Node {
        int      id = 0;
        NodeType type = NodeType::Value;
        float    posX = 0.0f, posY = 0.0f; // キャンバス座標
        float    value = 0.0f;             // Value: 値 / SinTime: 速さ
        float    param = 1.0f;             // SinTime: 振幅
        int      target = 0;               // Obj系/シェーダー出力: オブジェクト番号
        float    inConst[3] = { 0.0f, 0.0f, 0.0f }; // 未接続の入力に直接入れる数値
        float    color[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // ShaderColor: 定数色
    };

    // 接続（出力ピン → 入力ピン）
    struct Link {
        int fromNode = 0; // 出力側ノードID
        int toNode = 0;   // 入力側ノードID
        int toSlot = 0;   // 入力スロット番号 (0 or 1)
    };

    // --- グラフ操作 ---
    int  AddNode(NodeType type, float x, float y);
    void RemoveNode(int nodeId);
    void RemoveLinksTo(int nodeId, int slot);
    bool HasInputLink(int nodeId, int slot) const;
    const char* TypeName(NodeType t) const;
    const char* TypeDesc(NodeType t) const;
    int  InputCount(NodeType t) const;
    bool HasOutput(NodeType t) const;
    bool IsApplyNode(NodeType t) const;
    bool IsObjApplyNode(NodeType t) const; // ターゲット選択が必要な適用ノードか
    bool IsShaderNode(NodeType t) const;   // シェーダー（マテリアル）系ノードか

    // --- シェーダー生成（HLSLコード生成 → コンパイル → PSO → 適用） ---
    std::string GenShaderExpr(int nodeId, int depth) const;        // ノード→HLSL式
    std::string GenShaderInput(int nodeId, int slot, int depth) const;
    void ApplyShaderToTarget(const Node& outputNode);              // 最終色ノードの「適用」

    // --- 評価（値を流す） ---
    float Evaluate(int nodeId, int depth) const;
    float EvaluateInput(int nodeId, int slot, int depth) const;

    // --- 保存 / 読み込み ---
    void Save(const std::string& path) const;
    void Load(const std::string& path);

    std::vector<Node> nodes_;
    std::vector<Link> links_;
    int nextId_ = 1;

    // キャンバス表示状態
    float scrollX_ = 0.0f, scrollY_ = 0.0f;

    // 接続ドラッグ中の状態（-1 = 非ドラッグ）
    int   linkingFromNode_ = -1;
    bool  linkingSticky_ = false;              // クリック→クリック接続モード
    float linkStartX_ = 0.0f, linkStartY_ = 0.0f; // 接続開始時のマウス位置

    // 選択・パン状態
    int  selectedNodeId_ = -1; // 選択中ノード（Delete=削除 / Ctrl+D=複製）
    bool panningLeft_ = false; // 空きスペース左ドラッグでのパン中か

    // グラフの実行ON/OFF（OFFにすると適用ノードが作用しなくなる）
    bool runGraph_ = true;

    // 適用ノードの操作対象（所有しない）
    GPUParticleEmitter* emitter_ = nullptr;

    // 「→ ゲーム値」ノードのターゲット一覧（シーンが登録。所有しない）
    struct GameValue {
        std::string label;   // 表示名（例:「プレイヤー移動速度」）
        float* target = nullptr;
        float  minV = 0.0f, maxV = 1.0f; // 反映時のクランプ範囲
    };
    std::vector<GameValue> gameValues_;

    char fileBuf_[128] = "resources/nodes/graph01.json";
    std::string status_;
};
