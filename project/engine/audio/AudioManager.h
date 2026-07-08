#pragma once


// --- 標準ライブラリ ---
#include <string>
#include <map>
#include <cstdint>
#include <vector>
#include <memory>
#include <wrl/client.h>

// --- 外部ライブラリ ---
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib,"xaudio2.lib")

// 音声データ
struct SoundData{
	// 波型フォーマット
	WAVEFORMATEX wfex;
	// 音声バッファ
    std::vector<BYTE> buffer;

};

// IXAudio2SourceVoiceのデリータ
struct SourceVoiceDeleter{
	// デストラクタ
    void operator()(IXAudio2SourceVoice* v) const noexcept{
        if ( v ) {
            v->DestroyVoice();
        }
    }
};
// IXAudio2SourceVoiceのスマートポインタ
using SourceVoicePtr = std::unique_ptr<IXAudio2SourceVoice, SourceVoiceDeleter>;

// 音の種別（BGM / SE をまとめて音量調整できるように）
enum class AudioCategory{ BGM, SE };

// =====================================================================
//  AudioHandle : 再生中の1音を後から操作するための軽量ハンドル
//   Play() が返す。id=0 は無効。Stop/音量/フェード等は AudioManager に委譲。
// =====================================================================
class AudioHandle{
public:
    AudioHandle() = default;
    explicit AudioHandle(uint32_t id) : id_(id) {}

    bool IsValid() const { return id_ != 0; }
    uint32_t Id() const { return id_; }

    void  Stop();
    void  Pause();
    void  Resume();
    void  SetVolume(float volume);     // 0.0〜1.0（ユーザ音量）
    void  FadeOut(float seconds);      // seconds かけて消音→停止
    void  FadeIn(float seconds, float targetVolume = 1.0f);
    bool  IsPlaying() const;

private:
    uint32_t id_ = 0;
};


class AudioManager{
public: // シングルトンパターン
    static AudioManager* GetInstance();

public: // メンバ関数

    // 初期化
    void Initialize();

    // 終了処理
    void Finalize();

    // WAVファイルの読み込み
    // filename: ファイルパス ("resources/bgm.wav" など)
    void LoadWave(const std::string& filename);

    SoundData LoadMP3Internal(const std::string& filename);


    // 音声再生（従来互換：ハンドルを返さない）
    // volume: 音量 (0.0f ~ 1.0f)
    void PlayWave(const std::string& filename, bool loop = false, float volume = 1.0f);

    // 音声再生（ハンドルを返す版。Stop/音量/フェードに使う）
    AudioHandle Play(const std::string& filename, bool loop = false, float volume = 1.0f,
                     AudioCategory category = AudioCategory::SE);

    // --- 全体音量（0.0〜1.0）---
    void  SetMasterVolume(float v);
    float GetMasterVolume() const { return masterVolume_; }
    void  SetCategoryVolume(AudioCategory cat, float v);
    float GetCategoryVolume(AudioCategory cat) const;

    // --- ハンドルから呼ばれる個別操作（id 指定）---
    void StopById(uint32_t id);
    void PauseById(uint32_t id);
    void ResumeById(uint32_t id);
    void SetVolumeById(uint32_t id, float volume);
    void FadeById(uint32_t id, float seconds, float targetVolume, bool stopWhenDone);
    bool IsPlayingById(uint32_t id) const;

    // 指定カテゴリ／全部を止める
    void StopAll();
    void StopCategory(AudioCategory category);

	// 音声更新（毎フレーム：フェード進行＋終了ボイスの掃除）
    void UpdateVoices();

private: // 内部構造体（WAV読み込み用）
    // チャンクヘッダ
    struct ChunkHeader{
        char id[4]; // チャンクID
        int32_t size; // チャンクのサイズ
    };
    // RIFFヘッダチャンク
    struct RiffHeader{
        ChunkHeader chunk; // RIFF
        char type[4]; // WAVE
    };
    // FMTチャンク
    struct FormatChunk{
        ChunkHeader chunk; // FMT
        WAVEFORMATEX fmt;  // 波形フォーマット
    };

    // 内部的なWAV読み込み関数
    SoundData LoadWaveInternal(const std::string& filename);

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

private:

    // XAudio2の本体
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    // マスターボイス（スピーカー出力）
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    // 読み込んだ音声データの管理コンテナ
    // キー: ファイル名, 値: 音声データ
    std::map<std::string, SoundData> soundDatas_;

    // 再生中の1音の情報（id で管理し、ハンドルから操作できるようにする）
    struct PlayingVoice{
        SourceVoicePtr voice;
        AudioCategory  category = AudioCategory::SE;
        float userVolume = 1.0f;    // ユーザ指定の音量
        bool  paused = false;
        bool  loop = false;
        // フェード
        bool  fading = false;
        float fadeTime = 0.0f;      // フェード総時間
        float fadeElapsed = 0.0f;
        float fadeFrom = 1.0f;      // フェード係数 from
        float fadeTo = 1.0f;        // フェード係数 to
        float fadeMul = 1.0f;       // 現在のフェード係数(0〜1)
        bool  stopWhenFadeDone = false;
    };

    // 実際にボイスへ与える音量を計算してセットする
    void ApplyVolume(PlayingVoice& pv);

    std::map<uint32_t, PlayingVoice> voices_; // id → 再生中ボイス
    uint32_t nextId_ = 1;                      // 次に発行する id（0は無効）

    float masterVolume_ = 1.0f;
    float categoryVolume_[2] = { 1.0f, 1.0f }; // [0]=BGM, [1]=SE

};