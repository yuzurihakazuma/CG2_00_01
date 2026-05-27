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

// --- エンジン ---
#include "engine/math/struct.h" // Vector3（PlaySE3D に使用）

// 音声データ
struct SoundData{
    WAVEFORMATEX      wfex;
    std::vector<BYTE> buffer;
};

// IXAudio2SourceVoice のデリータ
struct SourceVoiceDeleter{
    void operator()(IXAudio2SourceVoice* v) const noexcept{
        if ( v ) { v->DestroyVoice(); }
    }
};
// IXAudio2SourceVoice のスマートポインタ
using SourceVoicePtr = std::unique_ptr<IXAudio2SourceVoice, SourceVoiceDeleter>;


class AudioManager{
public: // シングルトンパターン
    static AudioManager* GetInstance();

public: // メンバ関数

    // 初期化
    void Initialize();
    // 終了処理
    void Finalize();

    // ─────────────────────────────────────────
    // 名前登録システム（サウンドバンク）
    // ─────────────────────────────────────────
    // 音声ファイルに短い名前を割り当てる
    void Register(const std::string& name, const std::string& filepath);

    // 同じ役割の音を複数ファイルでまとめて登録（PlaySE 時にランダムで選ばれる）
    // 例：足音・ヒット音など繰り返し使う SE のバリエーションに
    void RegisterVariants(const std::string& name, const std::vector<std::string>& filepaths);

    // Register / RegisterVariants で登録した全音声を一括でメモリに読み込む
    void LoadAll();

    // 使わなくなった音声データをメモリから解放する（再生停止後に呼ぶこと）
    void Unload(const std::string& name);

    // ─────────────────────────────────────────
    // BGM 専用関数
    // ─────────────────────────────────────────
    // BGM 再生（ループ自動ON）。fadeInSec > 0 でフェードイン
    void PlayBGM(const std::string& name, float fadeInSec = 0.0f);

    // BGM 停止。fadeOutSec > 0 でフェードアウトしてから止まる
    void StopBGM(float fadeOutSec = 0.0f);

    // BGM 切り替え（前をフェードアウトしながら次をフェードイン）
    void ChangeBGM(const std::string& name, float fadeSec = 1.0f);

    // BGM 一時停止（続きから再開できる）。fadeOutSec > 0 でふわっと消える
    void PauseBGM(float fadeOutSec = 0.0f);

    // BGM 再開（PauseBGM で止めた続きから）。fadeInSec > 0 でフェードイン
    void ResumeBGM(float fadeInSec = 0.0f);

    // BGM 音量をフェードで変える（ポーズ中に下げるなど）
    void FadeBGMVolume(float targetVolume, float durationSec);

    // BGM が再生中か（ポーズ中は false）
    bool IsBGMPlaying() const { return bgmVoice_ != nullptr && !isBGMPaused_; }

    // 現在再生中の BGM 名（再生していなければ空文字）
    const std::string& GetCurrentBGMName() const { return currentBGMName_; }

    // ─────────────────────────────────────────
    // SE 再生（撃ちっぱなし）
    // ─────────────────────────────────────────
    // name          : Register / RegisterVariants で登録した名前
    // volume        : この SE 個別の音量 (0.0f ~ 1.0f)
    // pitchVariation: ランダムピッチ変化の幅（0.0f = なし, 0.1f = ±10%）
    // maxPolyphony  : 同時再生の上限（0 = 無制限）
    // cooldownSec   : 同じ SE を鳴らせる最短間隔（0.0f = 制限なし）
    void PlaySE(const std::string& name,
                float volume        = 1.0f,
                float pitchVariation = 0.0f,
                int   maxPolyphony  = 0,
                float cooldownSec   = 0.0f);

    // 3D 位置情報を渡して距離に応じた音量で SE を再生する
    // soundPos    : 音源の座標
    // listenerPos : 聴き手（カメラ・プレイヤー）の座標
    // maxDistance : これ以上離れると聴こえなくなる距離
    void PlaySE3D(const std::string& name,
                  const Vector3& soundPos,
                  const Vector3& listenerPos,
                  float maxDistance,
                  float volume        = 1.0f,
                  float pitchVariation = 0.0f,
                  int   maxPolyphony  = 0,
                  float cooldownSec   = 0.0f);

    // ─────────────────────────────────────────
    // ループ SE（環境音・足音など鳴らし続ける SE）
    // ─────────────────────────────────────────
    // 再生して「ハンドル」を返す。返ってきた値で操作・停止できる
    int  PlaySELoop(const std::string& name, float volume = 1.0f);

    // ループ SE を停止する。fadeOutSec > 0 でフェードアウトして止まる
    void StopSELoop(int handle, float fadeOutSec = 0.0f);

    // ループ SE の音量を変える。fadeSec > 0 でフェードして変化
    void SetSELoopVolume(int handle, float volume, float fadeSec = 0.0f);

    // ─────────────────────────────────────────
    // SE 停止
    // ─────────────────────────────────────────
    // 指定した名前の SE を全インスタンス即停止（ループ SE も含む）
    void StopSE(const std::string& name);

    // 再生中の全 SE を即停止（ループ SE も含む）
    void StopAllSE();

    // ─────────────────────────────────────────
    // ミュート
    // ─────────────────────────────────────────
    void MuteBGM(bool mute);
    bool IsBGMMuted() const { return bgmMuted_; }

    void MuteSE(bool mute);
    bool IsSEMuted() const { return seMuted_; }

