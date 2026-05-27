#include "AudioManager.h"
// --- 標準ライブラリ ---
#include <fstream>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

AudioManager* AudioManager::GetInstance(){
    static AudioManager instance;
    return &instance;
}

void AudioManager::Initialize(){
    HRESULT result;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(result));

    result = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(result));

    masterVoice_->SetVolume(masterVolume_);
}

void AudioManager::Finalize(){
    bgmVoice_.reset();
    loopSEVoices_.clear();
    playingVoices_.clear();

    if ( masterVoice_ ) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    xAudio2_.Reset();
    soundDatas_.clear();

    MFShutdown();
    CoUninitialize();
}

// ─────────────────────────────────────────
// 内部ヘルパー：名前 → ファイルパス解決 + 自動ロード
// ─────────────────────────────────────────
// variantRegistry_ に登録があればランダムに1つ選ぶ。
// soundRegistry_ に登録があればそのパスを使う。
// どちらにもなければ name をそのままパスとして扱う。
// 未ロードなら LoadWave で自動ロードする。
std::string AudioManager::Resolve(const std::string& name){
    // バリエーション登録が優先
    auto varIt = variantRegistry_.find(name);
    if ( varIt != variantRegistry_.end() && !varIt->second.empty() ) {
        int idx = rand() % static_cast<int>(varIt->second.size());
        const std::string& filepath = varIt->second[idx];
        if ( soundDatas_.find(filepath) == soundDatas_.end() ) { LoadWave(filepath); }
        return filepath;
    }
    // 通常登録
    std::string filepath = name;
    auto regIt = soundRegistry_.find(name);
    if ( regIt != soundRegistry_.end() ) { filepath = regIt->second; }
    if ( soundDatas_.find(filepath) == soundDatas_.end() ) { LoadWave(filepath); }
    return filepath;
}

// ─────────────────────────────────────────
// 名前登録システム
// ─────────────────────────────────────────

void AudioManager::Register(const std::string& name, const std::string& filepath){
    soundRegistry_[name] = filepath;
}

void AudioManager::RegisterVariants(const std::string& name, const std::vector<std::string>& filepaths){
    variantRegistry_[name] = filepaths;
}

void AudioManager::LoadAll(){
    for ( const auto& [name, filepath] : soundRegistry_ ) {
        LoadWave(filepath);
    }
    for ( const auto& [name, paths] : variantRegistry_ ) {
        for ( const auto& path : paths ) {
            LoadWave(path);
        }
    }
}

void AudioManager::Unload(const std::string& name){
    // 登録名からファイルパスを解決（登録名でも直接パスでも受け付ける）
    std::string filepath = name;
    auto regIt = soundRegistry_.find(name);
    if ( regIt != soundRegistry_.end() ) { filepath = regIt->second; }

    // ※ 再生中のボイスが参照しているバッファを解放するとクラッシュするため、
    //    必ず再生を停止してから呼ぶこと
    soundDatas_.erase(filepath);
}

// ─────────────────────────────────────────
// BGM 専用関数
// ─────────────────────────────────────────

void AudioManager::PlayBGM(const std::string& name, float fadeInSec){
    std::string filepath = Resolve(name);
    SoundData& soundData = soundDatas_[filepath];

    if ( bgmVoice_ ) { bgmVoice_->Stop(0); bgmVoice_.reset(); }
    isBGMFading_ = false;  isBGMPaused_ = false;
    bgmFadeAction_ = BGMFadeAction::None;
    pendingBGMName_.clear();

    IXAudio2SourceVoice* rawVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&rawVoice, &soundData.wfex);
    assert(SUCCEEDED(result));
    bgmVoice_ = SourceVoicePtr(rawVoice);

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.LoopCount  = XAUDIO2_LOOP_INFINITE;
    buf.Flags      = 0;
    result = bgmVoice_->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(result));

    float targetVol = bgmMuted_ ? 0.0f : bgmVolume_;

    if ( fadeInSec > 0.0f ) {
        bgmVoice_->SetVolume(0.0f);
        bgmFadeFrom_ = 0.0f;  bgmFadeTo_ = targetVol;
        bgmFadeDuration_ = fadeInSec;  bgmFadeTimer_ = 0.0f;
        isBGMFading_ = true;  bgmFadeAction_ = BGMFadeAction::ChangeVolume;
    } else {
        bgmVoice_->SetVolume(targetVol);
    }

    result = bgmVoice_->Start();
    assert(SUCCEEDED(result));
    currentBGMName_ = name;
}

