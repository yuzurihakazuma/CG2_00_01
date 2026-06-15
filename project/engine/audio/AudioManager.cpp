#include "AudioManager.h"
// --- 標準ライブラリ ---
#include <fstream>
#include <cassert>
#include <cstring>
// --- エンジン側のファイル ---
#include "engine/base/TimeManager.h"

AudioManager* AudioManager::GetInstance(){
    static AudioManager instance;
    return &instance;
}

void AudioManager::Initialize(){
    HRESULT result;
    // COMとMedia Foundationの初期化
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    // XAudio2エンジンのインスタンス生成
    result = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(result));

    // マスターボイス生成
    result = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(result));
}

void AudioManager::Finalize(){
	// 再生中の音声ボイスの破棄
    voices_.clear();


	// マスターボイスの破棄
    if ( masterVoice_ ) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    // XAudio2の解放
    xAudio2_.Reset();

	// 読み込んだ音声データの解放
    soundDatas_.clear();

    MFShutdown();
    CoUninitialize();
}

// 実音量 = ユーザ音量 × フェード係数 × カテゴリ音量 × マスター音量
void AudioManager::ApplyVolume(PlayingVoice& pv){
    float catVol = categoryVolume_[( pv.category == AudioCategory::BGM ) ? 0 : 1];
    float v = pv.userVolume * pv.fadeMul * catVol * masterVolume_;
    if ( v < 0.0f ) v = 0.0f;
    if ( pv.voice ) pv.voice->SetVolume(v);
}

