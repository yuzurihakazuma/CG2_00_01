#include "DebugCamera.h"

// --- 標準・外部ライブラリ ---
#include <algorithm>
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// --- エンジン側のファイル ---
#include "Camera.h" 
#include "engine/math/VectorMath.h"
#include "engine/math/Matrix4x4.h"
#include "engine/base/Input.h" // ★作成したInputクラスを使う！


using namespace VectorMath;
using namespace MatrixMath;

void DebugCamera::Initialize() {
	// DirectInputのおかげで初期化時に記憶するものがなくなりました！
}

void DebugCamera::Update(Camera* camera) {
    if (!camera){
        return;
    }

    // UI等で使うために現在のターゲットカメラを保持
    targetCamera_ = camera;

    // アクティブじゃない時は何もしない
    if (!isActive_){
        return;
    }

    // 現在のカメラの位置と回転を取得
    Vector3 translation = camera->GetWorldPosition();
    Vector3 rotation = camera->GetRotation();

    // Inputクラスのインスタンスを取得
    Input* input = Input::GetInstance();

    // 右ドラッグ中：視点回転（先に回転を確定させてから移動方向を計算する）
    if (input->PushMouseButton(1)) {
        // フォーカス復帰直後などの巨大な差分スパイクで視点が吹っ飛ばないようクランプ
        float dx = std::clamp(input->GetMouseMoveX(), -200.0f, 200.0f);
        float dy = std::clamp(input->GetMouseMoveY(), -200.0f, 200.0f);
        const float rotationSpeed = 0.003f; // 回転の感度
        rotation.y += dx * rotationSpeed;
        rotation.x += dy * rotationSpeed;
        const float limit = 1.5f; // 約85度で頭打ち
        rotation.x = std::clamp(rotation.x, -limit, limit);
    }

    // 向いている角度から「前」「右」「上」の方向を求める（移動系で共通利用）
    Matrix4x4 rotateMatrix = Multiply(MakeRotateX(rotation.x), MakeRotateY(rotation.y));
    Vector3 forward = Transforms({ 0.0f, 0.0f, 1.0f }, rotateMatrix);
    Vector3 right   = Transforms({ 1.0f, 0.0f, 0.0f }, rotateMatrix);
    Vector3 up      = Transforms({ 0.0f, 1.0f, 0.0f }, rotateMatrix);

    float moveSpeed = 0.2f;
    if (input->Pushkey(DIK_LSHIFT)) { moveSpeed *= 3.0f; } // Shiftで高速

    // WASD / Q,E で移動（右ドラッグ中のみ。視点を回しながら飛び回る用）
    if (input->PushMouseButton(1)) {
        if (input->Pushkey(DIK_W)) translation = Add(translation, Multiply( moveSpeed, forward));
        if (input->Pushkey(DIK_S)) translation = Add(translation, Multiply(-moveSpeed, forward));
        if (input->Pushkey(DIK_D)) translation = Add(translation, Multiply( moveSpeed, right));
        if (input->Pushkey(DIK_A)) translation = Add(translation, Multiply(-moveSpeed, right));
        if (input->Pushkey(DIK_E)) translation = Add(translation, Multiply( moveSpeed, up));
        if (input->Pushkey(DIK_Q)) translation = Add(translation, Multiply(-moveSpeed, up));
    }

    // ★ホイール：前後ズーム（Game View にマウスが乗っている時だけ効く）。
    //   ImGuiパネル（レールエディタ等）の上でスクロールしてもカメラは動かさない。
    //   1フレームの適用量は3ノッチ分まで（差分スパイクで一気に吹っ飛ばない保険）
    float wheel = std::clamp(input->GetMouseWheel(), -360.0f, 360.0f);
    if (wheel != 0.0f && gameViewHovered_) {
        const float zoomSpeed = 0.01f; // DirectInputのホイール量(±120単位)に対する係数
        translation = Add(translation, Multiply(wheel * zoomSpeed, forward));
    }

    // ★中ドラッグ：平行移動（パン）。画面の右/上方向にスライド
    if (input->PushMouseButton(2)) {
        float dx = std::clamp(input->GetMouseMoveX(), -200.0f, 200.0f);
        float dy = std::clamp(input->GetMouseMoveY(), -200.0f, 200.0f);
        const float panSpeed = 0.01f;
        translation = Add(translation, Multiply(-dx * panSpeed, right)); // マウス右→視界左へ流れる
        translation = Add(translation, Multiply( dy * panSpeed, up));    // マウス下→視界上へ流れる
    }

    // 更新した新しい位置と角度を本物のカメラにセット！
    camera->SetTranslation(translation);
    camera->SetRotation(rotation);
}

// 外部（メニュー等）からの ON/OFF。切替時に元カメラ姿勢を保存/復元する
void DebugCamera::SetActive(bool active){
    if (active == isActive_) return;
    if (targetCamera_) {
        if (active) {
            // OFF→ON：元の見た目を覚えておく
            preCameraPos_ = targetCamera_->GetTransform().translate;
            preCameraRot_ = targetCamera_->GetTransform().rotate;
        } else {
            // ON→OFF：元の見た目に戻す
            targetCamera_->SetTranslation(preCameraPos_);
            targetCamera_->SetRotation(preCameraRot_);
        }
    }
    isActive_ = active;
}


void DebugCamera::DrawDebugUI(){
#ifdef USE_IMGUI
    if ( ImGui::Begin("インスペクター (詳細設定)") ) {
        if ( ImGui::CollapsingHeader("デバッグカメラ (Debug Camera)", ImGuiTreeNodeFlags_DefaultOpen) ) {

            // ※ ON/OFF はメニューバー「表示(View)」に移動しました。
            ImGui::Text("状態: %s", isActive_ ? "有効" : "無効");
            ImGui::TextDisabled("ON/OFF はメニューバー「表示」から");

            // 操作方法の説明
            ImGui::TextDisabled("操作方法:");
            ImGui::Text("  右ドラッグ      : 視点回転");
            ImGui::Text("  右ドラッグ+WASD : 飛行移動 / Q,E : 上下");
            ImGui::Text("  ホイール        : 前後ズーム");
            ImGui::Text("  中ドラッグ      : 平行移動（パン）");
            ImGui::Text("  Shift           : 高速移動");
        }
    }
    ImGui::End();
#endif
}