void AudioManager::StopBGM(float fadeOutSec){
    if ( !bgmVoice_ ) return;

    if ( fadeOutSec > 0.0f ) {
        float currentVol = bgmVolume_;
        bgmVoice_->GetVolume(&currentVol);
        bgmFadeFrom_ = currentVol;  bgmFadeTo_ = 0.0f;
        bgmFadeDuration_ = fadeOutSec;  bgmFadeTimer_ = 0.0f;
        isBGMFading_ = true;  bgmFadeAction_ = BGMFadeAction::StopAndDestroy;
    } else {
        bgmVoice_->Stop(0);  bgmVoice_.reset();
        isBGMFading_ = false;  isBGMPaused_ = false;
        bgmFadeAction_ = BGMFadeAction::None;
        currentBGMName_.clear();
    }
}

void AudioManager::ChangeBGM(const std::string& name, float fadeSec){
    if ( !bgmVoice_ || fadeSec <= 0.0f ) {
        PlayBGM(name, fadeSec > 0.0f ? fadeSec : 0.0f);
        return;
    }
    pendingBGMName_   = name;
    pendingBGMFadeIn_ = fadeSec;
    StopBGM(fadeSec);
}

void AudioManager::PauseBGM(float fadeOutSec){
    if ( !bgmVoice_ || isBGMPaused_ ) return;

    if ( fadeOutSec > 0.0f ) {
        float currentVol = bgmVolume_;
        bgmVoice_->GetVolume(&currentVol);
        bgmFadeFrom_ = currentVol;  bgmFadeTo_ = 0.0f;
        bgmFadeDuration_ = fadeOutSec;  bgmFadeTimer_ = 0.0f;
        isBGMFading_ = true;  bgmFadeAction_ = BGMFadeAction::StopAndPause;
    } else {
        bgmVoice_->Stop(0);
        isBGMPaused_ = true;
    }
}

void AudioManager::ResumeBGM(float fadeInSec){
    if ( !bgmVoice_ || !isBGMPaused_ ) return;
    isBGMPaused_ = false;
    bgmVoice_->Start();

    float targetVol = bgmMuted_ ? 0.0f : bgmVolume_;

    if ( fadeInSec > 0.0f ) {
        bgmVoice_->SetVolume(0.0f);
        bgmFadeFrom_ = 0.0f;  bgmFadeTo_ = targetVol;
        bgmFadeDuration_ = fadeInSec;  bgmFadeTimer_ = 0.0f;
        isBGMFading_ = true;  bgmFadeAction_ = BGMFadeAction::ChangeVolume;
    } else {
        bgmVoice_->SetVolume(targetVol);
    }
}

void AudioManager::FadeBGMVolume(float targetVolume, float durationSec){
    if ( !bgmVoice_ ) return;
    targetVolume = (std::max)(0.0f, (std::min)(targetVolume, 1.0f));

    if ( durationSec <= 0.0f ) {
        bgmVolume_ = targetVolume;
        bgmVoice_->SetVolume(bgmMuted_ ? 0.0f : bgmVolume_);
        return;
    }

    float currentVol = bgmVolume_;
    bgmVoice_->GetVolume(&currentVol);
    bgmFadeFrom_ = currentVol;
    bgmFadeTo_   = bgmMuted_ ? 0.0f : targetVolume;
    bgmFadeDuration_ = durationSec;  bgmFadeTimer_ = 0.0f;
    isBGMFading_ = true;  bgmFadeAction_ = BGMFadeAction::ChangeVolume;
    bgmVolume_   = targetVolume; // 目標値を記憶
}

// ─────────────────────────────────────────
// SE 再生（撃ちっぱなし）
// ─────────────────────────────────────────

