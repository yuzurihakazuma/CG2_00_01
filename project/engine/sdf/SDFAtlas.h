#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include <filesystem>
#include <d3d12.h>

// =====================================================================
//  SDFAtlas
//   Python(SDFPrintf.py) が出力した SDF アトラス（JSON + PNG）1組を表す。
//   JSON の中身からフォント("glyphs")かスプライト("sprites")かを自動判別する。
//   ホットリロード対応：ファイル更新を検知したら Reload で中身だけ差し替える
//   （SRVスロットは維持されるので、参照している側はそのまま使える）。
// =====================================================================

// 1文字分のグリフ情報（フォントアトラス用）
// 座標は「上から下」モデル。offsetY は行の上端から下向きが正。
struct SDFGlyph {
    float u0, v0, u1, v1;  // アトラス上の UV (0-1)
    float offsetX;          // カーソルからの水平オフセット (px / 基準サイズ時)
    float offsetY;          // 行上端からの垂直オフセット (px / 基準サイズ時)
    float width, height;    // グリフサイズ (px / 基準サイズ時)
    float advance;          // 次の文字への送り幅 (px / 基準サイズ時)
};

// 1スプライト分の情報（スプライトアトラス用）
struct SDFSpriteInfo {
    float u0, v0, u1, v1;   // アトラス上の UV (0-1)
    float width, height;     // アトラス上での切り出しサイズ (px)
    float originalWidth;     // 元画像の幅 (px)
    float originalHeight;    // 元画像の高さ (px)
    float offsetX, offsetY;  // オートクロップ補正オフセット (px)
};

class SDFAtlas {
public:
    enum class Type {
        Unknown,
        Font,    // "glyphs" を持つ（グレースケール: R=距離）
        Sprite,  // "sprites" を持つ（keepColor時 RGBA: RGB=色, A=距離）
    };

    // jsonPath / pngPath のペアを読み込む（テクスチャ転送があるので安全なタイミングで呼ぶこと）
    bool Load(const std::string& jsonPath, const std::string& pngPath,
              ID3D12GraphicsCommandList* commandList);

    // ファイルが更新されていたら true（ホットリロードの判定に使う）
    bool IsFileModified() const;

    // 中身を再読込（SRVスロット維持。安全なタイミングで呼ぶこと）
    bool Reload(ID3D12GraphicsCommandList* commandList);

    // --- 参照 ---
    Type     GetType() const { return type_; }
    bool     IsFont() const { return type_ == Type::Font; }
    bool     IsKeepColor() const { return keepColor_; }
    uint32_t GetSrvIndex() const { return srvIndex_; }
    float    GetAtlasWidth() const { return atlasWidth_; }
    float    GetAtlasHeight() const { return atlasHeight_; }
    float    GetBaseSize() const { return baseSize_; }        // フォント: 生成時サイズ
    const std::string& GetName() const { return name_; }      // 表示名（ファイル名から）
    const std::string& GetJsonPath() const { return jsonPath_; }

    const SDFGlyph* GetGlyph(uint32_t codepoint) const;
    const SDFSpriteInfo* GetSprite(const std::string& name) const;
    // スプライト名一覧（エディタのコンボ用）
    const std::unordered_map<std::string, SDFSpriteInfo>& GetSprites() const { return sprites_; }
    int GetGlyphCount() const { return ( int ) glyphs_.size(); }

private:
    bool ParseJson(const std::string& jsonPath);

    Type type_ = Type::Unknown;
    std::string name_;
    std::string jsonPath_;
    std::string pngPath_;

    std::unordered_map<uint32_t, SDFGlyph> glyphs_;
    std::unordered_map<std::string, SDFSpriteInfo> sprites_;

    uint32_t srvIndex_ = 0;
    float atlasWidth_ = 1024.0f;
    float atlasHeight_ = 1024.0f;
    float baseSize_ = 48.0f;
    bool  keepColor_ = false;

    // ホットリロード用のタイムスタンプ
    std::filesystem::file_time_type jsonTime_ {};
    std::filesystem::file_time_type pngTime_ {};
};
