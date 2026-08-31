#include "SDFSprite.h"

#include <cstring>
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

void SDFSprite::Initialize() {
    auto factory = ResourceFactory::GetInstance();
    transformBuffer_ = factory->CreateBufferResource(sizeof(TransformCB));
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &transformData_ ));
    paramsBuffer_ = factory->CreateBufferResource(sizeof(ParamsCB));
    paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &paramsData_ ));
    dirty_ = true;
}

void SDFSprite::SetSpriteName(const std::string& name) {
    if ( spriteName_ != name ) { spriteName_ = name; dirty_ = true; }
}
void SDFSprite::SetPosition(float x, float y) {
    if ( posX_ != x || posY_ != y ) { posX_ = x; posY_ = y; dirty_ = true; }
}
void SDFSprite::SetScale(float scale) {
    if ( scale_ != scale ) { scale_ = scale; dirty_ = true; }
}

void SDFSprite::SetTransform3D(const Vector3& worldPos, float worldScale) {
    use3D_ = true;
    worldPos3D_ = worldPos;
    worldScale3D_ = worldScale;
    dirty_ = true;
}

void SDFSprite::SetScreenMode() {
    if ( use3D_ ) { use3D_ = false; dirty_ = true; }
}

// 1枚分の四角形を作り直す
void SDFSprite::RebuildGeometry(const SDFAtlas& atlas) {
    indexCount_ = 0;

    const SDFSpriteInfo* info = atlas.GetSprite(spriteName_);
    // 名前未指定/不一致なら最初のスプライトを使う（アトラス1枚=1画像の場合に便利）
    if ( !info && !atlas.GetSprites().empty() ) {
        info = &atlas.GetSprites().begin()->second;
    }
    if ( !info ) return;

    Vertex vertices[4];
    if ( use3D_ ) {
        // 3D: 原点中心の板（高さ=worldScale、幅=アスペクト比から）。配置はWVP側で行う
        float aspect = ( info->height > 0.0f ) ? ( info->width / info->height ) : 1.0f;
        float hh = worldScale3D_ * 0.5f;
        float hw = hh * aspect;
        vertices[0] = { -hw, +hh, info->u0, info->v0 };
        vertices[1] = { +hw, +hh, info->u1, info->v0 };
        vertices[2] = { -hw, -hh, info->u0, info->v1 };
        vertices[3] = { +hw, -hh, info->u1, info->v1 };
    } else {
        // 2D: 左上(posX,posY)基準。offsetX/offsetY（オートクロップ補正）で見た目位置を復元
        float x0 = posX_ + info->offsetX * scale_;
        float y0 = posY_ + info->offsetY * scale_;
        float x1 = x0 + info->width * scale_;
        float y1 = y0 + info->height * scale_;
        vertices[0] = { x0, y0, info->u0, info->v0 };
        vertices[1] = { x1, y0, info->u1, info->v0 };
        vertices[2] = { x0, y1, info->u0, info->v1 };
        vertices[3] = { x1, y1, info->u1, info->v1 };
    }
    uint32_t indices[6] = { 0, 1, 2, 1, 3, 2 };

    auto factory = ResourceFactory::GetInstance();

    vertexBuffer_ = factory->CreateBufferResource(sizeof(vertices));
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices, sizeof(vertices));
    vertexBuffer_->Unmap(0, nullptr);
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = sizeof(vertices);
    vbv_.StrideInBytes = sizeof(Vertex);

    indexBuffer_ = factory->CreateBufferResource(sizeof(indices));
    indexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, indices, sizeof(indices));
    indexBuffer_->Unmap(0, nullptr);
    ibv_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibv_.SizeInBytes = sizeof(indices);
    ibv_.Format = DXGI_FORMAT_R32_UINT;

    indexCount_ = 6;
}

void SDFSprite::UpdateBuffers(const SDFAtlas& atlas) {
    if ( transformData_ ) {
        const Camera* camera = Obj3dCommon::GetInstance()->GetDefaultCamera();
        if ( use3D_ && camera ) {
            // 3D: WVP = ワールド(平行移動) × カメラのビュー射影
            Matrix4x4 world = MatrixMath::MakeTranslate(worldPos3D_);
            Matrix4x4 wvp = MatrixMath::Multiply(world, camera->GetViewProjectionMatrix());
            std::memcpy(transformData_->mat, &wvp, sizeof(float) * 16);
        } else {
            // 2D: スクリーン座標の正射影
            auto dxCommon = DirectXCommon::GetInstance();
            float W = static_cast<float>( dxCommon->GetClientWidth() );
            float H = static_cast<float>( dxCommon->GetClientHeight() );
            std::memset(transformData_, 0, sizeof(TransformCB));
            transformData_->mat[0] = 2.0f / W;
            transformData_->mat[5] = -2.0f / H;
            transformData_->mat[10] = 1.0f;
            transformData_->mat[12] = -1.0f;
            transformData_->mat[13] = 1.0f;
            transformData_->mat[15] = 1.0f;
        }
    }
    if ( paramsData_ ) {
        auto setV4 = [](float* dst, const Vector4& c) {
            dst[0] = c.x; dst[1] = c.y; dst[2] = c.z; dst[3] = c.w;
        };
        setV4(paramsData_->baseColor, color_);
        setV4(paramsData_->outlineColor, outlineColor_);
        setV4(paramsData_->glowColor, glowColor_);
        // 近接フェード（本体・フチ・グローの全アルファに掛ける）
        paramsData_->baseColor[3]    *= drawAlpha_;
        paramsData_->outlineColor[3] *= drawAlpha_;
        paramsData_->glowColor[3]    *= drawAlpha_;
        paramsData_->edgeBias = edgeBias_;
        paramsData_->outlineWidth = outlineWidth_;
        paramsData_->glowWidth = glowWidth_;
        // keepColor アトラス: 距離=A / 色=テクスチャRGB。単色アトラス: 距離=R / 色=baseColor
        bool keep = atlas.IsKeepColor();
        paramsData_->sdfInAlpha = keep ? 1.0f : 0.0f;
        paramsData_->useTexColor = keep ? 1.0f : 0.0f;
    }
}

void SDFSprite::Update(const SDFAtlas& atlas) {
    // 2D/3D モードが切り替わったらジオメトリを作り直す
    if ( prev3D_ != use3D_ ) { prev3D_ = use3D_; dirty_ = true; }
    if ( dirty_ ) {
        RebuildGeometry(atlas);
        dirty_ = false;
    }
    UpdateBuffers(atlas);
}

void SDFSprite::Draw(ID3D12GraphicsCommandList* commandList, const SDFAtlas& atlas) {
    if ( indexCount_ == 0 ) return;

    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);
    commandList->SetGraphicsRootConstantBufferView(0, transformBuffer_->GetGPUVirtualAddress()); // b0
    commandList->SetGraphicsRootConstantBufferView(1, paramsBuffer_->GetGPUVirtualAddress());    // b1
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, atlas.GetSrvIndex());           // t0
    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