void AudioManager::PlaySE(const std::string& name, float volume,
                          float pitchVariation, int maxPolyphony, float cooldownSec){
    if ( seMuted_ ) return;

    // クールダウン中ならスキップ
    if ( cooldownSec > 0.0f ) {
        auto cdIt = seCooldownTimers_.find(name);
        if ( cdIt != seCooldownTimers_.end() && cdIt->second > 0.0f ) { return; }
        seCooldownTimers_[name] = cooldownSec; // クールダウン開始
    }

    std::string filepath = Resolve(name);
    SoundData& soundData = soundDatas_[filepath];

    // 重複制限チェック
    if ( maxPolyphony > 0 ) {
        int count = 0;
        for ( const auto& se : playingVoices_ ) {
            if ( se.name == name ) { ++count; }
        }
        if ( count >= maxPolyphony ) { return; }
    }

    IXAudio2SourceVoice* rawVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&rawVoice, &soundData.wfex);
    assert(SUCCEEDED(result));
    SourceVoicePtr voice(rawVoice);

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    result = voice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(result));

    voice->SetVolume(seVolume_ * volume);

    if ( pitchVariation > 0.0f ) {
        float t     = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        float pitch = 1.0f + (t * 2.0f - 1.0f) * pitchVariation;
        pitch = (std::max)(XAUDIO2_MIN_FREQ_RATIO, (std::min)(pitch, XAUDIO2_MAX_FREQ_RATIO));
        voice->SetFrequencyRatio(pitch);
    }

    result = voice->Start();
    assert(SUCCEEDED(result));

    playingVoices_.push_back({ std::move(voice), name });
}

// ─────────────────────────────────────────
// 簡易 3D 音量（距離減衰）
// ─────────────────────────────────────────

void AudioManager::PlaySE3D(const std::string& name,
                             const Vector3& soundPos, const Vector3& listenerPos,
                             float maxDistance,
                             float volume, float pitchVariation,
                             int maxPolyphony, float cooldownSec){
    // 音源とリスナーの距離を計算
    float dx   = soundPos.x - listenerPos.x;
    float dy   = soundPos.y - listenerPos.y;
    float dz   = soundPos.z - listenerPos.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    // 線形減衰: 距離 0 → 音量そのまま、距離 maxDistance → 無音
    float distVol = 1.0f - (std::min)(dist / maxDistance, 1.0f);

    // 聴こえないほど遠ければ再生しない
    if ( distVol <= 0.0f ) { return; }

    PlaySE(name, volume * distVol, pitchVariation, maxPolyphony, cooldownSec);
}

// ─────────────────────────────────────────
// ループ SE
// ─────────────────────────────────────────

int AudioManager::PlaySELoop(const std::string& name, float volume){
    std::string filepath = Resolve(name);
    SoundData& soundData = soundDatas_[filepath];

    IXAudio2SourceVoice* rawVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&rawVoice, &soundData.wfex);
    assert(SUCCEEDED(result));
    SourceVoicePtr voice(rawVoice);

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.LoopCount  = XAUDIO2_LOOP_INFINITE;
    buf.Flags      = 0;
    result = voice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(result));

    voice->SetVolume(seMuted_ ? 0.0f : seVolume_ * volume);

    result = voice->Start();
    assert(SUCCEEDED(result));

    int handle = nextLoopHandle_++;
    LoopSE loopSE{};
    loopSE.voice  = std::move(voice);
    loopSE.name   = name;
    loopSE.volume = volume;
    loopSEVoices_[handle] = std::move(loopSE);

    return handle;
}

void AudioManager::StopSELoop(int handle, float fadeOutSec){
    auto it = loopSEVoices_.find(handle);
    if ( it == loopSEVoices_.end() ) return;
    auto& loopSE = it->second;

    if ( fadeOutSec > 0.0f ) {
        float currentVol = 0.0f;
        loopSE.voice->GetVolume(&currentVol);
        loopSE.fadeFrom      = currentVol;
        loopSE.fadeTo        = 0.0f;
        loopSE.fadeDuration  = fadeOutSec;
        loopSE.fadeTimer     = 0.0f;
        loopSE.isFading      = true;
        loopSE.stopAfterFade = true;
    } else {
        loopSE.voice->Stop(0);
        loopSEVoices_.erase(it);
    }
}

void AudioManager::SetSELoopVolume(int handle, float volume, float fadeSec){
    auto it = loopSEVoices_.find(handle);
    if ( it == loopSEVoices_.end() ) return;
    auto& loopSE  = it->second;
    loopSE.volume = (std::max)(0.0f, (std::min)(volume, 1.0f));
    float targetVol = seMuted_ ? 0.0f : seVolume_ * loopSE.volume;

    if ( fadeSec > 0.0f ) {
        float currentVol = 0.0f;
        loopSE.voice->GetVolume(&currentVol);
        loopSE.fadeFrom      = currentVol;
        loopSE.fadeTo        = targetVol;
        loopSE.fadeDuration  = fadeSec;
        loopSE.fadeTimer     = 0.0f;
        loopSE.isFading      = true;
        loopSE.stopAfterFade = false;
    } else {
        loopSE.voice->SetVolume(targetVol);
        loopSE.isFading = false;
    }
}

// ─────────────────────────────────────────
// SE 停止
// ─────────────────────────────────────────

