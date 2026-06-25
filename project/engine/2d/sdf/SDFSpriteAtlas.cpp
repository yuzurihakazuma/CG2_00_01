#include "SDFSpriteAtlas.h"

#include <fstream>
#include "externals/nlohmann/json.hpp"
#include "engine/graphics/TextureManager.h"

using json = nlohmann::json;

bool SDFSpriteAtlas::Load(const std::string& jsonPath,
                          const std::string& pngPath,
                          ID3D12GraphicsCommandList* commandList)
{
    TextureData td = TextureManager::GetInstance()->LoadTextureAndCreateSRV(pngPath, commandList);
    atlasSrvIndex_ = td.srvIndex;
    atlasWidth_    = td.width  > 0.0f ? td.width  : 1024.0f;
    atlasHeight_   = td.height > 0.0f ? td.height : 1024.0f;

    std::ifstream file(jsonPath);
    if (!file.is_open()) { return false; }

    json j;
    file >> j;

    if (j.contains("textureWidth"))  atlasWidth_  = j["textureWidth"].get<float>();
    if (j.contains("textureHeight")) atlasHeight_ = j["textureHeight"].get<float>();
    if (j.contains("keepColor"))     keepColor_   = j["keepColor"].get<bool>();

    if (!j.contains("sprites")) { return false; }
    auto& spritesJson = j["sprites"];
    if (!spritesJson.is_object()) { return false; }

    for (auto it = spritesJson.begin(); it != spritesJson.end(); ++it) {
        const json& s = it.value();
        SDFSpriteInfo info{};
        float x = s.value("x", 0.0f);
        float y = s.value("y", 0.0f);
        float w = s.value("width",  0.0f);
        float h = s.value("height", 0.0f);
        info.u0 = x / atlasWidth_;
        info.v0 = y / atlasHeight_;
        info.u1 = (x + w) / atlasWidth_;
        info.v1 = (y + h) / atlasHeight_;
        info.width          = w;
        info.height         = h;
        info.originalWidth  = s.value("originalWidth",  w);
        info.originalHeight = s.value("originalHeight", h);
        info.offsetX        = s.value("offsetX", 0.0f);
        info.offsetY        = s.value("offsetY", 0.0f);
        sprites_[it.key()] = info;
    }

    return !sprites_.empty();
}

const SDFSpriteInfo* SDFSpriteAtlas::GetSprite(const std::string& name) const {
    auto it = sprites_.find(name);
    if (it == sprites_.end()) return nullptr;
    return &it->second;
}
