#include "SDFAtlas.h"

#include <fstream>
#include "externals/nlohmann/json.hpp"
#include "engine/graphics/TextureManager.h"

using json = nlohmann::json;

namespace {
    // UTF-8 文字列の先頭コードポイントを返す
    uint32_t DecodeUTF8First(const std::string& str) {
        if ( str.empty() ) return 0;
        unsigned char c = static_cast<unsigned char>( str[0] );
        if ( c < 0x80 ) {
            return c;
        } else if ( ( c & 0xE0 ) == 0xC0 && str.size() >= 2 ) {
            return ( ( c & 0x1F ) << 6 ) | ( static_cast<unsigned char>( str[1] ) & 0x3F );
        } else if ( ( c & 0xF0 ) == 0xE0 && str.size() >= 3 ) {
            return ( ( c & 0x0F ) << 12 )
                | ( ( static_cast<unsigned char>( str[1] ) & 0x3F ) << 6 )
                | ( static_cast<unsigned char>( str[2] ) & 0x3F );
        } else if ( ( c & 0xF8 ) == 0xF0 && str.size() >= 4 ) {
            return ( ( c & 0x07 ) << 18 )
                | ( ( static_cast<unsigned char>( str[1] ) & 0x3F ) << 12 )
                | ( ( static_cast<unsigned char>( str[2] ) & 0x3F ) << 6 )
                | ( static_cast<unsigned char>( str[3] ) & 0x3F );
        }
        return 0xFFFD;
    }

    std::filesystem::file_time_type SafeWriteTime(const std::string& path) {
        std::error_code ec;
        return std::filesystem::last_write_time(path, ec);
    }
}

bool SDFAtlas::Load(const std::string& jsonPath, const std::string& pngPath,
                    ID3D12GraphicsCommandList* commandList) {
    jsonPath_ = jsonPath;
    pngPath_ = pngPath;
    name_ = std::filesystem::path(jsonPath).stem().string();
    // "arial_sdf" → "arial" のように末尾の _sdf を落として表示名にする
    if ( name_.size() > 4 && name_.substr(name_.size() - 4) == "_sdf" ) {
        name_ = name_.substr(0, name_.size() - 4);
    }

    // --- PNG をアトラステクスチャとしてロード ---
    TextureData td = TextureManager::GetInstance()->LoadTextureAndCreateSRV(pngPath, commandList);
    srvIndex_ = td.srvIndex;
    atlasWidth_ = td.width > 0.0f ? td.width : 1024.0f;
    atlasHeight_ = td.height > 0.0f ? td.height : 1024.0f;

    if ( !ParseJson(jsonPath) ) return false;

    jsonTime_ = SafeWriteTime(jsonPath_);
    pngTime_ = SafeWriteTime(pngPath_);
    return true;
}

bool SDFAtlas::IsFileModified() const {
    return SafeWriteTime(jsonPath_) != jsonTime_ || SafeWriteTime(pngPath_) != pngTime_;
}

bool SDFAtlas::Reload(ID3D12GraphicsCommandList* commandList) {
    // テクスチャは同じ SRV スロットへ上書き（参照側のインデックスは有効なまま）
    TextureData td = TextureManager::GetInstance()->ReloadTextureAndCreateSRV(pngPath_, commandList);
    srvIndex_ = td.srvIndex;
    atlasWidth_ = td.width > 0.0f ? td.width : atlasWidth_;
    atlasHeight_ = td.height > 0.0f ? td.height : atlasHeight_;

    glyphs_.clear();
    sprites_.clear();
    if ( !ParseJson(jsonPath_) ) return false;

    jsonTime_ = SafeWriteTime(jsonPath_);
    pngTime_ = SafeWriteTime(pngPath_);
    return true;
}

bool SDFAtlas::ParseJson(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if ( !file.is_open() ) return false;

    json j;
    try {
        file >> j;
    } catch ( ... ) {
        return false; // 生成途中の不完全なJSONは次のリロードで拾う
    }

    if ( j.contains("textureWidth") )  atlasWidth_ = j["textureWidth"].get<float>();
    if ( j.contains("textureHeight") ) atlasHeight_ = j["textureHeight"].get<float>();
    if ( j.contains("size") )          baseSize_ = j["size"].get<float>();
    keepColor_ = j.value("keepColor", false);

    // --- フォント形式: "glyphs": { "A": { x,y,width,height,offsetX,offsetY,advance } } ---
    if ( j.contains("glyphs") && j["glyphs"].is_object() ) {
        type_ = Type::Font;
        for ( auto it = j["glyphs"].begin(); it != j["glyphs"].end(); ++it ) {
            uint32_t cp = DecodeUTF8First(it.key());
            if ( cp == 0 ) continue;
            const json& g = it.value();
            SDFGlyph glyph {};
            float x = g.value("x", 0.0f);
            float y = g.value("y", 0.0f);
            float w = g.value("width", 0.0f);
            float h = g.value("height", 0.0f);
            glyph.u0 = x / atlasWidth_;
            glyph.v0 = y / atlasHeight_;
            glyph.u1 = ( x + w ) / atlasWidth_;
            glyph.v1 = ( y + h ) / atlasHeight_;
            glyph.width = w;
            glyph.height = h;
            glyph.offsetX = g.value("offsetX", 0.0f);
            glyph.offsetY = g.value("offsetY", 0.0f);
            glyph.advance = g.value("advance", w);
            glyphs_[cp] = glyph;
        }
        return !glyphs_.empty();
    }

    // --- スプライト形式: "sprites": { "name": { x,y,width,height,... } } ---
    if ( j.contains("sprites") && j["sprites"].is_object() ) {
        type_ = Type::Sprite;
        for ( auto it = j["sprites"].begin(); it != j["sprites"].end(); ++it ) {
            const json& s = it.value();
            SDFSpriteInfo info {};
            float x = s.value("x", 0.0f);
            float y = s.value("y", 0.0f);
            float w = s.value("width", 0.0f);
            float h = s.value("height", 0.0f);
            info.u0 = x / atlasWidth_;
            info.v0 = y / atlasHeight_;
            info.u1 = ( x + w ) / atlasWidth_;
            info.v1 = ( y + h ) / atlasHeight_;
            info.width = w;
            info.height = h;
            info.originalWidth = s.value("originalWidth", w);
            info.originalHeight = s.value("originalHeight", h);
            info.offsetX = s.value("offsetX", 0.0f);
            info.offsetY = s.value("offsetY", 0.0f);
            sprites_[it.key()] = info;
        }
        return !sprites_.empty();
    }

    type_ = Type::Unknown;
    return false;
}

const SDFGlyph* SDFAtlas::GetGlyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return it != glyphs_.end() ? &it->second : nullptr;
}

const SDFSpriteInfo* SDFAtlas::GetSprite(const std::string& name) const {
    auto it = sprites_.find(name);
    return it != sprites_.end() ? &it->second : nullptr;
}
