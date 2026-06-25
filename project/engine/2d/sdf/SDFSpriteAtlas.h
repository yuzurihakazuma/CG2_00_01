#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include <d3d12.h>

// アトラス内の 1 スプライト分の情報
// Python(SDFPrintf.py) の sprite モード出力に対応:
//   "sprites": { "name": { x,y,width,height,originalWidth,originalHeight,offsetX,offsetY } }
struct SDFSpriteInfo {
    float u0, v0, u1, v1;
    float width, height;
    float originalWidth;
    float originalHeight;
    float offsetX;
    float offsetY;
};

// SDF スプライトアトラス
// keepColor=true → RGBA (RGB=色, A=SDF) / keepColor=false → グレースケール (R=SDF)
class SDFSpriteAtlas {
public:
    bool Load(const std::string& jsonPath,
              const std::string& pngPath,
              ID3D12GraphicsCommandList* commandList);

    const SDFSpriteInfo* GetSprite(const std::string& name) const;

    uint32_t GetAtlasSrvIndex() const { return atlasSrvIndex_; }
    float    GetAtlasWidth()   const { return atlasWidth_; }
    float    GetAtlasHeight()  const { return atlasHeight_; }
    bool     IsKeepColor()     const { return keepColor_; }

private:
    std::unordered_map<std::string, SDFSpriteInfo> sprites_;
    uint32_t atlasSrvIndex_ = 0;
    float atlasWidth_  = 1024.0f;
    float atlasHeight_ = 1024.0f;
    bool  keepColor_   = false;
};