void AudioManager::StopSE(const std::string& name){
    for ( auto it = playingVoices_.begin(); it != playingVoices_.end(); ) {
        if ( it->name == name ) { it->voice->Stop(0); it = playingVoices_.erase(it); }
        else { ++it; }
    }
    for ( auto it = loopSEVoices_.begin(); it != loopSEVoices_.end(); ) {
        if ( it->second.name == name ) { it->second.voice->Stop(0); it = loopSEVoices_.erase(it); }
        else { ++it; }
    }
}

void AudioManager::StopAllSE(){
    for ( auto& se : playingVoices_ ) { se.voice->Stop(0); }
    playingVoices_.clear();
    for ( auto& [handle, loopSE] : loopSEVoices_ ) { loopSE.voice->Stop(0); }
    loopSEVoices_.clear();
}

// ─────────────────────────────────────────
// ミュート
// ─────────────────────────────────────────

void AudioManager::MuteBGM(bool mute){
    bgmMuted_ = mute;
    if ( bgmVoice_ && !isBGMFading_ ) {
        bgmVoice_->SetVolume(mute ? 0.0f : bgmVolume_);
    }
}

void AudioManager::MuteSE(bool mute){
    seMuted_ = mute;
    for ( auto& [handle, loopSE] : loopSEVoices_ ) {
        if ( !loopSE.isFading ) {
            loopSE.voice->SetVolume(mute ? 0.0f : seVolume_ * loopSE.volume);
        }
    }
}

void AudioManager::MuteAll(bool mute){
    MuteBGM(mute);
    MuteSE(mute);
}

// ─────────────────────────────────────────
// 音量カテゴリ
// ─────────────────────────────────────────

void AudioManager::SetMasterVolume(float v){
    masterVolume_ = (std::max)(0.0f, (std::min)(v, 1.0f));
    // フォーカスがある場合のみマスターボイスに反映（フォーカス外は 0 のまま）
    if ( masterVoice_ && isFocused_ ) {
        masterVoice_->SetVolume(masterVolume_);
    }
}

void AudioManager::SetBGMVolume(float v){
    bgmVolume_ = (std::max)(0.0f, (std::min)(v, 1.0f));
    if ( bgmVoice_ && !isBGMFading_ ) {
        bgmVoice_->SetVolume(bgmMuted_ ? 0.0f : bgmVolume_);
    }
}

void AudioManager::SetSEVolume(float v){
    seVolume_ = (std::max)(0.0f, (std::min)(v, 1.0f));
    for ( auto& [handle, loopSE] : loopSEVoices_ ) {
        if ( !loopSE.isFading ) {
            loopSE.voice->SetVolume(seMuted_ ? 0.0f : seVolume_ * loopSE.volume);
        }
    }
}

// ─────────────────────────────────────────
// 音量設定の保存・読み込み
// ─────────────────────────────────────────

void AudioManager::SaveSettings(const std::string& filepath){
    std::ofstream file(filepath, std::ios::binary);
    if ( !file.is_open() ) { return; }
    file.write(reinterpret_cast<const char*>(&masterVolume_), sizeof(float));
    file.write(reinterpret_cast<const char*>(&bgmVolume_),    sizeof(float));
    file.write(reinterpret_cast<const char*>(&seVolume_),     sizeof(float));
    file.write(reinterpret_cast<const char*>(&bgmMuted_),     sizeof(bool));
    file.write(reinterpret_cast<const char*>(&seMuted_),      sizeof(bool));
}

void AudioManager::LoadSettings(const std::string& filepath){
    std::ifstream file(filepath, std::ios::binary);
    if ( !file.is_open() ) { return; } // ファイルがなければデフォルト値のまま

    float master = masterVolume_, bgm = bgmVolume_, se = seVolume_;
    bool  muteB = bgmMuted_, muteS = seMuted_;

    file.read(reinterpret_cast<char*>(&master), sizeof(float));
    file.read(reinterpret_cast<char*>(&bgm),    sizeof(float));
    file.read(reinterpret_cast<char*>(&se),     sizeof(float));
    file.read(reinterpret_cast<char*>(&muteB),  sizeof(bool));
    file.read(reinterpret_cast<char*>(&muteS),  sizeof(bool));

    SetMasterVolume(master);
    SetBGMVolume(bgm);
    SetSEVolume(se);
    MuteBGM(muteB);
    MuteSE(muteS);
}

// ─────────────────────────────────────────
// アプリフォーカス制御
// ─────────────────────────────────────────

