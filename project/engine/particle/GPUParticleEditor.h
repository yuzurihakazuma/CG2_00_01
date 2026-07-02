#pragma once
#include <string>
#include <vector>

class GPUParticleEmitter;

// GPUパーティクルエディタクラス
class GPUParticleEditor{
public:

    // 編集対象のエミッターをセット（GamePlaySceneから渡してもらう）
    void SetEmitter(GPUParticleEmitter* emitter);


    // メニューバーから呼ぶ保存/ロード
    void Save();
    void Load();

    // ImGui UIの描画（EditorManager::Update() から呼ぶ）
    void DrawDebugUI();


private:
    // resources/particles/ の .json を列挙する
    void ScanFiles();

    // プリセット適用（0:炎 1:煙 2:火花 3:雪 4:魔法）
    void ApplyPreset(int index);

    // 編集対象（所有はしない。シーン側が持つ）
    GPUParticleEmitter* emitter_ = nullptr;

    // 保存先ファイルパス
    char saveFileName_[256] = "resources/particles/emitter01.json";

    // 保存済みファイル一覧（コンボで選んで読み込める）
    std::vector<std::string> fileList_;
    int selectedFile_ = -1;
    bool scanned_ = false;
};