void AudioManager::UpdateVoices(){
    const float dt = Time::GetInstance()->GetUnscaledDeltaTime(); // 音はポーズに影響されない実時間で

    for ( auto it = voices_.begin(); it != voices_.end(); ) {
        PlayingVoice& pv = it->second;

        // --- フェードの進行 ---
        if ( pv.fading ) {
            pv.fadeElapsed += dt;
            float t = ( pv.fadeTime > 1e-6f ) ? ( pv.fadeElapsed / pv.fadeTime ) : 1.0f;
            if ( t >= 1.0f ) { t = 1.0f; pv.fading = false; }
            pv.fadeMul = pv.fadeFrom + ( pv.fadeTo - pv.fadeFrom ) * t;
            ApplyVolume(pv);
            if ( !pv.fading && pv.stopWhenFadeDone ) {
                if ( pv.voice ) pv.voice->Stop();
                it = voices_.erase(it); // ボイス破棄
                continue;
            }
        }

        // --- 再生終了の掃除（ポーズ中・ループ中は消さない）---
        if ( !pv.paused && !pv.loop && pv.voice ) {
            XAUDIO2_VOICE_STATE st {};
            pv.voice->GetState(&st);
            if ( st.BuffersQueued == 0 ) {
                it = voices_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

// 内部共通：1音を生成して再生し、id を返す
AudioHandle AudioManager::Play(const std::string& filename, bool loop, float volume, AudioCategory category){
    assert(soundDatas_.contains(filename));
    SoundData& soundData = soundDatas_[filename];

    IXAudio2SourceVoice* rawVoice = nullptr;
    HRESULT result = xAudio2_->CreateSourceVoice(&rawVoice, &soundData.wfex);
    assert(SUCCEEDED(result));
    SourceVoicePtr voice(rawVoice);

    XAUDIO2_BUFFER buf {};
    buf.pAudioData = soundData.buffer.data();
    buf.AudioBytes = static_cast< UINT32 >( soundData.buffer.size() );
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if ( loop ) { buf.LoopCount = XAUDIO2_LOOP_INFINITE; buf.Flags = 0; }

    result = voice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(result));

    uint32_t id = nextId_++;
    if ( nextId_ == 0 ) nextId_ = 1; // 0(無効)は飛ばす

    PlayingVoice pv;
    pv.voice = std::move(voice);
    pv.category = category;
    pv.userVolume = volume;
    pv.loop = loop;
    pv.fadeMul = 1.0f;

    // 登録してから音量適用＆再生
    auto& stored = ( voices_[id] = std::move(pv) );
    ApplyVolume(stored);
    stored.voice->Start();

    return AudioHandle(id);
}

void AudioManager::SetMasterVolume(float v){
    masterVolume_ = ( v < 0.0f ) ? 0.0f : v;
    for ( auto& [id, pv] : voices_ ) { ApplyVolume(pv); }
}
void AudioManager::SetCategoryVolume(AudioCategory cat, float v){
    categoryVolume_[( cat == AudioCategory::BGM ) ? 0 : 1] = ( v < 0.0f ) ? 0.0f : v;
    for ( auto& [id, pv] : voices_ ) { ApplyVolume(pv); }
}
float AudioManager::GetCategoryVolume(AudioCategory cat) const{
    return categoryVolume_[( cat == AudioCategory::BGM ) ? 0 : 1];
}

void AudioManager::StopById(uint32_t id){
    auto it = voices_.find(id);
    if ( it == voices_.end() ) return;
    if ( it->second.voice ) it->second.voice->Stop();
    voices_.erase(it);
}
void AudioManager::PauseById(uint32_t id){
    auto it = voices_.find(id);
    if ( it == voices_.end() || !it->second.voice ) return;
    it->second.voice->Stop();
    it->second.paused = true;
}
void AudioManager::ResumeById(uint32_t id){
    auto it = voices_.find(id);
    if ( it == voices_.end() || !it->second.voice ) return;
    it->second.voice->Start();
    it->second.paused = false;
}
void AudioManager::SetVolumeById(uint32_t id, float volume){
    auto it = voices_.find(id);
    if ( it == voices_.end() ) return;
    it->second.userVolume = ( volume < 0.0f ) ? 0.0f : volume;
    ApplyVolume(it->second);
}
void AudioManager::FadeById(uint32_t id, float seconds, float targetVolume, bool stopWhenDone){
    auto it = voices_.find(id);
    if ( it == voices_.end() ) return;
    PlayingVoice& pv = it->second;
    pv.fading = true;
    pv.fadeTime = ( seconds > 0.0f ) ? seconds : 0.0001f;
    pv.fadeElapsed = 0.0f;
    pv.fadeFrom = pv.fadeMul;
    pv.fadeTo = ( targetVolume < 0.0f ) ? 0.0f : targetVolume;
    pv.stopWhenFadeDone = stopWhenDone;
}
bool AudioManager::IsPlayingById(uint32_t id) const{
    auto it = voices_.find(id);
    if ( it == voices_.end() ) return false;
    if ( it->second.paused ) return true;
    if ( !it->second.voice ) return false;
    XAUDIO2_VOICE_STATE st {};
    it->second.voice->GetState(&st);
    return st.BuffersQueued > 0;
}
void AudioManager::StopAll(){
    for ( auto& [id, pv] : voices_ ) { if ( pv.voice ) pv.voice->Stop(); }
    voices_.clear();
}
void AudioManager::StopCategory(AudioCategory category){
    for ( auto it = voices_.begin(); it != voices_.end(); ) {
        if ( it->second.category == category ) {
            if ( it->second.voice ) it->second.voice->Stop();
            it = voices_.erase(it);
        } else { ++it; }
    }
}

// ===== AudioHandle（AudioManager に委譲するだけ）=====
void AudioHandle::Stop(){ if ( IsValid() ) AudioManager::GetInstance()->StopById(id_); }
void AudioHandle::Pause(){ if ( IsValid() ) AudioManager::GetInstance()->PauseById(id_); }
void AudioHandle::Resume(){ if ( IsValid() ) AudioManager::GetInstance()->ResumeById(id_); }
void AudioHandle::SetVolume(float volume){ if ( IsValid() ) AudioManager::GetInstance()->SetVolumeById(id_, volume); }
void AudioHandle::FadeOut(float seconds){ if ( IsValid() ) AudioManager::GetInstance()->FadeById(id_, seconds, 0.0f, true); }
void AudioHandle::FadeIn(float seconds, float targetVolume){ if ( IsValid() ) AudioManager::GetInstance()->FadeById(id_, seconds, targetVolume, false); }
bool AudioHandle::IsPlaying() const{ return IsValid() && AudioManager::GetInstance()->IsPlayingById(id_); }


void AudioManager::LoadWave(const std::string& filename) {
    // 既に読み込み済みならスルー
    if (soundDatas_.find(filename) != soundDatas_.end()) {
        return;
    }

    // ".mp3" が含まれているかで分岐
    if (filename.find(".mp3") != std::string::npos) {
        soundDatas_[filename] = LoadMP3Internal(filename);
    }
    else {
        soundDatas_[filename] = LoadWaveInternal(filename);
    }
}
// 従来互換：ハンドルを返さずに再生（内部は Play に委譲）
void AudioManager::PlayWave(const std::string& filename, bool loop, float volume){
    Play(filename, loop, volume, AudioCategory::SE);
}

SoundData AudioManager::LoadMP3Internal(const std::string& filename) {
    HRESULT hr;
    SoundData soundData = {};

    // 1. std::string を std::wstring に変換 (Media Foundationはワイド文字必須なため)
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &filename[0], (int)filename.size(), NULL, 0);
    std::wstring wFilename(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &filename[0], (int)filename.size(), &wFilename[0], size_needed);

    // 2. ソースリーダーの作成（ファイルを開く）
    IMFSourceReader* pReader = nullptr;
    hr = MFCreateSourceReaderFromURL(wFilename.c_str(), nullptr, &pReader);
    assert(SUCCEEDED(hr));

    // 3. オーディオストリームを選択し、強制的に「PCM（無圧縮WAV）」に変換する設定
    IMFMediaType* pPartialType = nullptr;
    hr = MFCreateMediaType(&pPartialType);
    assert(SUCCEEDED(hr));
    hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPartialType);
    assert(SUCCEEDED(hr));
    pPartialType->Release();

    // 4. 変換後のメディアタイプ（WAVEFORMATEX）を取得
    IMFMediaType* pUncompressedAudioType = nullptr;
    hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pUncompressedAudioType);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX* pWavFormat = nullptr;
    UINT32 cbFormat = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(pUncompressedAudioType, &pWavFormat, &cbFormat);
    assert(SUCCEEDED(hr));

    // フォーマットをコピーして確保
    memcpy(&soundData.wfex, pWavFormat, sizeof(WAVEFORMATEX));
    CoTaskMemFree(pWavFormat);
    pUncompressedAudioType->Release();

    // 5. データを最後まで読み込んでバッファに詰める
    IMFSample* pSample = nullptr;
    DWORD dwFlags = 0;
    while (true) {
        hr = pReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &dwFlags, nullptr, &pSample);
        assert(SUCCEEDED(hr));

        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
            break; // 終端に達した
        }

        if (pSample) {
            IMFMediaBuffer* pBuffer = nullptr;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);
            assert(SUCCEEDED(hr));

            BYTE* pAudioData = nullptr;
            DWORD cbCurrentLength = 0;
            hr = pBuffer->Lock(&pAudioData, nullptr, &cbCurrentLength);
            assert(SUCCEEDED(hr));

            // ベクターの末尾に解凍されたデータを追加
            size_t currentSize = soundData.buffer.size();
            soundData.buffer.resize(currentSize + cbCurrentLength);
            memcpy(soundData.buffer.data() + currentSize, pAudioData, cbCurrentLength);

            pBuffer->Unlock();
            pBuffer->Release();
            pSample->Release();
        }
    }

    pReader->Release();

    return soundData; // 解凍済みのピカピカなデータを返す！
}