void AudioManager::OnFocusLost(){
    if ( !isFocused_ ) return;
    isFocused_ = false;
    // マスターボイスを 0 にして全音声を無音にする（masterVolume_ 自体は変えない）
    if ( masterVoice_ ) { masterVoice_->SetVolume(0.0f); }
}

void AudioManager::OnFocusGained(){
    if ( isFocused_ ) return;
    isFocused_ = true;
    // 保存していた masterVolume_ に戻す
    if ( masterVoice_ ) { masterVoice_->SetVolume(masterVolume_); }
}

// ─────────────────────────────────────────
// デバッグ用
// ─────────────────────────────────────────

bool AudioManager::IsRegistered(const std::string& name) const{
    return soundRegistry_.find(name)   != soundRegistry_.end() ||
           variantRegistry_.find(name) != variantRegistry_.end();
}

int AudioManager::GetSEPlayCount(const std::string& name) const{
    int count = 0;
    for ( const auto& se : playingVoices_ ) {
        if ( se.name == name ) { ++count; }
    }
    for ( const auto& [handle, loopSE] : loopSEVoices_ ) {
        if ( loopSE.name == name ) { ++count; }
    }
    return count;
}

// ─────────────────────────────────────────
// 毎フレーム更新（60FPS 固定）
// ─────────────────────────────────────────

