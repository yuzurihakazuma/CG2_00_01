#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "engine/2d/sdf/SDFSpriteAtlas.h"

// 複数の SDF スプライトアトラスを名前で管理するシングルトン
class SDFSpriteManager {
public:
    static SDFSpriteManager* GetInstance() {
        static SDFSpriteManager instance;
        return &instance;
    }

    bool Load(const std::string& name,
              const std::string& jsonPath,
              const std::string& pngPath,
              ID3D12GraphicsCommandList* commandList)
    {
        auto atlas = std::make_unique<SDFSpriteAtlas>();
        if (!atlas->Load(jsonPath, pngPath, commandList)) return false;
        atlases_[name] = std::move(atlas);
        return true;
    }

    SDFSpriteAtlas* Get(const std::string& name) const {
        auto it = atlases_.find(name);
        return it != atlases_.end() ? it->second.get() : nullptr;
    }

    void Finalize() { atlases_.clear(); }

private:
    SDFSpriteManager() = default;
    ~SDFSpriteManager() = default;
    SDFSpriteManager(const SDFSpriteManager&) = delete;
    SDFSpriteManager& operator=(const SDFSpriteManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<SDFSpriteAtlas>> atlases_;
};
