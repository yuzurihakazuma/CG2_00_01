#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include <d3d12.h>

// 1文字分のグリフ情報
// Python(SDFPrintf.py) の font モード出力に対応:
//   "glyphs": { "A": { x,y,width,height,offsetX,offsetY,advance } }
//   ※ 座標は「上から下」モデル。offsetY は行の上端から下向きが正。
struct SDFGlyph {
    float u0, v0, u1, v1;  // アトラス上の UV 座標 (0-1)
    float offsetX;          // カーソルからの水平オフセット (px / 基準サイズ時)
    float offsetY;          // 行上端からの垂直オフセット (px / 基準サイズ時, 下が正)
    float width;            // グリフの幅 (px / 基準サイズ時)
    float height;           // グリフの高さ (px / 基準サイズ時)
    float advance;          // 次の文字への水平移動量 (px / 基準サイズ時)
};

// SDF フォントアトラス
// Python 生成の グレースケール PNG + JSON("glyphs") を読み込む
class SDFFont {
public:
    bool Load(const std::string& jsonPath,
              const std::string& pngPath,
              ID3D12GraphicsCommandList* commandList);

    const SDFGlyph* GetGlyph(uint32_t codepoint) const;

    uint32_t GetAtlasSrvIndex() const { return atlasSrvIndex_; }
    float    GetAtlasWidth()   const { return atlasWidth_; }
    float    GetAtlasHeight()  const { return atlasHeight_; }
    float    GetBaseSize()     const { return baseSize_; }   // JSON "size"

private:
    std::unordered_map<uint32_t, SDFGlyph> glyphs_;
    uint32_t atlasSrvIndex_ = 0;
    float atlasWidth_  = 1024.0f;
    float atlasHeight_ = 1024.0f;
    float baseSize_    = 48.0f;
};
