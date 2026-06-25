#include "SDFFont.h"

#include <fstream>
#include "externals/nlohmann/json.hpp"
#include "engine/graphics/TextureManager.h"

using json = nlohmann::json;

// UTF-8 文字列の先頭コードポイントを解析して返す
static uint32_t DecodeUTF8First(const std::string& str) {
    if (str.empty()) return 0;
    unsigned char c = static_cast<unsigned char>(str[0]);
    if (c < 0x80) {
        return c;
    } else if ((c & 0xE0) == 0xC0 && str.size() >= 2) {
        return ((c & 0x1F) << 6) | (static_cast<unsigned char>(str[1]) & 0x3F);
    } else if ((c & 0xF0) == 0xE0 && str.size() >= 3) {
        return ((c & 0x0F) << 12)
             | ((static_cast<unsigned char>(str[1]) & 0x3F) << 6)
             |  (static_cast<unsigned char>(str[2]) & 0x3F);
    } else if ((c & 0xF8) == 0xF0 && str.size() >= 4) {
        return ((c & 0x07) << 18)
             | ((static_cast<unsigned char>(str[1]) & 0x3F) << 12)
             | ((static_cast<unsigned char>(str[2]) & 0x3F) << 6)
             |  (static_cast<unsigned char>(str[3]) & 0x3F);
    }
    return 0xFFFD;
}

bool SDFFont::Load(const std::string& jsonPath,
                   const std::string& pngPath,
                   ID3D12GraphicsCommandList* commandList)
{
    // --- PNG をアトラステクスチャとしてロード ---
    TextureData td = TextureManager::GetInstance()->LoadTextureAndCreateSRV(pngPath, commandList);
    atlasSrvIndex_ = td.srvIndex;
    atlasWidth_    = td.width  > 0.0f ? td.width  : 1024.0f;
    atlasHeight_   = td.height > 0.0f ? td.height : 1024.0f;

    // --- JSON をパース ---
    std::ifstream file(jsonPath);
    if (!file.is_open()) { return false; }

    json j;
    file >> j;

    if (j.contains("textureWidth"))  atlasWidth_  = j["textureWidth"].get<float>();
    if (j.contains("textureHeight")) atlasHeight_ = j["textureHeight"].get<float>();
    if (j.contains("size"))          baseSize_    = j["size"].get<float>();

    if (!j.contains("glyphs")) { return false; }
    auto& glyphsJson = j["glyphs"];

    auto parseGlyph = [&](uint32_t cp, const json& g) {
        SDFGlyph glyph{};
        float x = g.value("x", 0.0f);
        float y = g.value("y", 0.0f);
        float w = g.value("width",  0.0f);
        float h = g.value("height", 0.0f);
        glyph.u0 = x / atlasWidth_;
        glyph.v0 = y / atlasHeight_;
        glyph.u1 = (x + w) / atlasWidth_;
        glyph.v1 = (y + h) / atlasHeight_;
        glyph.width    = w;
        glyph.height   = h;
        glyph.offsetX  = g.value("offsetX", 0.0f);
        glyph.offsetY  = g.value("offsetY", 0.0f);
        glyph.advance  = g.value("advance", w);
        glyphs_[cp] = glyph;
    };

    if (glyphsJson.is_object()) {
        // 実フォーマット: キーが文字そのもの ("A", "あ", " ")
        for (auto it = glyphsJson.begin(); it != glyphsJson.end(); ++it) {
            uint32_t cp = DecodeUTF8First(it.key());
            if (cp != 0) parseGlyph(cp, it.value());
        }
    } else if (glyphsJson.is_array()) {
        for (auto& g : glyphsJson) {
            uint32_t cp = 0;
            if (g.contains("codepoint")) {
                cp = g["codepoint"].get<uint32_t>();
            } else if (g.contains("char")) {
                cp = DecodeUTF8First(g["char"].get<std::string>());
            }
            if (cp != 0) parseGlyph(cp, g);
        }
    }

    return !glyphs_.empty();
}

const SDFGlyph* SDFFont::GetGlyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it == glyphs_.end()) return nullptr;
    return &it->second;
}
