#include "SDFText.h"

#include <algorithm>
#include <cstring>
#include "engine/base/DirectXCommon.h"
#include "engine/graphics/SrvManager.h"
#include "engine/graphics/ResourceFactory.h"
#include "engine/3d/obj/Obj3dCommon.h"
#include "engine/camera/Camera.h"
#include "engine/math/Matrix4x4.h"

void SDFText::Initialize() {
    auto factory = ResourceFactory::GetInstance();
    transformBuffer_ = factory->CreateBufferResource(sizeof(TransformCB));
    transformBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &transformData_ ));
    paramsBuffer_ = factory->CreateBufferResource(sizeof(ParamsCB));
    paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>( &paramsData_ ));
    dirty_ = true;
}

void SDFText::SetText(const std::string& utf8Text) {
    if ( text_ != utf8Text ) { text_ = utf8Text; dirty_ = true; }
}
void SDFText::SetPosition(float x, float y) {
    if ( posX_ != x || posY_ != y ) { posX_ = x; posY_ = y; dirty_ = true; }
}
void SDFText::SetFontSize(float size) {
    if ( fontSize_ != size ) { fontSize_ = size; dirty_ = true; }
}

void SDFText::SetTransform3D(const Vector3& worldPos, float worldScale) {
    use3D_ = true;
    worldPos3D_ = worldPos;
    worldScale3D_ = worldScale;
    // ジオメトリはピクセル空間のまま共用し、3D化は行列側で行う（再構築不要）
}

void SDFText::SetScreenMode() {
    use3D_ = false;
}

std::vector<uint32_t> SDFText::DecodeUTF8(const std::string& str) {
    std::vector<uint32_t> codepoints;
    size_t i = 0;
    while ( i < str.size() ) {
        unsigned char c = static_cast<unsigned char>( str[i] );
        uint32_t cp = 0;
        if ( c < 0x80 ) {
            cp = c; ++i;
        } else if ( ( c & 0xE0 ) == 0xC0 && i + 1 < str.size() ) {
            cp = ( ( c & 0x1F ) << 6 ) | ( static_cast<unsigned char>( str[i + 1] ) & 0x3F );
            i += 2;
        } else if ( ( c & 0xF0 ) == 0xE0 && i + 2 < str.size() ) {
            cp = ( ( c & 0x0F ) << 12 )
                | ( ( static_cast<unsigned char>( str[i + 1] ) & 0x3F ) << 6 )
                | ( static_cast<unsigned char>( str[i + 2] ) & 0x3F );
            i += 3;
        } else if ( ( c & 0xF8 ) == 0xF0 && i + 3 < str.size() ) {
            cp = ( ( c & 0x07 ) << 18 )
                | ( ( static_cast<unsigned char>( str[i + 1] ) & 0x3F ) << 12 )
                | ( ( static_cast<unsigned char>( str[i + 2] ) & 0x3F ) << 6 )
                | ( static_cast<unsigned char>( str[i + 3] ) & 0x3F );
            i += 4;
        } else { ++i; continue; }
        codepoints.push_back(cp);
    }
    return codepoints;
}

// 文字列から頂点/インデックスバッファを作り直す
void SDFText::RebuildGeometry(const SDFAtlas& atlas) {
    indexCount_ = 0;
    if ( text_.empty() || !atlas.IsFont() ) return;

    auto codepoints = DecodeUTF8(text_);

    // フォント生成時の基準サイズに対するスケール比
    float baseSize = atlas.GetBaseSize() > 0.0f ? atlas.GetBaseSize() : 48.0f;
    float scale = fontSize_ / baseSize;
    float lineGap = fontSize_;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(codepoints.size() * 4);
    indices.reserve(codepoints.size() * 6);

    float cursorX = posX_; // 現在のペン位置 X
    float lineTop = posY_; // 現在の行の上端 Y

    for ( uint32_t cp : codepoints ) {
        if ( cp == '\n' ) {
            cursorX = posX_;
            lineTop += lineGap;
            continue;
        }
        const SDFGlyph* g = atlas.GetGlyph(cp);
        if ( !g ) { cursorX += fontSize_ * 0.5f; continue; } // 未登録文字はスペース扱い
        if ( g->width <= 0.0f || g->height <= 0.0f ) {
            cursorX += g->advance * scale; // スペース等は送りのみ
            continue;
        }

        // 上から下モデル: 行上端 + offsetY がグリフの上端
        float x0 = cursorX + g->offsetX * scale;
        float y0 = lineTop + g->offsetY * scale;
        float x1 = x0 + g->width * scale;
        float y1 = y0 + g->height * scale;

        uint32_t base = static_cast<uint32_t>( vertices.size() );
        vertices.push_back({ x0, y0, g->u0, g->v0 });
        vertices.push_back({ x1, y0, g->u1, g->v0 });
        vertices.push_back({ x0, y1, g->u0, g->v1 });
        vertices.push_back({ x1, y1, g->u1, g->v1 });

        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);

        cursorX += g->advance * scale;
    }

    if ( vertices.empty() ) return;

    // 文字列全体のバウンズを計測（3D配置の中央揃えに使う）
    boundsMinX_ = boundsMinY_ = 1e9f;
    boundsMaxX_ = boundsMaxY_ = -1e9f;
    for ( const Vertex& v : vertices ) {
        boundsMinX_ = ( std::min )( boundsMinX_, v.x );
        boundsMinY_ = ( std::min )( boundsMinY_, v.y );
        boundsMaxX_ = ( std::max )( boundsMaxX_, v.x );
        boundsMaxY_ = ( std::max )( boundsMaxY_, v.y );
    }

    auto factory = ResourceFactory::GetInstance();

    size_t vbSize = vertices.size() * sizeof(Vertex);
    vertexBuffer_ = factory->CreateBufferResource(vbSize);
    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, vertices.data(), vbSize);
    vertexBuffer_->Unmap(0, nullptr);
    vbv_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vbv_.SizeInBytes = static_cast<UINT>( vbSize );
    vbv_.StrideInBytes = sizeof(Vertex);

    size_t ibSize = indices.size() * sizeof(uint32_t);
    indexBuffer_ = factory->CreateBufferResource(ibSize);
    indexBuffer_->Map(0, nullptr, &mapped);
    std::memcpy(mapped, indices.data(), ibSize);
    indexBuffer_->Unmap(0, nullptr);
    ibv_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
    ibv_.SizeInBytes = static_cast<UINT>( ibSize );
    ibv_.Format = DXGI_FORMAT_R32_UINT;

    indexCount_ = static_cast<uint32_t>( indices.size() );
}