// 内部用：WAVファイル読み込みの実装（main.cppにあったものを移植）
SoundData AudioManager::LoadWaveInternal(const std::string& filename){
    std::ifstream file(filename, std::ios::binary);
    assert(file.is_open());

    // RIFFヘッダー確認
    RiffHeader riff;
    file.read(( char* ) &riff, sizeof(riff));
    assert(strncmp(riff.chunk.id, "RIFF", 4) == 0);
    assert(strncmp(riff.type, "WAVE", 4) == 0);

    FormatChunk format = {};

    // チャンク情報の初期化
    ChunkHeader chunk;
    WAVEFORMATEX wfex = {};
    std::vector<BYTE> buffer;

    // ファイルを走査
    while ( file.read(( char* ) &chunk, sizeof(chunk)) ) {
        if ( strncmp(chunk.id, "fmt ", 4) == 0 ) {
            // fmtチャンク読み込み
            assert(chunk.size <= sizeof(WAVEFORMATEX));
            file.read(( char* ) &wfex, chunk.size);
        } else if ( strncmp(chunk.id, "data", 4) == 0 ) {
            // dataチャンク読み込み（音声データ本体）
            buffer.resize(chunk.size);
            file.read(( char* ) buffer.data(), chunk.size);
        } else {
            // 不要なチャンクはスキップ
            file.seekg(chunk.size, std::ios_base::cur);
        }

        // 必要な情報が揃ったらループを抜ける
        if ( wfex.nChannels != 0 && !buffer.empty() ) {
            break;
        }
    }
    file.close();

    SoundData soundData {};
    soundData.wfex = wfex;
    soundData.buffer = std::move(buffer);

    return soundData;
}