    void MuteAll(bool mute);

    // ─────────────────────────────────────────
    // 音量カテゴリ
    // ─────────────────────────────────────────
    void  SetMasterVolume(float v);
    float GetMasterVolume() const { return masterVolume_; }

    void  SetBGMVolume(float v);
    float GetBGMVolume() const { return bgmVolume_; }

    void  SetSEVolume(float v);
    float GetSEVolume() const { return seVolume_; }

    // ─────────────────────────────────────────
    // 音量設定の保存・読み込み
    // ─────────────────────────────────────────
    void SaveSettings(const std::string& filepath = "resources/audio_settings.dat");
    void LoadSettings(const std::string& filepath = "resources/audio_settings.dat");

    // ─────────────────────────────────────────
    // アプリフォーカス制御
    // ─────────────────────────────────────────
    // ウィンドウがフォーカスを失ったら全音声をミュート
    void OnFocusLost();
    // ウィンドウがフォーカスを取り戻したら音声を復帰
    void OnFocusGained();

    // ─────────────────────────────────────────
    // デバッグ用
    // ─────────────────────────────────────────
    // 指定した名前が Register / RegisterVariants 済みか確認する
    bool IsRegistered(const std::string& name) const;

    // 指定した名前の SE が現在何インスタンス再生中か返す（ループ SE も含む）
    int  GetSEPlayCount(const std::string& name) const;

    // ─────────────────────────────────────────
    // 毎フレーム呼ぶ更新処理
    // ─────────────────────────────────────────
    void Update();

    // ─────────────────────────────────────────
    // 旧 API（後方互換のため残す）
    // ─────────────────────────────────────────
    void      LoadWave(const std::string& filename);
    SoundData LoadMP3Internal(const std::string& filename);
    void      PlayWave(const std::string& filename, bool loop = false, float volume = 1.0f);
    void      UpdateVoices();

private: // 内部型定義

    // フェード完了後の BGM の動作
    enum class BGMFadeAction{
        None,
        StopAndDestroy, // ボイスを止めて破棄（StopBGM のフェードアウト用）
        StopAndPause,   // ボイスを止めるが破棄しない（PauseBGM 用）
        ChangeVolume,   // 音量を目標値に保つだけ（FadeBGMVolume / フェードイン用）
    };

    // 再生中の SE（撃ちっぱなし）
    struct ActiveSE{
        SourceVoicePtr voice;
        std::string    name;
    };

    // ループ SE 1エントリ
    struct LoopSE{
        SourceVoicePtr voice;
        std::string    name;
        float          volume      = 1.0f;
        float          fadeFrom    = 0.0f;
        float          fadeTo      = 0.0f;
        float          fadeDuration = 0.0f;
        float          fadeTimer   = 0.0f;
        bool           isFading    = false;
        bool           stopAfterFade = false;
    };

    // 内部 WAV 読み込み用
    struct ChunkHeader{ char id[4]; int32_t size; };
    struct RiffHeader{ ChunkHeader chunk; char type[4]; };
    struct FormatChunk{ ChunkHeader chunk; WAVEFORMATEX fmt; };

    SoundData LoadWaveInternal(const std::string& filename);

    // 名前 → ファイルパス解決 + 自動ロード（バリエーションにも対応）
    std::string Resolve(const std::string& name);

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

private: // メンバ変数

    // XAudio2 本体
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;

    // 読み込んだ音声データ（キー: ファイルパス）
    std::map<std::string, SoundData> soundDatas_;

    // 再生中の SE（撃ちっぱなし）
    std::vector<ActiveSE> playingVoices_;

    // ─────────────────────────────────────────
    // 名前登録システム
    // ─────────────────────────────────────────
    std::map<std::string, std::string>              soundRegistry_;   // 名前 → 1パス
    std::map<std::string, std::vector<std::string>> variantRegistry_; // 名前 → 複数パス

    // ─────────────────────────────────────────
    // BGM 管理
    // ─────────────────────────────────────────
    SourceVoicePtr bgmVoice_;
    std::string    currentBGMName_;
    bool           isBGMPaused_    = false;
    std::string    pendingBGMName_;
    float          pendingBGMFadeIn_ = 0.0f;

    // ─────────────────────────────────────────
    // ループ SE 管理
    // ─────────────────────────────────────────
    std::map<int, LoopSE> loopSEVoices_;
    int nextLoopHandle_ = 1;

    // ─────────────────────────────────────────
    // SE クールダウン（名前 → 残り秒数）
    // ─────────────────────────────────────────
    std::map<std::string, float> seCooldownTimers_;

    // ─────────────────────────────────────────
    // 音量カテゴリ
    // ─────────────────────────────────────────
    float masterVolume_ = 1.0f;
    float bgmVolume_    = 0.5f;
    float seVolume_     = 1.0f;

    // ─────────────────────────────────────────
    // ミュート / フォーカス状態
    // ─────────────────────────────────────────
    bool bgmMuted_   = false;
    bool seMuted_    = false;
    bool isFocused_  = true; // アプリがフォーカスを持っているか

    // ─────────────────────────────────────────
    // BGM フェード制御
    // ─────────────────────────────────────────
    float         bgmFadeFrom_     = 0.0f;
    float         bgmFadeTo_       = 0.0f;
    float         bgmFadeDuration_ = 0.0f;
    float         bgmFadeTimer_    = 0.0f;
    bool          isBGMFading_     = false;
    BGMFadeAction bgmFadeAction_   = BGMFadeAction::None;
};