// 変換行列（2D=正射影 / 3D=WVP）とパラメータを CB へ転送
void SDFText::UpdateBuffers() {
    if ( transformData_ ) {
        const Camera* camera = Obj3dCommon::GetInstance()->GetDefaultCamera();
        if ( use3D_ && camera ) {
            // 3D: ピクセル座標の文字列を「1行の高さ = worldScale3D_ (m)」に縮小し、
            //     バウンズ中心が worldPos3D_ に来るよう平行移動してから、カメラのVPへ。
            //     Yはスクリーン下向き→ワールド上向きなのでスケールの -k で反転する。
            float k = ( fontSize_ > 0.0f ) ? ( worldScale3D_ / fontSize_ ) : 0.01f;
            float cx = ( boundsMinX_ + boundsMaxX_ ) * 0.5f;
            float cy = ( boundsMinY_ + boundsMaxY_ ) * 0.5f;
            Matrix4x4 world = MatrixMath::Multiply(
                MatrixMath::MakeScale({ k, -k, 1.0f }),
                MatrixMath::MakeTranslate({ worldPos3D_.x - cx * k, worldPos3D_.y + cy * k, worldPos3D_.z }));
            Matrix4x4 wvp = MatrixMath::Multiply(world, camera->GetViewProjectionMatrix());
            std::memcpy(transformData_->mat, &wvp, sizeof(float) * 16);
        } else {
            auto dxCommon = DirectXCommon::GetInstance();
            float W = static_cast<float>( dxCommon->GetClientWidth() );
            float H = static_cast<float>( dxCommon->GetClientHeight() );
            std::memset(transformData_, 0, sizeof(TransformCB));
            transformData_->mat[0] = 2.0f / W;   // x:[0,W] → [-1,1]
            transformData_->mat[5] = -2.0f / H;  // y:[0,H] → [1,-1]
            transformData_->mat[10] = 1.0f;
            transformData_->mat[12] = -1.0f;
            transformData_->mat[13] = 1.0f;
            transformData_->mat[15] = 1.0f;
        }
    }
    if ( paramsData_ ) {
        paramsData_->textColor[0] = color_.x;
        paramsData_->textColor[1] = color_.y;
        paramsData_->textColor[2] = color_.z;
        paramsData_->textColor[3] = color_.w * drawAlpha_;         // 近接フェードを掛ける
        paramsData_->outlineColor[0] = outlineColor_.x;
        paramsData_->outlineColor[1] = outlineColor_.y;
        paramsData_->outlineColor[2] = outlineColor_.z;
        paramsData_->outlineColor[3] = outlineColor_.w * drawAlpha_;
        paramsData_->edgeWidth = edgeWidth_;
        paramsData_->outlineWidth = outlineWidth_;
        paramsData_->boldOffset = thickness_;
    }
}

void SDFText::Update(const SDFAtlas& atlas) {
    if ( dirty_ ) {
        RebuildGeometry(atlas);
        dirty_ = false;
    }
    UpdateBuffers();
}

void SDFText::Draw(ID3D12GraphicsCommandList* commandList, const SDFAtlas& atlas) {
    if ( indexCount_ == 0 ) return;

    commandList->IASetVertexBuffers(0, 1, &vbv_);
    commandList->IASetIndexBuffer(&ibv_);
    commandList->SetGraphicsRootConstantBufferView(0, transformBuffer_->GetGPUVirtualAddress()); // b0
    commandList->SetGraphicsRootConstantBufferView(1, paramsBuffer_->GetGPUVirtualAddress());    // b1
    SrvManager::GetInstance()->SetGraphicsRootDescriptorTable(2, atlas.GetSrvIndex());           // t0
    commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
