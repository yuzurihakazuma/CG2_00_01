#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include "engine/2d/sdf/SDFFont.h"

// 複数の SDF フォントを名前で管理するシングルトン
class SDFFontManager {
public:
    static SDFFontManager* GetInstance() {
        static SDFFontManager instance;
        return &instance;
    }

    bool Load(const std::string& name,
              const std::string& jsonPath,
              const std::string& pngPath,
              ID3D12GraphicsCommandList* commandList)
    {
        auto font = std::make_unique<SDFFont>();
        if (!font->Load(jsonPath, pngPath, commandList)) return false;
        fonts_[name] = std::move(font);
        return true;
    }

    SDFFont* Get(const std::string& name) const {
        auto it = fonts_.find(name);
        return it != fonts_.end() ? it->second.get() : nullptr;
    }

    void Finalize() { fonts_.clear(); }

private:
    SDFFontManager() = default;
    ~SDFFontManager() = default;
    SDFFontManager(const SDFFontManager&) = delete;
    SDFFontManager& operator=(const SDFFontManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<SDFFont>> fonts_;
};