void AudioManager::Update(){
    constexpr float kDeltaTime = 1.0f / 60.0f;

    // --- 撃ちっぱなし SE のクリーンアップ ---
    UpdateVoices();

    // --- SE クールダウンタイマーを減算 ---
    for ( auto it = seCooldownTimers_.begin(); it != seCooldownTimers_.end(); ) {
        it->second -= kDeltaTime;
        if ( it->second <= 0.0f ) { it = seCooldownTimers_.erase(it); }
        else { ++it; }
    }

    // --- BGM フェード処理 ---
    if ( isBGMFading_ && bgmVoice_ ) {
        bgmFadeTimer_ += kDeltaTime;
        float t = (bgmFadeDuration_ > 0.0f) ? bgmFadeTimer_ / bgmFadeDuration_ : 1.0f;
        t = (std::min)(t, 1.0f);

        bgmVoice_->SetVolume(bgmFadeFrom_ + (bgmFadeTo_ - bgmFadeFrom_) * t);

        if ( t >= 1.0f ) {
            isBGMFading_ = false;

            switch ( bgmFadeAction_ ) {
            case BGMFadeAction::StopAndDestroy:
                bgmVoice_->Stop(0);  bgmVoice_.reset();
                currentBGMName_.clear();
                if ( !pendingBGMName_.empty() ) {
                    std::string next   = pendingBGMName_;
                    float       nextFI = pendingBGMFadeIn_;
                    pendingBGMName_.clear();  pendingBGMFadeIn_ = 0.0f;
                    PlayBGM(next, nextFI);
                }
                break;
            case BGMFadeAction::StopAndPause:
                bgmVoice_->Stop(0);
                isBGMPaused_ = true;
                break;
            case BGMFadeAction::ChangeVolume:
                // 音量変化のみ完了（何もしない）
                break;
            default:
                break;
            }
            bgmFadeAction_ = BGMFadeAction::None;
        }
    }

    // --- ループ SE フェード処理 ---
    for ( auto it = loopSEVoices_.begin(); it != loopSEVoices_.end(); ) {
        auto& loopSE = it->second;
        if ( !loopSE.isFading ) { ++it; continue; }

        loopSE.fadeTimer += kDeltaTime;
        float t = (loopSE.fadeDuration > 0.0f) ? loopSE.fadeTimer / loopSE.fadeDuration : 1.0f;
        t = (std::min)(t, 1.0f);

        loopSE.voice->SetVolume(loopSE.fadeFrom + (loopSE.fadeTo - loopSE.fadeFrom) * t);

        if ( t >= 1.0f ) {
            loopSE.isFading = false;
            if ( loopSE.stopAfterFade ) {
                loopSE.voice->Stop(0);
                it = loopSEVoices_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// ─────────────────────────────────────────
// 旧 API（後方互換のため残す）
// ─────────────────────────────────────────

void AudioManager::UpdateVoices(){
    for ( auto it = playingVoices_.begin(); it != playingVoices_.end(); ) {
        XAUDIO2_VOICE_STATE st{};
        it->voice->GetState(&st);
        if ( st.BuffersQueued == 0 ) { it = playingVoices_.erase(it); }
        else { ++it; }
    }
}

void AudioManager::LoadWave(const std::string& filename){
    if ( soundDatas_.find(filename) != soundDatas_.end() ) { return; }
    if ( filename.find(".mp3") != std::string::npos ) {
        soundDatas_[filename] = LoadMP3Internal(filename);
    } else {
        soundDatas_[filename] = LoadWaveInternal(filename);
    }
}

void AudioManager::PlayWave(const std::string& filename, bool loop, float volume){
    assert(soundDatas_.contains(filename));
    SoundData& soundData = soundDatas_[filename];

    IXAudio2SourceVoice* rawVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&rawVoice, &soundData.wfex);
    assert(SUCCEEDED(result));
    SourceVoicePtr voice(rawVoice);

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(soundData.buffer.size());
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    if ( loop ) { buf.LoopCount = XAUDIO2_LOOP_INFINITE; buf.Flags = 0; }

    voice->SetVolume(volume);
    result = voice->SubmitSourceBuffer(&buf);  assert(SUCCEEDED(result));
    result = voice->Start();                    assert(SUCCEEDED(result));

    playingVoices_.push_back({ std::move(voice), filename });
}

SoundData AudioManager::LoadMP3Internal(const std::string& filename){
    HRESULT hr;
    SoundData soundData = {};

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &filename[0], (int)filename.size(), NULL, 0);
    std::wstring wFilename(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &filename[0], (int)filename.size(), &wFilename[0], size_needed);

    IMFSourceReader* pReader = nullptr;
    hr = MFCreateSourceReaderFromURL(wFilename.c_str(), nullptr, &pReader);
    assert(SUCCEEDED(hr));

    IMFMediaType* pPartialType = nullptr;
    hr = MFCreateMediaType(&pPartialType);           assert(SUCCEEDED(hr));
    hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
    assert(SUCCEEDED(hr));
    pPartialType->Release();

    IMFMediaType* pUncompressedAudioType = nullptr;
    hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pUncompressedAudioType);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX* pWavFormat = nullptr;
    UINT32 cbFormat = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(pUncompressedAudioType, &pWavFormat, &cbFormat);
    assert(SUCCEEDED(hr));

    memcpy(&soundData.wfex, pWavFormat, sizeof(WAVEFORMATEX));
    CoTaskMemFree(pWavFormat);
    pUncompressedAudioType->Release();

    IMFSample* pSample = nullptr;
    DWORD dwFlags = 0;
    while ( true ) {
        hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &dwFlags, nullptr, &pSample);
        assert(SUCCEEDED(hr));
        if ( dwFlags & MF_SOURCE_READERF_ENDOFSTREAM ) { break; }

        if ( pSample ) {
            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);  assert(SUCCEEDED(hr));

            BYTE* pAudioData = nullptr;
            DWORD cbCurrentLength = 0;
            hr = pBuffer->Lock(&pAudioData, nullptr, &cbCurrentLength);  assert(SUCCEEDED(hr));

            size_t currentSize = soundData.buffer.size();
            soundData.buffer.resize(currentSize + cbCurrentLength);
            memcpy(soundData.buffer.data() + currentSize, pAudioData, cbCurrentLength);

            pBuffer->Unlock();  pBuffer->Release();  pSample->Release();
        }
    }
    pReader->Release();
    return soundData;
}

SoundData AudioManager::LoadWaveInternal(const std::string& filename){
    std::ifstream file(filename, std::ios::binary);
    assert(file.is_open());

    RiffHeader riff;
    file.read(( char* ) &riff, sizeof(riff));
    assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(strncmp(riff.type, "WAVE", 4) == 0);

    ChunkHeader chunk;
    WAVEFORMATEX wfex = {};
    std::vector<BYTE> buffer;

    while ( file.read(( char* ) &chunk, sizeof(chunk)) ) {
        if ( strncmp(chunk.id, "fmt ", 4) == 0 ) {
            assert(chunk.size <= sizeof(WAVEFORMATEX));
            file.read(( char* ) &wfex, chunk.size);
        } else if ( strncmp(chunk.id, "data", 4) == 0 ) {
            buffer.resize(chunk.size);
            file.read(( char* ) buffer.data(), chunk.size);
        } else {
            file.seekg(chunk.size, std::ios_base::cur);
        }
        if ( wfex.nChannels != 0 && !buffer.empty() ) { break; }
    }
    file.close();

    SoundData soundData{};
    soundData.wfex   = wfex;
    soundData.buffer = std::move(buffer);
    return soundData;
}
